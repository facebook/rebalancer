'use client';

import type {Dispatch, SetStateAction} from 'react';
import {useCallback, useMemo} from 'react';

import ArrowDownward from '@mui/icons-material/ArrowDownward';
import ArrowUpward from '@mui/icons-material/ArrowUpward';
import HelpOutline from '@mui/icons-material/HelpOutline';
import {
  Alert,
  Autocomplete,
  Box,
  Chip,
  CircularProgress,
  IconButton,
  Paper,
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

import {
  ColumnPinButton,
  getPinnedColumnHelpers,
  PINNED_SHADOW,
  useColumnPinning,
} from '@/app/components/columnPinning';
import {
  ColumnHideButton,
  getColumnHiding,
} from '@/app/components/columnVisibility';

import {metricsFaqUrl} from '@platform/internal-links';

import EntityFilter from '@/app/components/EntityFilter';
import GroupBySelector from '@/app/components/GroupBySelector';
import ShowColumnsSelector from '@/app/components/ShowColumnsSelector';
import useCopyOnClick from '@/app/components/useCopyOnClick';
import type {Assignments} from '@/app/components/evaluation/AssignmentCard';
import {
  B_MINUS_A_SUFFIX,
  getColumnWidth,
  isVisibleColumn,
  type MetricsViewState,
} from '@/app/components/evaluation/MetricsTable.state';
import {
  extractCellValue,
  formatCellValue,
  NAN_STRING,
} from '@/app/components/evaluation/MoveSetsTable.cells';
import CopyCellAffordance from '@/app/components/CopyCellAffordance';
import UpdatingOverlay from '@/app/components/UpdatingOverlay';
import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import {useMetricsData} from '@/app/components/evaluation/useMetricsData';
import {
  AUTOCOMPLETE_WORD_BREAK_PROPS,
  isNumericColumn,
  toTitleCase,
} from '@/lib/format';
import type {CellData, Filter} from '@/lib/rebalancer-explorer-types';
import {OrderDirection} from '@/lib/rebalancer-explorer-types';

const PAGE_SIZE_OPTIONS = [10, 25, 50, 100];

type RowData = Record<string, CellData | undefined>;

const columnHelper = createColumnHelper<RowData>();

interface MetricsTableProps {
  metricName: string;
  viewState: MetricsViewState;
  onViewStateChange: Dispatch<SetStateAction<MetricsViewState>>;
  assignments: Assignments;
  metricCollectionNames: string[];
  onMetricNameChange: (
    event: React.SyntheticEvent,
    value: string | null,
  ) => void;
}

export default function MetricsTable({
  metricName,
  viewState,
  onViewStateChange,
  assignments,
  metricCollectionNames,
  onMetricNameChange,
}: MetricsTableProps) {
  const {copyOnClick, snackbar} = useCopyOnClick();

  const {offset, pageSize, orderBy, filters, groupBy, showColumns} = viewState;

  const {
    result,
    originalResult,
    loading,
    error,
    setError,
    isValid,
    columnTypeaheadEntities,
  } = useMetricsData({metricName, viewState, assignments});

  // ---- Columns ----
  const allColumns = useMemo(() => result?.columns ?? [], [result]);

  const visibleColumnIndices = useMemo(
    () =>
      allColumns
        .map((col, i) => ({col, i}))
        .filter(({col}) => isVisibleColumn(col, groupBy, showColumns))
        .map(({i}) => i),
    [allColumns, groupBy, showColumns],
  );

  const columns = useMemo(() => {
    return visibleColumnIndices.map(colIndex => {
      const col = allColumns[colIndex];
      const isBMinusA = col.name.endsWith(B_MINUS_A_SUFFIX);
      const isNumeric = isNumericColumn(col.type);
      return columnHelper.accessor(String(colIndex), {
        id: String(colIndex),
        header: col.name,
        cell: info => {
          const value = extractCellValue(info.getValue(), col);
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
          if (isBMinusA && typeof value === 'number') {
            return (
              <PreciseNumber
                value={value}
                highlight={true}
                positiveIsBad={true}
                fixedDecimals={Number.isInteger(value) ? 0 : 4}
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
        size: getColumnWidth(col),
        meta: {
          numeric: isNumeric,
          colName: col.name,
          primaryKey: col.primaryKey,
        },
      });
    });
  }, [visibleColumnIndices, allColumns]);

  // ---- Rows ----
  const data = useMemo<RowData[]>(() => {
    if (result == null) return [];
    return result.rows.map(row => {
      const rowObj: RowData = {};
      allColumns.forEach((_col, colIndex) => {
        rowObj[String(colIndex)] = row.cells[colIndex];
      });
      return rowObj;
    });
  }, [result, allColumns]);

  // The identifier columns start pinned to the left.
  const defaultPinnedIds = useMemo(
    () =>
      visibleColumnIndices
        .filter(i => allColumns[i]?.primaryKey)
        .map(i => String(i)),
    [visibleColumnIndices, allColumns],
  );
  const {columnPinning, setColumnPinning, togglePin} =
    useColumnPinning(defaultPinnedIds);

  const table = useReactTable({
    data,
    columns,
    state: {columnPinning},
    onColumnPinningChange: setColumnPinning,
    getCoreRowModel: getCoreRowModel(),
    manualSorting: true,
    manualPagination: true,
    columnResizeMode: 'onChange',
  });

  // ---- Handlers ----
  const handleSort = useCallback(
    (colIndex: string) => {
      const col = allColumns[parseInt(colIndex, 10)];
      if (col == null) return;
      let nextOrderBy: MetricsViewState['orderBy'];
      if (orderBy?.column === col.name) {
        nextOrderBy =
          orderBy.direction === OrderDirection.ASCENDING
            ? {column: col.name, direction: OrderDirection.DESCENDING}
            : null;
      } else {
        nextOrderBy = {column: col.name, direction: OrderDirection.ASCENDING};
      }
      onViewStateChange({...viewState, orderBy: nextOrderBy, offset: 0});
    },
    [allColumns, orderBy, viewState, onViewStateChange],
  );

  const currentPage = pageSize > 0 ? Math.floor(offset / pageSize) : 0;

  const handlePageChange = useCallback(
    (_event: unknown, newPage: number) => {
      onViewStateChange({...viewState, offset: newPage * pageSize});
    },
    [viewState, pageSize, onViewStateChange],
  );

  const handleRowsPerPageChange = useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      const newSize = parseInt(event.target.value, 10);
      onViewStateChange({...viewState, pageSize: newSize, offset: 0});
    },
    [viewState, onViewStateChange],
  );

  const handleFiltersChange = useCallback(
    (newFilters: Filter[]) => {
      onViewStateChange({...viewState, filters: newFilters, offset: 0});
    },
    [viewState, onViewStateChange],
  );

  const handleGroupByChange = useCallback(
    (newGroupBy: string[]) => {
      onViewStateChange({
        ...viewState,
        groupBy: newGroupBy,
        offset: 0,
        orderBy:
          newGroupBy.length > 0
            ? {column: newGroupBy[0], direction: OrderDirection.ASCENDING}
            : null,
      });
    },
    [viewState, onViewStateChange],
  );

  const handleShowColumnsChange = useCallback(
    (newShowColumns: string[]) => {
      onViewStateChange({...viewState, showColumns: newShowColumns, offset: 0});
    },
    [viewState, onViewStateChange],
  );

  // ---- Render ----
  if (!isValid) {
    return null;
  }

  const totalCount = result?.totalCount ?? 0;
  const filterColumns = originalResult?.columns ?? [];
  const headerGroups = table.getHeaderGroups();
  const rows = table.getRowModel().rows;

  const pinnedLeft = columnPinning.left ?? [];
  const {stickyStyle, isLastPinned} = getPinnedColumnHelpers(table, pinnedLeft);
  const hiding = getColumnHiding(allColumns, groupBy, showColumns);

  return (
    <Paper variant="outlined" sx={{position: 'relative', overflow: 'hidden'}}>
      {/* Header */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 1,
          px: 2,
          py: 1,
          borderBottom: 1,
          borderBottomColor: 'divider',
        }}>
        <Typography variant="h6" sx={{fontWeight: 600}}>
          Metrics
        </Typography>
        {(() => {
          const faqUrl = metricsFaqUrl();
          return faqUrl == null ? null : (
            <Tooltip title="Click to learn more about Metrics">
              <IconButton
                component="a"
                href={faqUrl}
                target="_blank"
                rel="noopener noreferrer"
                size="small"
                aria-label="Click to learn more about Metrics">
                <HelpOutline fontSize="small" />
              </IconButton>
            </Tooltip>
          );
        })()}
        {result != null && (
          <Typography variant="body2" color="text.secondary">
            ({totalCount} rows)
          </Typography>
        )}
      </Box>

      {/* Metric collection picker */}
      <Box sx={{px: 2, py: 2, borderBottom: 1, borderBottomColor: 'divider'}}>
        <Autocomplete
          options={metricCollectionNames}
          value={metricCollectionNames.includes(metricName) ? metricName : null}
          onChange={onMetricNameChange}
          getOptionLabel={toTitleCase}
          isOptionEqualToValue={(option, value) => option === value}
          renderInput={inputParams => (
            <TextField
              {...inputParams}
              label="Metric Collection"
              placeholder="Select a metric collection"
            />
          )}
          fullWidth
          sx={{maxWidth: 520}}
          slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
        />
      </Box>

      {/* Filters */}
      {filterColumns.length > 0 && (
        <Box sx={{px: 2, py: 1}}>
          <EntityFilter
            columnDescriptions={filterColumns}
            filters={filters}
            onFiltersChange={handleFiltersChange}
            entityName={metricName}
            columnTypeaheadEntities={columnTypeaheadEntities}
          />
        </Box>
      )}

      {/* Group-by and columns-to-show pickers, on one row */}
      {filterColumns.length > 0 && (
        <Box
          sx={{
            px: 2,
            py: 1,
            display: 'flex',
            flexWrap: 'wrap',
            gap: 2,
            alignItems: 'flex-start',
            justifyContent: 'space-between',
          }}>
          <GroupBySelector
            columnDescriptions={filterColumns}
            groupByColumns={groupBy}
            onGroupByChange={handleGroupByChange}
          />
          <ShowColumnsSelector
            columnDescriptions={filterColumns}
            groupByColumns={groupBy}
            showColumns={showColumns}
            onShowColumnsChange={handleShowColumnsChange}
          />
        </Box>
      )}

      {/* Error */}
      {error != null && (
        <Box sx={{px: 2, py: 1}}>
          <Alert severity="error" onClose={() => setError(null)}>
            {error}
          </Alert>
        </Box>
      )}

      {/* Initial loading (no data yet) */}
      {result == null && loading && (
        <Box
          role="status"
          sx={{
            display: 'flex',
            justifyContent: 'center',
            alignItems: 'center',
            py: 4,
          }}>
          <CircularProgress size={40} />
          <Box sx={{ml: 2, color: 'text.secondary'}}>Loading metrics...</Box>
        </Box>
      )}

      {/* Refetch overlay, anchored to the Paper so the badge stays in view. */}
      {result != null && loading && <UpdatingOverlay />}

      {/* Table */}
      {result != null && (
        <Box sx={{overflow: 'auto'}}>
          <table
            style={{
              width: table.getTotalSize(),
              minWidth: '100%',
              tableLayout: 'fixed',
              borderCollapse: 'separate',
              borderSpacing: 0,
            }}>
            <thead>
              {headerGroups.map(headerGroup => (
                <tr key={headerGroup.id}>
                  {headerGroup.headers.map(header => {
                    const meta = header.column.columnDef.meta as
                      | {
                          numeric?: boolean;
                          colName?: string;
                          primaryKey?: boolean;
                        }
                      | undefined;
                    const isSorted = orderBy?.column === meta?.colName;
                    const isAsc =
                      isSorted &&
                      orderBy?.direction === OrderDirection.ASCENDING;
                    const isPinned = pinnedLeft.includes(header.column.id);
                    return (
                      <Box
                        component="th"
                        key={header.id}
                        onClick={() => handleSort(header.column.id)}
                        sx={{
                          position: 'relative',
                          ...stickyStyle(header.column.id, 3),
                          width: header.getSize(),
                          textAlign: meta?.numeric ? 'right' : 'left',
                          padding: '8px 12px',
                          borderBottom: 1,
                          borderBottomColor: 'divider',
                          fontWeight: meta?.primaryKey ? 600 : 500,
                          fontSize: '0.875rem',
                          color: 'text.secondary',
                          backgroundColor: 'background.paper',
                          cursor: 'pointer',
                          userSelect: 'none',
                          boxShadow: isLastPinned(header.column.id)
                            ? PINNED_SHADOW
                            : undefined,
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
                          <ColumnPinButton
                            pinned={isPinned}
                            onToggle={() => togglePin(header.column.id)}
                            label={
                              isPinned
                                ? `Unpin ${meta?.colName}`
                                : `Pin ${meta?.colName}`
                            }
                          />
                          {meta?.colName != null &&
                            hiding.isHideable(meta.colName) && (
                              <ColumnHideButton
                                colName={meta.colName}
                                pinned={isPinned}
                                hideDisabled={hiding.hideDisabled}
                                onHide={() =>
                                  onViewStateChange(prev => ({
                                    ...prev,
                                    showColumns: hiding.hideColumn(
                                      meta.colName!,
                                      prev.showColumns,
                                    ),
                                    offset: 0,
                                  }))
                                }
                              />
                            )}
                        </Box>
                        {header.column.getCanResize() && (
                          <Box
                            component="div"
                            onMouseDown={header.getResizeHandler()}
                            onTouchStart={header.getResizeHandler()}
                            onClick={e => e.stopPropagation()}
                            sx={{
                              position: 'absolute',
                              right: 0,
                              top: 0,
                              height: '100%',
                              width: '4px',
                              cursor: 'col-resize',
                              userSelect: 'none',
                              touchAction: 'none',
                              bgcolor: header.column.getIsResizing()
                                ? 'primary.main'
                                : 'transparent',
                              '&:hover': {bgcolor: 'action.disabled'},
                            }}
                          />
                        )}
                      </Box>
                    );
                  })}
                </tr>
              ))}
            </thead>
            <tbody>
              {rows.map(row => (
                <Box
                  component="tr"
                  key={row.id}
                  sx={{
                    backgroundColor: 'background.paper',
                    '&:hover': {backgroundColor: 'action.hover'},
                  }}>
                  {row.getVisibleCells().map(cell => {
                    const meta = cell.column.columnDef.meta as
                      {numeric?: boolean} | undefined;
                    const isPinned = pinnedLeft.includes(cell.column.id);
                    return (
                      <Box
                        component="td"
                        key={cell.id}
                        className="group"
                        onClick={copyOnClick}
                        sx={{
                          position: 'relative',
                          ...stickyStyle(cell.column.id, 2),
                          width: cell.column.getSize(),
                          textAlign: meta?.numeric ? 'right' : 'left',
                          py: '8px',
                          pl: '12px',
                          // Extra right room for the copy icon.
                          pr: '24px',
                          borderBottom: 1,
                          borderBottomColor: 'divider',
                          backgroundColor: isPinned ? 'inherit' : undefined,
                          fontSize: '0.875rem',
                          whiteSpace: 'normal',
                          wordBreak: 'break-word',
                          overflowWrap: 'anywhere',
                          cursor: 'pointer',
                          boxShadow: isLastPinned(cell.column.id)
                            ? PINNED_SHADOW
                            : undefined,
                        }}>
                        {flexRender(
                          cell.column.columnDef.cell,
                          cell.getContext(),
                        )}
                        <CopyCellAffordance />
                      </Box>
                    );
                  })}
                </Box>
              ))}
              {rows.length === 0 && !loading && (
                <tr>
                  <td
                    colSpan={visibleColumnIndices.length || 1}
                    style={{
                      textAlign: 'center',
                      padding: '24px 12px',
                      fontSize: '0.875rem',
                    }}>
                    <Typography
                      component="span"
                      variant="body2"
                      color="text.secondary">
                      No data
                    </Typography>
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </Box>
      )}

      {/* Pagination */}
      {result != null && (
        <TablePagination
          component="div"
          count={totalCount}
          page={currentPage}
          onPageChange={handlePageChange}
          rowsPerPage={pageSize}
          onRowsPerPageChange={handleRowsPerPageChange}
          rowsPerPageOptions={PAGE_SIZE_OPTIONS}
          sx={{
            borderTop: 1,
            borderTopColor: 'divider',
            flexShrink: 0,
            '& .MuiTablePagination-spacer': {display: 'none'},
            '& .MuiTablePagination-toolbar': {justifyContent: 'flex-start'},
          }}
        />
      )}
      {snackbar}
    </Paper>
  );
}
