'use client';

import {useState} from 'react';

import {
  Alert,
  AlertTitle,
  Card,
  CardContent,
  CardHeader,
  Skeleton,
} from '@mui/material';
import {Check, Copy} from 'lucide-react';

import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {tupperwareTaskLink} from '@platform/internal-links';

function formatDuration(totalSeconds: number): string {
  if (totalSeconds <= 0) {
    return '0s';
  }
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  const parts: string[] = [];
  if (hours > 0) {
    parts.push(`${hours}h`);
  }
  if (minutes > 0) {
    parts.push(`${minutes}m`);
  }
  if (seconds > 0 || parts.length === 0) {
    parts.push(`${seconds}s`);
  }
  return parts.join(' ');
}

function CopyButton({value}: {value: string}) {
  const [copied, setCopied] = useState(false);

  const handleCopy = () => {
    navigator.clipboard.writeText(value).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    });
  };

  return (
    <button
      type="button"
      onClick={handleCopy}
      className="ml-2 inline-flex items-center rounded p-1 text-muted-foreground hover:bg-muted hover:text-foreground transition-colors"
      aria-label="Copy to clipboard">
      {copied ? <Check className="size-3.5" /> : <Copy className="size-3.5" />}
    </button>
  );
}

function DescriptionItem({
  label,
  children,
}: {
  label: string;
  children: React.ReactNode;
}) {
  return (
    <div className="flex items-start justify-between gap-4 border-b border-border py-2 last:border-b-0">
      <dt className="shrink-0 text-sm font-medium text-muted-foreground">
        {label}
      </dt>
      <dd className="min-w-0 text-right text-sm [overflow-wrap:anywhere]">
        {children}
      </dd>
    </div>
  );
}

export default function SummaryPage() {
  const {metadata, loading, error} = useProblemMetadata();
  const {handle} = useRebalancerHandle();

  if (loading) {
    return (
      <div className="p-8">
        <Card className="mx-auto max-w-2xl">
          <CardHeader title={<Skeleton width={192} height={24} />} />
          <CardContent className="flex flex-col gap-3">
            {Array.from({length: 10}, (_, i) => (
              <Skeleton key={i} variant="rectangular" height={20} />
            ))}
          </CardContent>
        </Card>
      </div>
    );
  }

  if (error) {
    return (
      <div className="p-8">
        <Alert severity="error" className="mx-auto max-w-2xl">
          <AlertTitle>Error</AlertTitle>
          {error}
        </Alert>
      </div>
    );
  }

  if (metadata == null) {
    return null;
  }

  return (
    <div className="p-8">
      <Card className="mx-auto max-w-2xl">
        <CardHeader title="Summary" />
        <CardContent>
          <dl>
            <DescriptionItem label="Run ID">
              <span className="inline-flex max-w-full min-w-0 items-start">
                <span className="min-w-0 font-mono text-xs break-all">
                  {metadata.runId}
                </span>
                <CopyButton value={metadata.runId} />
              </span>
            </DescriptionItem>
            <DescriptionItem label="Service Name">
              {metadata.serviceName}
            </DescriptionItem>
            <DescriptionItem label="Service Scope">
              {metadata.serviceScope}
            </DescriptionItem>
            <DescriptionItem label="Solver Type">
              {metadata.solverType}
            </DescriptionItem>
            <DescriptionItem label="Solver End Reason">
              {metadata.solverEndReason}
            </DescriptionItem>
            <DescriptionItem label="Total Runtime">
              {formatDuration(metadata.totalRuntime)}
            </DescriptionItem>
            <DescriptionItem label="Number of Objects">
              {metadata.numObjects}
            </DescriptionItem>
            <DescriptionItem label="Number of Containers">
              {metadata.numContainers}
            </DescriptionItem>
            <DescriptionItem label="Number of Dimensions">
              {metadata.numDimensions}
            </DescriptionItem>
            <DescriptionItem label="Number of Scopes">
              {metadata.numScopes}
            </DescriptionItem>
            {handle != null &&
              (() => {
                const twTask = tupperwareTaskLink(handle.taskId);
                return twTask == null ? null : (
                  <DescriptionItem label="TW Task">
                    <a
                      href={twTask.href}
                      target="_blank"
                      rel="noopener noreferrer"
                      className="text-blue-600 hover:underline dark:text-blue-400">
                      {twTask.label}
                    </a>
                  </DescriptionItem>
                );
              })()}
          </dl>
        </CardContent>
      </Card>
    </div>
  );
}
