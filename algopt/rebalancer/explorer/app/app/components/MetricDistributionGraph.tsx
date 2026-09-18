'use client';

import {useCallback, useEffect, useState} from 'react';

import {Alert, CircularProgress, Typography} from '@mui/material';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';

import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {fetchMetricDistribution} from '@/lib/rebalancer-explorer-api';
import type {MetricDistributionResponse} from '@/lib/rebalancer-explorer-types';

const MAX_POINTS = 1000;

const COLORS = [
  '#1976d2',
  '#d32f2f',
  '#388e3c',
  '#f57c00',
  '#7b1fa2',
  '#0097a7',
  '#c2185b',
  '#455a64',
];

interface MetricDistributionGraphProps {
  entityName: string;
  selectedMetrics: string[];
}

interface ChartDataPoint {
  index: number;
  [metricName: string]: number;
}

function buildChartData(
  metrics: string[],
  responses: MetricDistributionResponse[],
): ChartDataPoint[] {
  if (responses.length === 0) {
    return [];
  }

  // Match the www implementation: x = index, y = metric_value.
  // Since the API returns the same number of evenly-spaced points per metric,
  // we align by index position.
  const maxLen = Math.max(...responses.map(r => r.points.length));

  const chartData: ChartDataPoint[] = [];
  for (let idx = 0; idx < maxLen; idx++) {
    const point: ChartDataPoint = {index: idx};

    for (let i = 0; i < metrics.length; i++) {
      const points = responses[i]?.points ?? [];
      if (idx < points.length) {
        point[metrics[i]] = points[idx].metricValue;
      }
    }

    chartData.push(point);
  }

  return chartData;
}

export default function MetricDistributionGraph({
  entityName,
  selectedMetrics,
}: MetricDistributionGraphProps) {
  const {handle} = useRebalancerHandle();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [chartData, setChartData] = useState<ChartDataPoint[]>([]);
  const [loadedMetrics, setLoadedMetrics] = useState<string[]>([]);

  const fetchDistributions = useCallback(async () => {
    if (handle == null || selectedMetrics.length === 0) {
      setChartData([]);
      setLoadedMetrics([]);
      return;
    }

    setLoading(true);
    setError(null);

    try {
      const requests = selectedMetrics.map(metric => ({
        entity: entityName,
        metric,
        maxPoints: MAX_POINTS,
      }));

      const responses = await fetchMetricDistribution(handle, requests);
      setChartData(buildChartData(selectedMetrics, responses));
      setLoadedMetrics([...selectedMetrics]);
    } catch (err: unknown) {
      setError(
        err instanceof Error
          ? err.message
          : 'Failed to fetch metric distributions',
      );
    } finally {
      setLoading(false);
    }
  }, [handle, entityName, selectedMetrics]);

  useEffect(() => {
    void fetchDistributions();
  }, [fetchDistributions]);

  if (loading) {
    return (
      <div className="flex items-center justify-center py-8">
        <CircularProgress size={32} />
      </div>
    );
  }

  if (error != null) {
    return <Alert severity="error">{error}</Alert>;
  }

  if (chartData.length === 0) {
    return null;
  }

  return (
    <>
      <Typography variant="subtitle2" align="center" gutterBottom>
        Distribution of {entityName}
      </Typography>
      <ResponsiveContainer width="100%" height={400}>
        <LineChart data={chartData}>
          <CartesianGrid strokeDasharray="3 3" />
          <XAxis
            dataKey="index"
            type="number"
            domain={['dataMin', 'dataMax']}
          />
          <YAxis
            label={{
              value: 'Metric Value',
              angle: -90,
              position: 'insideLeft',
            }}
            domain={['auto', 'auto']}
            tickFormatter={(v: number) =>
              Number.isInteger(v) ? String(v) : v.toFixed(2)
            }
          />
          <Tooltip
            formatter={(value: number | string) => Number(value).toFixed(2)}
            labelFormatter={(label: number) => `Index: ${label}`}
          />
          <Legend />
          {loadedMetrics.map((metric, i) => (
            <Line
              key={metric}
              type="monotone"
              dataKey={metric}
              stroke={COLORS[i % COLORS.length]}
              dot={false}
              strokeWidth={2}
            />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </>
  );
}
