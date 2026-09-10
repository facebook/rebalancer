'use client';

import {useMemo, useState} from 'react';

import {Autocomplete, Chip, Paper, TextField, Typography} from '@mui/material';

import {AUTOCOMPLETE_WORD_BREAK_PROPS, CHIP_WORD_BREAK_SX} from '@/lib/format';
import type {ColumnDescription} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';

import MetricDistributionGraph from './MetricDistributionGraph';

interface MetricDistributionSectionProps {
  entityName: string;
  columnDescriptions: ColumnDescription[];
}

const METRIC_TYPES = new Set([ColumnType.DIMENSION, ColumnType.UTILIZATION]);

const COLUMN_TYPE_GROUP_LABEL: Partial<Record<ColumnType, string>> = {
  [ColumnType.DIMENSION]: 'Dimensions',
  [ColumnType.UTILIZATION]: 'Utilizations',
};

const GROUP_ORDER = ['Dimensions', 'Utilizations'];

export default function MetricDistributionSection({
  entityName,
  columnDescriptions,
}: MetricDistributionSectionProps) {
  const metricColumns = useMemo(
    () => columnDescriptions.filter(col => METRIC_TYPES.has(col.type)),
    [columnDescriptions],
  );

  const [selectedMetrics, setSelectedMetrics] = useState<ColumnDescription[]>(
    [],
  );

  if (metricColumns.length === 0) {
    return null;
  }

  const selectedNames = selectedMetrics.map(col => col.name);

  return (
    <Paper className="p-4 space-y-4" variant="outlined">
      <Typography variant="subtitle1">Metric Distributions</Typography>

      <Autocomplete
        multiple
        options={metricColumns}
        getOptionLabel={option => option.name}
        groupBy={option => COLUMN_TYPE_GROUP_LABEL[option.type] ?? 'Other'}
        value={selectedMetrics}
        onChange={(_event, newValue) => setSelectedMetrics(newValue)}
        isOptionEqualToValue={(option, value) => option.name === value.name}
        renderInput={params => (
          <TextField {...params} label="Select Metrics" size="small" />
        )}
        renderTags={(value, getTagProps) =>
          value.map((option, index) => {
            const {key, ...rest} = getTagProps({index});
            return (
              <Chip
                key={key}
                label={option.name}
                size="small"
                sx={CHIP_WORD_BREAK_SX}
                {...rest}
              />
            );
          })
        }
        slotProps={{
          listbox: {
            sx: {
              ...AUTOCOMPLETE_WORD_BREAK_PROPS.listbox.sx,
              '& .MuiAutocomplete-groupLabel': {
                fontWeight: 'bold',
              },
            },
          },
        }}
        filterOptions={(options, {inputValue}) => {
          const lowerInput = inputValue.toLowerCase();
          const filtered = options.filter(opt =>
            opt.name.toLowerCase().includes(lowerInput),
          );
          filtered.sort((a, b) => {
            const groupA = COLUMN_TYPE_GROUP_LABEL[a.type] ?? 'Other';
            const groupB = COLUMN_TYPE_GROUP_LABEL[b.type] ?? 'Other';
            const orderDiff =
              GROUP_ORDER.indexOf(groupA) - GROUP_ORDER.indexOf(groupB);
            if (orderDiff !== 0) return orderDiff;
            return a.name.localeCompare(b.name);
          });
          return filtered;
        }}
      />

      {selectedNames.length > 0 && (
        <MetricDistributionGraph
          entityName={entityName}
          selectedMetrics={selectedNames}
        />
      )}
    </Paper>
  );
}
