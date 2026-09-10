'use client';

import {
  type Dispatch,
  type SetStateAction,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';

import ArrowDownward from '@mui/icons-material/ArrowDownward';
import ArrowUpward from '@mui/icons-material/ArrowUpward';
import WarningAmber from '@mui/icons-material/WarningAmber';
import {
  Autocomplete,
  Box,
  Button,
  Checkbox,
  Chip,
  CircularProgress,
  FormControlLabel,
  IconButton,
  Paper,
  Popover,
  TablePagination,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material';
import {
  createColumnHelper,
  flexRender,
  getCoreRowModel,
  useReactTable,
} from '@tanstack/react-table';

import type {Assignments} from '@/app/components/evaluation/AssignmentCard';
import {
  extractCellValue,
  formatCellValue,
  NAN_STRING,
} from '@/app/components/evaluation/MoveSetsTable.cells';
import {
  PARTITION_AFFECTED_COLUMNS,
  pruneFiltersByColumns,
  SCOPE_AFFECTED_COLUMNS,
} from '@/app/components/evaluation/MoveSetsTable.filters';
import {toThriftAssignment} from '@/lib/assignment';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {
  AUTOCOMPLETE_WORD_BREAK_PROPS,
  CHIP_WORD_BREAK_SX,
  getBadgeColor,
  isHighlightColumn,
  isNumericColumn,
} from '@/lib/format';
import {fetchMoveSets} from '@/lib/rebalancer-explorer-api';
import {
  extractMoveDataFromRow,
  movesFromSelection,
} from '@/lib/move-selection-utils';
import type {SelectedMoveData} from '@/lib/move-selection-utils';
import type {
  CellData,
  Filter,
  MoveSetsRequest,
  Result,
} from '@/lib/rebalancer-explorer-types';
import {OrderDirection} from '@/lib/rebalancer-explorer-types';

import EntityFilter from '@/app/components/EntityFilter';
import TablePaginationActions from '@/app/components/TablePaginationActions';
import useCopyOnClick from '@/app/components/useCopyOnClick';

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const DEFAULT_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [10, 25, 50, 100];

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

type RowData = Record<string, CellData | undefined>;
type ColumnMeta = {numeric?: boolean; colName?: string};

export interface MoveSetsViewState {
  partition: string | null;
  scope: string | null;
  objectives: string[];
  filters: Filter[];
  offset: number;
  pageSize: number;
  orderColumn: string | null;
  orderDirection: OrderDirection;
  isMinimized: boolean;
}

interface MoveSetsTableProps {
  assignments: Assignments;
  viewState: MoveSetsViewState;
  onViewStateChange: Dispatch<SetStateAction<MoveSetsViewState>>;
  enableRowSelection?: boolean;
  onAddMovesToAssignment?: (
    moves: Array<{variable: string; container: string}>,
    target: 'src' | 'dst',
  ) => void;
  pinToInitialFinal?: boolean;
  onPinToInitialFinalChange?: (pinned: boolean) => void;
}

// ---------------------------------------------------------------------------
// Column helper for @tanstack/react-table
// ---------------------------------------------------------------------------

const columnHelper = createColumnHelper<RowData>();

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

export default function MoveSetsTable({
  assignments,
  viewState,
  onViewStateChange,
  enableRowSelection,
  onAddMovesToAssignment,
  pinToInitialFinal,
  onPinToInitialFinalChange,
}: MoveSetsTableProps) {
  const {handle} = useRebalancerHandle();
  const {metadata} = useProblemMetadata();
  const {copyOnClick, copyOnKeyDown, snackbar} = useCopyOnClick();

  // ---- Data state ----
  const [result, setResult] = useState<Result | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // ---- Row selection state ----
  // Keys are global row indices (offset + localIndex) so selection
  // persists across page changes. Values store the move data captured
  // at selection time so cross-page selections can be submitted.
  const [selectedMoves, setSelectedMoves] = useState<
    Map<number, SelectedMoveData>
  >(() => new Map());

  // ---- Destructure controlled view state ----
  const {
    partition,
    scope,
    objectives,
    filters,
    offset,
    pageSize,
    orderColumn,
    orderDirection,
    isMinimized,
  } = viewState;

  // groupBy is not yet user-configurable; kept as a constant for future use.
  const groupByColumns: string[] = useMemo(() => [], []);

  // ---- Warning popover state ----
  const [warningPopoverOpen, setWarningPopoverOpen] = useState(false);
  const warningIconRef = useRef<HTMLButtonElement>(null);

  // ---- Derived values ----
  const tableHeaderStr = metadata?.hasOnlySingleMoves ? 'moves' : 'move sets';
  const totalCount = result?.totalCount ?? 0;

  const isObjSelectorEnabled =
    (metadata?.canDisplayObjChangesInMoveSetsTable ?? false) &&
    groupByColumns.length === 0;

  // ---- Row selection logic ----
  const canSelectMoves: boolean = useMemo(() => {
    if (
      !enableRowSelection ||
      onAddMovesToAssignment == null ||
      result == null
    ) {
      return false;
    }
    const names = new Set(result.columns.map(c => c.name));
    return (
      names.has('Object') &&
      names.has('Source Container') &&
      names.has('Destination Container')
    );
  }, [enableRowSelection, onAddMovesToAssignment, result]);

  const selectedOnPage = useMemo(() => {
    const pageRowCount = result?.rows.length ?? 0;
    let count = 0;
    for (let i = 0; i < pageRowCount; i++) {
      if (selectedMoves.has(offset + i)) {
        count++;
      }
    }
    return count;
  }, [selectedMoves, result, offset]);

  const handleMoveAction = useCallback(
    (
      containerColumn: 'Destination Container' | 'Source Container',
      target: 'src' | 'dst',
    ) => {
      if (onAddMovesToAssignment == null || selectedMoves.size === 0) {
        return;
      }
      const moves = movesFromSelection(selectedMoves, containerColumn);
      if (moves.length > 0) {
        onAddMovesToAssignment(moves, target);
        setSelectedMoves(new Map());
      }
    },
    [onAddMovesToAssignment, selectedMoves],
  );

  // ---- Column descriptions from the latest result ----
  const resultColumns = useMemo(() => result?.columns ?? [], [result]);

  // Latest resultColumns, read by handleObjectivesChange without depending on
  // every fetched page (which would re-create the callback unnecessarily).
  const resultColumnsRef = useRef(resultColumns);
  resultColumnsRef.current = resultColumns;

  // ---- Selector / filter change handlers ----
  // Each handler commits the new selector value, prunes filters whose
  // columns no longer apply, and resets pagination — all in one state
  // transition so the data-fetch effect runs once per user action.

  const handlePartitionChange = useCallback(
    (value: string | null) => {
      onViewStateChange(prev => {
        const next: MoveSetsViewState = {...prev, partition: value, offset: 0};
        // Skip null→partition transitions: those filters were set
        // intentionally (e.g. via a shared URL) and apply as-is.
        if (prev.partition !== null && prev.partition !== value) {
          next.filters = pruneFiltersByColumns(
            prev.filters,
            PARTITION_AFFECTED_COLUMNS,
          );
        }
        return next;
      });
    },
    [onViewStateChange],
  );

  const handleScopeChange = useCallback(
    (value: string | null) => {
      onViewStateChange(prev => {
        const next: MoveSetsViewState = {...prev, scope: value, offset: 0};
        if (prev.scope !== null && prev.scope !== value) {
          next.filters = pruneFiltersByColumns(
            prev.filters,
            SCOPE_AFFECTED_COLUMNS,
          );
        }
        return next;
      });
    },
    [onViewStateChange],
  );

  const handleObjectivesChange = useCallback(
    (value: string[]) => {
      onViewStateChange(prev => {
        const next: MoveSetsViewState = {...prev, objectives: value, offset: 0};
        const removed = prev.objectives.filter(o => !value.includes(o));
        if (removed.length > 0) {
          const colsToRemove = new Set(
            resultColumnsRef.current
              .filter(c => removed.some(obj => c.name.includes(obj)))
              .map(c => c.name),
          );
          if (colsToRemove.size > 0) {
            next.filters = pruneFiltersByColumns(prev.filters, colsToRemove);
          }
        }
        return next;
      });
    },
    [onViewStateChange],
  );

  const handleFiltersChange = useCallback(
    (newFilters: Filter[]) => {
      onViewStateChange(prev => ({...prev, filters: newFilters, offset: 0}));
    },
    [onViewStateChange],
  );

  // ---- Data fetching ----
  const fetchIdRef = useRef(0);

  const doFetch = useCallback(
    async (fetchId: number) => {
      if (handle == null) {
        return;
      }

      setLoading(true);
      setError(null);

      try {
        const assignmentA = toThriftAssignment(assignments.src);
        const assignmentB = toThriftAssignment(assignments.dst);

        const request: MoveSetsRequest = {
          assignmentA,
          assignmentB,
          query: {
            entity: 'move_sets',
            page: {offset, limit: pageSize},
            ...(orderColumn != null
              ? {
                  order: {
                    columns: [{name: orderColumn, direction: orderDirection}],
                  },
                }
              : {}),
            ...(groupByColumns.length > 0
              ? {group: {columns: groupByColumns}}
              : {}),
            ...(filters.length > 0
              ? {filter: {rules: filters.flatMap(f => f.rules)}}
              : {}),
          },
          ...(partition != null ? {partitionName: partition} : {}),
          ...(scope != null ? {scopeName: scope} : {}),
          ...(objectives.length > 0 ? {objectiveNames: objectives} : {}),
        };

        const response = await fetchMoveSets(handle, request);
        // Discard stale responses — only apply if this is still the latest request
        if (fetchId !== fetchIdRef.current) {
          return;
        }
        setResult(response.table);
      } catch (err: unknown) {
        if (fetchId !== fetchIdRef.current) {
          return;
        }
        setError(
          err instanceof Error ? err.message : 'Failed to fetch move sets',
        );
        setResult(null);
      } finally {
        if (fetchId === fetchIdRef.current) {
          setLoading(false);
        }
      }
    },
    [
      handle,
      assignments,
      offset,
      pageSize,
      orderColumn,
      orderDirection,
      groupByColumns,
      partition,
      scope,
      objectives,
      filters,
    ],
  );

  useEffect(() => {
    if (isMinimized) {
      setLoading(false);
      return;
    }
    const id = ++fetchIdRef.current;
    doFetch(id);
    return () => {
      // Invalidate this request so its callbacks become no-ops
      fetchIdRef.current = id + 1;
      setLoading(false);
    };
  }, [doFetch, isMinimized]);

  // Clear selection when data-changing parameters change (NOT on page change).
  useEffect(() => {
    setSelectedMoves(new Map());
  }, [
    partition,
    scope,
    objectives,
    filters,
    orderColumn,
    orderDirection,
    assignments,
  ]);

  // ---- Column definitions ----
  // (resultColumns is declared earlier, before the pruning effects that need it)

  const columnLabels: Record<string, string> = useMemo(() => {
    const labels: Record<string, string> = {};
    const objectName = metadata?.objectName;
    const containerName = metadata?.containerName;
    if (objectName) {
      labels['Object'] = objectName;
    }
    if (containerName) {
      labels['Container'] = containerName;
      labels['Source Container'] = `Source ${containerName}`;
      labels['Destination Container'] = `Destination ${containerName}`;
    }
    return labels;
  }, [metadata?.objectName, metadata?.containerName]);

  // Map move-set column names to the entity names used by the typeahead API.
  // The typeahead backend expects the actual entity name (e.g. "server")
  // rather than the generic column name (e.g. "Object").
  // When a partition is selected, "Group" maps to the partition name.
  // When a scope is selected, "Source ScopeItem" / "Destination ScopeItem"
  // map to the scope name.
  const columnTypeaheadEntities: Record<string, string> = useMemo(() => {
    const entities: Record<string, string> = {};
    const objectName = metadata?.objectName;
    const containerName = metadata?.containerName;
    if (objectName) {
      entities['Object'] = objectName;
    }
    if (containerName) {
      entities['Container'] = containerName;
      entities['Source Container'] = containerName;
      entities['Destination Container'] = containerName;
    }
    if (partition != null) {
      entities['Group'] = partition;
    }
    if (scope != null) {
      entities['Source ScopeItem'] = scope;
      entities['Destination ScopeItem'] = scope;
    }
    return entities;
  }, [metadata?.objectName, metadata?.containerName, partition, scope]);

  const columns = useMemo(() => {
    return resultColumns.map((col, colIndex) =>
      columnHelper.accessor(String(colIndex), {
        id: String(colIndex),
        header: columnLabels[col.name] ?? col.name,
        cell: info => {
          const cellData = info.getValue();
          const value = extractCellValue(cellData, col);

          // NaN chip
          if (value === NAN_STRING) {
            return (
              <Chip
                label="NaN"
                size="small"
                color="error"
                variant="filled"
                sx={{fontVariantNumeric: 'tabular-nums', fontSize: '0.8125rem'}}
              />
            );
          }

          // Highlighted numeric value with colored chip
          if (isHighlightColumn(col.type) && typeof value === 'number') {
            const color = getBadgeColor(value);
            return (
              <Chip
                label={formatCellValue(value)}
                size="small"
                color={color}
                variant={color === 'default' ? 'outlined' : 'filled'}
                sx={{fontVariantNumeric: 'tabular-nums', fontSize: '0.8125rem'}}
              />
            );
          }

          return (
            <span
              style={{
                fontVariantNumeric: 'tabular-nums',
                fontSize: '0.8125rem',
              }}>
              {formatCellValue(value)}
            </span>
          );
        },
        size: isNumericColumn(col.type) ? 130 : 180,
        meta: {
          numeric: isNumericColumn(col.type),
          colName: col.name,
        },
      }),
    );
  }, [columnLabels, resultColumns]);

  // ---- Row data ----
  const data = useMemo<RowData[]>(() => {
    if (result == null) {
      return [];
    }
    return result.rows.map(row => {
      const rowObj: RowData = {};
      resultColumns.forEach((_col, colIndex) => {
        rowObj[String(colIndex)] = row.cells[colIndex];
      });
      return rowObj;
    });
  }, [result, resultColumns]);

  // ---- Table instance ----
  const table = useReactTable({
    data,
    columns,
    getCoreRowModel: getCoreRowModel(),
    manualSorting: true,
    manualPagination: true,
  });

  // ---- Sort handler ----
  const handleSort = useCallback(
    (colIndex: string) => {
      const col = resultColumns[parseInt(colIndex, 10)];
      if (col == null) {
        return;
      }
      onViewStateChange(prev => {
        if (prev.orderColumn === col.name) {
          if (prev.orderDirection === OrderDirection.ASCENDING) {
            return {
              ...prev,
              orderDirection: OrderDirection.DESCENDING,
              offset: 0,
            };
          } else {
            // Clear sort on third click
            return {...prev, orderColumn: null, offset: 0};
          }
        } else {
          return {
            ...prev,
            orderColumn: col.name,
            orderDirection: OrderDirection.ASCENDING,
            offset: 0,
          };
        }
      });
    },
    [resultColumns, onViewStateChange],
  );

  // ---- Pagination handlers ----
  const currentPage = Math.floor(offset / pageSize);

  const handlePageChange = useCallback(
    (_event: unknown, newPage: number) => {
      onViewStateChange(prev => ({...prev, offset: newPage * prev.pageSize}));
    },
    [onViewStateChange],
  );

  const handleRowsPerPageChange = useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      const newSize = parseInt(event.target.value, 10);
      onViewStateChange(prev => ({...prev, pageSize: newSize, offset: 0}));
    },
    [onViewStateChange],
  );

  // ---- Warning message ----
  const warnMsg = useMemo(() => {
    if (isObjSelectorEnabled) {
      return null;
    }
    if (!metadata?.canDisplayObjChangesInMoveSetsTable) {
      return 'Objective changes are unavailable. Re-run the problem with moveStats enabled and showAllChangedObjectivesInMovesSummary set to true.';
    }
    if (groupByColumns.length > 0) {
      return 'Seeing objective changes is disabled when groupBy is active.';
    }
    return null;
  }, [
    isObjSelectorEnabled,
    metadata?.canDisplayObjChangesInMoveSetsTable,
    groupByColumns,
  ]);

  // ---- Render ----
  const headerGroups = table.getHeaderGroups();
  const rows = table.getRowModel().rows;

  return (
    <Paper variant="outlined" sx={{overflow: 'hidden'}}>
      {/* Header */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          px: 2,
          py: 1,
          borderBottom: isMinimized ? undefined : 1,
          borderBottomColor: isMinimized ? undefined : 'divider',
        }}>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
          <Typography variant="h6" sx={{fontWeight: 600}}>
            {metadata?.hasOnlySingleMoves ? 'Moves' : 'Move sets'}
          </Typography>
          {result != null && (
            <Typography variant="body2" color="text.secondary">
              ({totalCount} {tableHeaderStr})
            </Typography>
          )}
        </Box>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 2}}>
          {onPinToInitialFinalChange != null && (
            <Tooltip title="Always show moves between Initial and Final, regardless of assignment selection">
              <FormControlLabel
                control={
                  <Checkbox
                    size="small"
                    checked={pinToInitialFinal ?? false}
                    onChange={(_e, checked) =>
                      onPinToInitialFinalChange(checked)
                    }
                  />
                }
                label={
                  <Typography variant="body2" sx={{fontSize: '0.8125rem'}}>
                    Pin to Initial → Final
                  </Typography>
                }
              />
            </Tooltip>
          )}
          <FormControlLabel
            control={
              <Checkbox
                size="small"
                checked={isMinimized}
                onChange={(_e, checked) =>
                  onViewStateChange(prev => ({...prev, isMinimized: checked}))
                }
              />
            }
            label={
              <Typography variant="body2" sx={{fontSize: '0.8125rem'}}>
                Hide all {tableHeaderStr}
              </Typography>
            }
          />
        </Box>
      </Box>

      {/* Content (hidden when minimized) */}
      {!isMinimized && (
        <>
          {/* Selection toolbar */}
          {canSelectMoves && selectedMoves.size > 0 && (
            <Box
              sx={{
                display: 'flex',
                alignItems: 'center',
                gap: 1,
                px: 2,
                py: 1,
                backgroundColor: 'action.hover',
                borderBottom: 1,
                borderBottomColor: 'divider',
              }}>
              <Typography variant="body2" sx={{fontWeight: 600, mr: 1}}>
                {selectedMoves.size}{' '}
                {selectedMoves.size === 1 ? 'move' : 'moves'} selected
                {selectedOnPage < selectedMoves.size &&
                  ` (${selectedOnPage} on this page)`}
              </Typography>
              <Button
                size="small"
                variant="outlined"
                onClick={() =>
                  handleMoveAction('Destination Container', 'src')
                }>
                Add to A
              </Button>
              <Button
                size="small"
                variant="outlined"
                onClick={() =>
                  handleMoveAction('Destination Container', 'dst')
                }>
                Add to B
              </Button>
              <Button
                size="small"
                variant="outlined"
                onClick={() => handleMoveAction('Source Container', 'src')}>
                Revert in A
              </Button>
              <Button
                size="small"
                variant="outlined"
                onClick={() => handleMoveAction('Source Container', 'dst')}>
                Revert in B
              </Button>
              <Button size="small" onClick={() => setSelectedMoves(new Map())}>
                Clear
              </Button>
            </Box>
          )}

          {/* Selectors */}
          <Box
            sx={{
              display: 'flex',
              gap: 2,
              px: 2,
              py: 1,
              alignItems: 'center',
              flexWrap: 'wrap',
            }}>
            {/* Partition selector */}
            <Autocomplete
              size="small"
              sx={{minWidth: 240, flex: 1}}
              options={metadata?.partitionNames ?? []}
              value={partition}
              onChange={(_e, value) => handlePartitionChange(value)}
              renderInput={params => (
                <TextField
                  {...params}
                  label="Partition"
                  placeholder={`Display ${tableHeaderStr} by partition`}
                  inputProps={{
                    ...params.inputProps,
                    title: partition ?? '',
                  }}
                />
              )}
              slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
            />

            {/* Scope selector */}
            <Autocomplete
              size="small"
              sx={{minWidth: 240, flex: 1}}
              options={metadata?.scopeNames ?? []}
              value={scope}
              onChange={(_e, value) => handleScopeChange(value)}
              renderInput={params => (
                <TextField
                  {...params}
                  label="Scope"
                  placeholder={`Display ${tableHeaderStr} by scope`}
                  inputProps={{
                    ...params.inputProps,
                    title: scope ?? '',
                  }}
                />
              )}
              slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
            />

            {/* Objective multi-selector */}
            <Box
              sx={{
                display: 'flex',
                alignItems: 'center',
                gap: 0.5,
                flex: 1,
                minWidth: 300,
              }}>
              <Autocomplete
                multiple
                size="small"
                sx={{minWidth: 300, flex: 1}}
                options={metadata?.objectiveNames ?? []}
                value={objectives}
                disabled={!isObjSelectorEnabled}
                onChange={(_e, value) => handleObjectivesChange(value)}
                renderInput={params => (
                  <TextField
                    {...params}
                    label="Objectives"
                    placeholder={`Objective changes for ${tableHeaderStr}`}
                  />
                )}
                renderTags={(value, getTagProps) =>
                  value.map((option, index) => {
                    const {key, ...rest} = getTagProps({index});
                    return (
                      <Chip
                        key={key}
                        label={option}
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
                      '& .MuiAutocomplete-option': {
                        whiteSpace: 'normal',
                        wordBreak: 'break-word',
                      },
                    },
                  },
                }}
              />
              {!isObjSelectorEnabled && (
                <>
                  <Tooltip title="Why is this disabled?">
                    <IconButton
                      ref={warningIconRef}
                      size="small"
                      onClick={() => setWarningPopoverOpen(true)}>
                      <WarningAmber color="warning" fontSize="small" />
                    </IconButton>
                  </Tooltip>
                  <Popover
                    open={warningPopoverOpen}
                    anchorEl={warningIconRef.current}
                    onClose={() => setWarningPopoverOpen(false)}
                    anchorOrigin={{
                      vertical: 'bottom',
                      horizontal: 'left',
                    }}>
                    <Box sx={{p: 2, maxWidth: 400}}>
                      <Typography variant="body2">{warnMsg}</Typography>
                    </Box>
                  </Popover>
                </>
              )}
            </Box>
          </Box>

          {/* Filters */}
          {resultColumns.length > 0 && (
            <Box sx={{px: 2, py: 1}}>
              <EntityFilter
                columnDescriptions={resultColumns}
                filters={filters}
                onFiltersChange={handleFiltersChange}
                entityName={metadata?.variableName ?? 'object'}
                columnLabels={columnLabels}
                columnTypeaheadEntities={columnTypeaheadEntities}
              />
            </Box>
          )}

          {/* Loading state */}
          {loading && (
            <Box
              role="status"
              sx={{
                display: 'flex',
                justifyContent: 'center',
                alignItems: 'center',
                py: 4,
              }}>
              <CircularProgress size={40} />
              <Box sx={{ml: 2, color: 'text.secondary'}}>
                Loading move sets...
              </Box>
            </Box>
          )}

          {/* Error state */}
          {error != null && !loading && (
            <Box sx={{px: 2, py: 2}}>
              <Typography color="error" variant="body2">
                {error}
              </Typography>
            </Box>
          )}

          {/* Table */}
          {!loading && error == null && result != null && (
            <Box sx={{overflow: 'auto'}}>
              <table
                style={{
                  width: '100%',
                  minWidth: table.getTotalSize(),
                  tableLayout: 'fixed',
                  borderCollapse: 'separate',
                  borderSpacing: 0,
                }}>
                <thead>
                  {headerGroups.map(headerGroup => (
                    <tr key={headerGroup.id}>
                      {canSelectMoves && (
                        <Box
                          component="th"
                          sx={{
                            width: 36,
                            minWidth: 36,
                            maxWidth: 36,
                            padding: '4px 6px',
                            borderBottom: 1,
                            borderBottomColor: 'divider',
                            textAlign: 'center',
                          }}>
                          <Checkbox
                            size="small"
                            checked={
                              rows.length > 0 &&
                              rows.every((_, i) =>
                                selectedMoves.has(offset + i),
                              )
                            }
                            indeterminate={
                              selectedOnPage > 0 && selectedOnPage < rows.length
                            }
                            onChange={(_e, checked) => {
                              const next = new Map(selectedMoves);
                              if (checked && result != null) {
                                rows.forEach((_, i) => {
                                  const globalId = offset + i;
                                  if (!next.has(globalId)) {
                                    const moveData = extractMoveDataFromRow(
                                      result.rows[i],
                                      result.columns,
                                    );
                                    if (moveData != null) {
                                      next.set(globalId, moveData);
                                    }
                                  }
                                });
                              } else {
                                rows.forEach((_, i) => next.delete(offset + i));
                              }
                              setSelectedMoves(next);
                            }}
                            sx={{p: 0}}
                          />
                        </Box>
                      )}
                      {headerGroup.headers.map(header => {
                        const columnDef = header.column.columnDef;
                        const meta = columnDef.meta as ColumnMeta | undefined;
                        const isSorted = orderColumn === meta?.colName;
                        const isAsc =
                          isSorted &&
                          orderDirection === OrderDirection.ASCENDING;

                        return (
                          <Box
                            component="th"
                            key={header.id}
                            onClick={() => handleSort(header.column.id)}
                            sx={{
                              width: header.getSize(),
                              textAlign: meta?.numeric ? 'right' : 'left',
                              padding: '8px 12px',
                              borderBottom: 1,
                              borderBottomColor: 'divider',
                              fontWeight: 500,
                              fontSize: '0.875rem',
                              color: 'text.secondary',
                              cursor: 'pointer',
                              userSelect: 'none',
                            }}>
                            <Box
                              sx={{
                                display: 'flex',
                                alignItems: 'center',
                                justifyContent: meta?.numeric
                                  ? 'flex-end'
                                  : 'flex-start',
                                gap: 0.5,
                              }}>
                              {flexRender(
                                header.column.columnDef.header,
                                header.getContext(),
                              )}
                              {isSorted &&
                                (isAsc ? (
                                  <ArrowUpward sx={{fontSize: 16}} />
                                ) : (
                                  <ArrowDownward sx={{fontSize: 16}} />
                                ))}
                            </Box>
                          </Box>
                        );
                      })}
                    </tr>
                  ))}
                </thead>
                <tbody>
                  {rows.map((row, rowIndex) => {
                    const globalId = offset + rowIndex;
                    const isSelected = selectedMoves.has(globalId);
                    return (
                      <Box
                        component="tr"
                        key={row.id}
                        sx={{
                          backgroundColor: isSelected
                            ? 'action.selected'
                            : 'background.paper',
                          '&:hover': {
                            backgroundColor: isSelected
                              ? 'action.selected'
                              : 'action.hover',
                          },
                        }}>
                        {canSelectMoves && (
                          <Box
                            component="td"
                            sx={{
                              width: 36,
                              minWidth: 36,
                              maxWidth: 36,
                              padding: '4px 6px',
                              borderBottom: 1,
                              borderBottomColor: 'divider',
                              textAlign: 'center',
                            }}>
                            <Checkbox
                              size="small"
                              checked={isSelected}
                              onChange={(_e, checked) => {
                                const next = new Map(selectedMoves);
                                if (checked && result != null) {
                                  const moveData = extractMoveDataFromRow(
                                    result.rows[rowIndex],
                                    result.columns,
                                  );
                                  if (moveData != null) {
                                    next.set(globalId, moveData);
                                  }
                                } else {
                                  next.delete(globalId);
                                }
                                setSelectedMoves(next);
                              }}
                              sx={{p: 0}}
                            />
                          </Box>
                        )}
                        {row.getVisibleCells().map(cell => {
                          const columnDef = cell.column.columnDef;
                          const meta = columnDef.meta as ColumnMeta | undefined;
                          return (
                            <Box
                              component="td"
                              key={cell.id}
                              onClick={copyOnClick}
                              onKeyDown={copyOnKeyDown}
                              tabIndex={0}
                              sx={{
                                textAlign: meta?.numeric ? 'right' : 'left',
                                padding: '8px 12px',
                                borderBottom: 1,
                                borderBottomColor: 'divider',
                                fontSize: '0.875rem',
                                whiteSpace: 'normal',
                                wordBreak: 'break-word',
                                overflowWrap: 'anywhere',
                                cursor: 'pointer',
                              }}>
                              {flexRender(
                                cell.column.columnDef.cell,
                                cell.getContext(),
                              )}
                            </Box>
                          );
                        })}
                      </Box>
                    );
                  })}
                  {rows.length === 0 && (
                    <tr>
                      <td
                        colSpan={
                          (resultColumns.length || 1) + (canSelectMoves ? 1 : 0)
                        }
                        style={{
                          textAlign: 'center',
                          padding: '24px 12px',
                          fontSize: '0.875rem',
                        }}>
                        <Typography
                          component="span"
                          variant="body2"
                          color="text.secondary">
                          No {tableHeaderStr} found
                        </Typography>
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </Box>
          )}

          {/* Pagination */}
          {result != null && !loading && (
            <TablePagination
              component="div"
              count={totalCount}
              page={currentPage}
              onPageChange={handlePageChange}
              rowsPerPage={pageSize}
              onRowsPerPageChange={handleRowsPerPageChange}
              rowsPerPageOptions={PAGE_SIZE_OPTIONS}
              ActionsComponent={TablePaginationActions}
              sx={{
                borderTop: 1,
                borderTopColor: 'divider',
                flexShrink: 0,
                '& .MuiTablePagination-spacer': {display: 'none'},
                '& .MuiTablePagination-toolbar': {
                  justifyContent: 'flex-start',
                },
              }}
            />
          )}
        </>
      )}
      {snackbar}
    </Paper>
  );
}
