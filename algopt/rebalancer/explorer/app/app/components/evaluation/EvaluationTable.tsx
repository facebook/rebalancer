'use client';

import {memo, useCallback, useMemo, useState} from 'react';

import ArrowDownward from '@mui/icons-material/ArrowDownward';
import ArrowUpward from '@mui/icons-material/ArrowUpward';
import InfoOutlined from '@mui/icons-material/InfoOutlined';
import {
  Box,
  Checkbox,
  FormControlLabel,
  IconButton,
  Tooltip,
  Typography,
} from '@mui/material';
import {
  createColumnHelper,
  flexRender,
  getCoreRowModel,
  getSortedRowModel,
  useReactTable,
} from '@tanstack/react-table';
import type {Column, SortingState} from '@tanstack/react-table';

import CollapsibleText from '@/app/components/evaluation/CollapsibleText';
import {
  compileEvaluationFilters,
  matchesEvaluationFilters,
} from '@/app/components/evaluation/evaluation-filters';
import {isRowChanged} from '@/app/components/evaluation/evaluation-penalty';
import CopyCellAffordance from '@/app/components/CopyCellAffordance';
import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import SpecViewerButton from '@/app/components/evaluation/SpecViewerButton';
import TreeViewer from '@/app/components/evaluation/TreeViewer';
import useCopyOnClick from '@/app/components/useCopyOnClick';
import type {
  Assignment,
  ExpressionResult,
  Filter,
  Handle,
} from '@/lib/rebalancer-explorer-types';
import {
  LINE_COLOR,
  MUTED_TEXT_COLOR,
  ROW_HOVER_COLOR,
  TOTAL_LINE_COLOR,
} from '@/lib/ui-tokens';

const totalCellStyle = {
  padding: '10px 16px',
  borderTop: `2px solid ${TOTAL_LINE_COLOR}`,
};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type HideAllUnchangedControl = {
  hideAllUnchanged: boolean;
  setHideAllUnchanged: (v: boolean) => void;
  uncheckedByTable: number | null;
  setUncheckedByTable: (v: number | null) => void;
  tableIndex: number;
};

type EvaluationTableProps = {
  title: string;
  srcExpressions: ExpressionResult[];
  dstExpressions: ExpressionResult[];
  expressionType: 'CONSTRAINT' | 'OBJECTIVE';
  hideAllUnchangedControl: HideAllUnchangedControl;
  handle: Handle;
  sourceAssignment: Assignment;
  destinationAssignment: Assignment;
  evaluationFilters?: Filter[];
  showPenalty: boolean;
  onShowPenaltyChange: (show: boolean) => void;
};

/** Merged row joining src and dst expression data. */
interface EvaluationRow {
  expressionId: number;
  name: string;
  description: string;
  srcValue: number;
  dstValue: number;
  delta: number;
  percentage: number;
  // Separated local-search penalty; null when the objective has none.
  srcPenalty: number | null;
  dstPenalty: number | null;
  penaltyDelta: number;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Compute percentage change: (B - A) / |A| * 100.
 * Returns 0 when both are zero, 100 when only A is zero.
 */
function calculatePercentageChange(src: number, dst: number): number {
  const delta = dst - src;
  if (src === 0) {
    return dst === 0 ? 0 : 100;
  }
  return (delta / Math.abs(src)) * 100;
}

// Penalty value, or a muted "-" when the objective has no penalty.
function PenaltyValue({value}: {value: number | null}) {
  if (value == null) {
    return (
      <Box component="span" sx={{color: 'text.secondary'}}>
        -
      </Box>
    );
  }
  return <PreciseNumber value={value} highlight={false} />;
}

// Small muted "(%)" shown beneath the B - A delta. Parenthesized to match the
// "(%)" sort label in the header. data-copy-skip keeps it out of the cell's
// click-to-copy text, so copying a B - A cell yields just the value.
function PercentBeneath({value}: {value: number}) {
  return (
    <Box
      component="span"
      data-copy-skip
      sx={{
        fontSize: '0.6rem',
        lineHeight: 1,
        color: 'text.disabled',
        fontVariantNumeric: 'tabular-nums',
      }}>
      {`(${value.toFixed(2)}%)`}
    </Box>
  );
}

// A clickable sort target with its own direction arrow, used for the two labels
// ("B - A" and the smaller, muted "(%)") stacked in the merged B - A header.
function SortLabel({
  label,
  column,
  secondary = false,
}: {
  label: string;
  column: Column<EvaluationRow> | undefined;
  secondary?: boolean;
}) {
  const sorted = column?.getIsSorted();
  const arrowSize = secondary ? 12 : 14;
  return (
    <Box
      component="button"
      type="button"
      aria-label={`Sort by ${label}`}
      onClick={e => {
        e.stopPropagation();
        column?.toggleSorting();
      }}
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 0.25,
        cursor: 'pointer',
        border: 0,
        p: 0,
        bgcolor: 'transparent',
        font: 'inherit',
        color: 'inherit',
        ...(secondary && {color: 'text.secondary', fontSize: '0.65rem'}),
      }}>
      {label}
      {sorted === 'asc' && <ArrowUpward sx={{fontSize: arrowSize}} />}
      {sorted === 'desc' && <ArrowDownward sx={{fontSize: arrowSize}} />}
    </Box>
  );
}

// Fixed width for the A / B / B-A number columns, so they line up and stay
// compact; longer values wrap. `ch` is the width of one digit (tabular-nums
// keeps digits equal), plus `3rem` padding.
const NUMBER_COL_WIDTH = 'calc(17ch + 3rem)';
// With the three penalty columns shown, all number columns narrow and
// Description gets a fixed width, so the table scrolls horizontally on a narrow
// screen instead of squeezing Description to nothing.
const NUMBER_COL_WITH_PENALTY_WIDTH = 'calc(12ch + 3rem)';
const DESCRIPTION_WITH_PENALTY_WIDTH = 200;

// Name sizes to content (clamped) so short names free width for the auto-width
// Description column.
const MIN_NAME_CH = 10;
const MAX_NAME_CH = 38;

// Stacks the B - A value over its smaller "(%)" line, right-aligned. Used in the
// header, the body cell, and the totals footer so all three line up.
const STACKED_BA_SX = {
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'flex-end',
  gap: '2px',
} as const;

// ---------------------------------------------------------------------------
// Column definitions
// ---------------------------------------------------------------------------

const columnHelper = createColumnHelper<EvaluationRow>();

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

const EvaluationTable = memo(function EvaluationTable({
  title,
  srcExpressions,
  dstExpressions,
  expressionType,
  hideAllUnchangedControl,
  handle,
  sourceAssignment,
  destinationAssignment,
  evaluationFilters,
  showPenalty,
  onShowPenaltyChange,
}: EvaluationTableProps) {
  const {
    hideAllUnchanged,
    setHideAllUnchanged,
    uncheckedByTable,
    setUncheckedByTable,
    tableIndex,
  } = hideAllUnchangedControl;

  const [hideUnchanged, setHideUnchanged] = useState(hideAllUnchanged);
  const {copyOnClick, copyOnKeyDown, snackbar} = useCopyOnClick();

  const handleTableHideUnchangedChange = useCallback(
    (newState: boolean) => {
      setHideUnchanged(newState);
      if (!newState) {
        setHideAllUnchanged(false);
        setUncheckedByTable(tableIndex);
      }
    },
    [setHideAllUnchanged, setUncheckedByTable, tableIndex],
  );

  // Sync local state with global "hide all" control whenever the parent
  // values change:
  // 1. hideAll checked → check this table
  // 2. hideAll unchecked directly (uncheckedByTable === null) → uncheck
  // 3. hideAll unchecked by THIS table → uncheck
  // 4. hideAll unchecked by ANOTHER table → leave unchanged
  const [prevHideAllUnchanged, setPrevHideAllUnchanged] =
    useState(hideAllUnchanged);
  const [prevUncheckedByTable, setPrevUncheckedByTable] =
    useState(uncheckedByTable);
  if (
    hideAllUnchanged !== prevHideAllUnchanged ||
    uncheckedByTable !== prevUncheckedByTable
  ) {
    setPrevHideAllUnchanged(hideAllUnchanged);
    setPrevUncheckedByTable(uncheckedByTable);
    if (hideAllUnchanged) {
      setHideUnchanged(true);
    } else if (uncheckedByTable === null || uncheckedByTable === tableIndex) {
      setHideUnchanged(false);
    }
  }

  const isConstraint = expressionType === 'CONSTRAINT';

  // Join src and dst by expression id
  const allRows = useMemo<EvaluationRow[]>(() => {
    const dstMap = new Map<number, ExpressionResult>();
    for (const expr of dstExpressions) {
      dstMap.set(expr.id, expr);
    }

    return srcExpressions
      .map(src => {
        const dst = dstMap.get(src.id);
        if (dst == null) {
          return null;
        }
        const delta = dst.value - src.value;
        const srcPenalty = src.penaltyValue ?? null;
        const dstPenalty = dst.penaltyValue ?? null;
        return {
          expressionId: src.id,
          name: src.name,
          description: src.description,
          srcValue: src.value,
          dstValue: dst.value,
          delta,
          percentage: calculatePercentageChange(src.value, dst.value),
          srcPenalty,
          dstPenalty,
          penaltyDelta: (dstPenalty ?? 0) - (srcPenalty ?? 0),
        };
      })
      .filter((row): row is EvaluationRow => row != null);
  }, [srcExpressions, dstExpressions]);

  const hasPenalty = useMemo(
    () => allRows.some(row => row.srcPenalty != null || row.dstPenalty != null),
    [allRows],
  );
  const isPenaltyShown = hasPenalty && showPenalty;

  // Rows matching the user-selected evaluation filters.
  const filteredRows = useMemo(() => {
    if (evaluationFilters == null || evaluationFilters.length === 0) {
      return allRows;
    }
    const compiled = compileEvaluationFilters(evaluationFilters);
    return allRows.filter(row => matchesEvaluationFilters(row, compiled));
  }, [allRows, evaluationFilters]);

  // Rows actually rendered in the table body: the "hide unchanged" toggle
  // layered on top of the filtered rows.
  const displayRows = useMemo(
    () =>
      hideUnchanged
        ? filteredRows.filter(row => isRowChanged(row, isPenaltyShown))
        : filteredRows,
    [filteredRows, hideUnchanged, isPenaltyShown],
  );

  // Based on allRows, not the visible rows, so the width holds steady as
  // filters and "hide unchanged" change what's shown.
  const nameColWidth = useMemo(() => {
    const longest = allRows.reduce(
      (max, row) => Math.max(max, row.name.length),
      0,
    );
    const ch = Math.min(Math.max(longest, MIN_NAME_CH), MAX_NAME_CH);
    return `calc(${ch}ch + 3rem)`;
  }, [allRows]);

  // Footer totals sum exactly the rows rendered in the table body, so they
  // always match what is on screen — both the evaluation filter and the
  // "hide unchanged" toggle are reflected.
  const totals = useMemo(() => {
    let srcTotal = 0;
    let dstTotal = 0;
    let srcPenaltyTotal = 0;
    let dstPenaltyTotal = 0;
    for (const row of displayRows) {
      srcTotal += row.srcValue;
      dstTotal += row.dstValue;
      srcPenaltyTotal += row.srcPenalty ?? 0;
      dstPenaltyTotal += row.dstPenalty ?? 0;
    }
    const deltaTotal = dstTotal - srcTotal;
    const percentTotal = calculatePercentageChange(srcTotal, dstTotal);
    return {
      srcTotal,
      dstTotal,
      deltaTotal,
      percentTotal,
      srcPenaltyTotal,
      dstPenaltyTotal,
      penaltyDeltaTotal: dstPenaltyTotal - srcPenaltyTotal,
    };
  }, [displayRows]);

  // Sorting state
  const [sorting, setSorting] = useState<SortingState>([]);

  // Column definitions
  const columns = useMemo(
    () => [
      columnHelper.accessor('name', {
        header: 'Name',
        cell: info => (
          <Typography variant="body2" sx={{fontSize: '0.8125rem'}}>
            {info.getValue()}
          </Typography>
        ),
        enableSorting: false,
        meta: {
          tooltip:
            'Unique name of a goal or constraint. It can be customized with the parameter "name" when constructing a spec.',
        },
      }),
      columnHelper.accessor('description', {
        header: 'Description',
        cell: info => <CollapsibleText text={info.getValue()} />,
        enableSorting: false,
        size: 280,
        meta: {
          flex: true,
          tooltip: 'Auto-generated description of the goal or constraint.',
        },
      }),
      columnHelper.accessor('srcValue', {
        header: 'A',
        cell: info => (
          <PreciseNumber value={info.getValue()} highlight={isConstraint} />
        ),
        meta: {
          numeric: true,
          tooltip: 'Result of evaluating the expression with assignment A.',
        },
      }),
      columnHelper.accessor('dstValue', {
        header: 'B',
        cell: info => (
          <PreciseNumber value={info.getValue()} highlight={isConstraint} />
        ),
        meta: {
          numeric: true,
          tooltip: 'Result of evaluating the expression with assignment B.',
        },
      }),
      columnHelper.accessor('delta', {
        // Two independent sort targets stacked in one header: "B - A" sorts by
        // the delta, "(%)" sorts by the hidden percentage column.
        header: ({table: t}) => (
          <Box sx={STACKED_BA_SX}>
            <SortLabel label="B - A" column={t.getColumn('delta')} />
            <SortLabel
              label="(%)"
              column={t.getColumn('percentage')}
              secondary
            />
          </Box>
        ),
        cell: info => {
          const {delta, percentage} = info.row.original;
          return (
            <Box sx={STACKED_BA_SX}>
              <PreciseNumber value={delta} highlight={!isConstraint} />
              <PercentBeneath value={percentage} />
            </Box>
          );
        },
        meta: {
          numeric: true,
          dualSort: true,
          headerLabel: 'B - A',
          tooltip:
            'B - A is the difference between the two assignments, with the percentage change beneath. Click "B - A" or "(%)" in the header to sort by either.',
        },
      }),
      // Hidden (see columnVisibility): exists only as the sort target for the
      // "(%)" control in the B - A header. Its value is shown by PercentBeneath.
      columnHelper.accessor('percentage', {header: '%'}),
      // Penalty columns sort a missing penalty as 0; hidden unless the table
      // has penalties and "Show penalty" is checked (see columnVisibility).
      columnHelper.accessor(row => row.srcPenalty ?? 0, {
        id: 'srcPenalty',
        header: 'Penalty A',
        cell: info => <PenaltyValue value={info.row.original.srcPenalty} />,
        meta: {
          numeric: true,
          tooltip:
            'Separated local-search penalty with assignment A. Local search compares it only after the objective value, so it can only break ties between moves that leave the objective unchanged.',
        },
      }),
      columnHelper.accessor(row => row.dstPenalty ?? 0, {
        id: 'dstPenalty',
        header: 'Penalty B',
        cell: info => <PenaltyValue value={info.row.original.dstPenalty} />,
        meta: {
          numeric: true,
          tooltip: 'Separated local-search penalty with assignment B.',
        },
      }),
      columnHelper.accessor('penaltyDelta', {
        header: 'Penalty B - A',
        cell: info => {
          const {srcPenalty, dstPenalty, penaltyDelta} = info.row.original;
          return srcPenalty == null && dstPenalty == null ? (
            <PenaltyValue value={null} />
          ) : (
            <PreciseNumber value={penaltyDelta} highlight />
          );
        },
        meta: {
          numeric: true,
          tooltip: 'Difference of the separated local-search penalty.',
        },
      }),
      columnHelper.display({
        id: 'actions',
        header: ' ',
        cell: info => {
          const row = info.row.original;
          return (
            <Box sx={{display: 'flex', gap: 0.5}}>
              <SpecViewerButton
                name={row.name}
                expressionType={expressionType}
                handle={handle}
              />
              <TreeViewer
                expressionId={row.expressionId}
                handle={handle}
                sourceAssignment={sourceAssignment}
                destinationAssignment={destinationAssignment}
              />
            </Box>
          );
        },
        size: 90,
        enableSorting: false,
      }),
    ],
    [
      isConstraint,
      expressionType,
      handle,
      sourceAssignment,
      destinationAssignment,
    ],
  );

  const columnVisibility = useMemo(
    () => ({
      // The percentage column is merged into the B - A cell; keep it in the
      // model (so it stays sortable) but hidden so it renders no column of its
      // own.
      percentage: false,
      srcPenalty: isPenaltyShown,
      dstPenalty: isPenaltyShown,
      penaltyDelta: isPenaltyShown,
    }),
    [isPenaltyShown],
  );

  const table = useReactTable({
    data: displayRows,
    columns,
    state: {sorting, columnVisibility},
    onSortingChange: setSorting,
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
  });

  const headerGroups = table.getHeaderGroups();
  const rows = table.getRowModel().rows;

  return (
    <Box>
      {/* Title bar with per-table hide unchanged */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          px: 3,
          py: 1.5,
          borderBottom: `1px solid ${LINE_COLOR}`,
        }}>
        <Typography
          variant="subtitle1"
          sx={{fontWeight: 600, fontSize: '1.125rem'}}>
          {title}
        </Typography>
        <Box sx={{display: 'flex', alignItems: 'center', columnGap: 2}}>
          {hasPenalty && (
            <Tooltip title="Show the penalty of each objective in this table">
              <FormControlLabel
                sx={{mr: 0}}
                control={
                  <Checkbox
                    size="small"
                    checked={showPenalty}
                    onChange={(_e, checked) => onShowPenaltyChange(checked)}
                  />
                }
                label={
                  <Typography variant="body2" sx={{fontSize: '0.8125rem'}}>
                    Show penalty
                  </Typography>
                }
              />
            </Tooltip>
          )}
          <Tooltip
            title={
              isPenaltyShown
                ? 'Hide rows where the two assignments have the same value and the same penalty'
                : 'Hide rows where the two assignments have the same value'
            }>
            <FormControlLabel
              sx={{mr: 0}}
              control={
                <Checkbox
                  size="small"
                  checked={hideUnchanged}
                  onChange={(_e, checked) =>
                    handleTableHideUnchangedChange(checked)
                  }
                />
              }
              label={
                <Typography variant="body2" sx={{fontSize: '0.8125rem'}}>
                  {`Hide unchanged ${title}`}
                </Typography>
              }
            />
          </Tooltip>
        </Box>
      </Box>

      {/* Table */}
      <Box sx={{overflow: 'auto'}}>
        <table
          style={{
            width: '100%',
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
                        flex?: boolean;
                        tooltip?: string;
                        dualSort?: boolean;
                        headerLabel?: string;
                      }
                    | undefined;
                  // The dual-sort column (B - A / %) handles its own clicks and
                  // arrows inside its header, so skip the generic ones here.
                  const isDualSort = meta?.dualSort === true;
                  const canSort = header.column.getCanSort() && !isDualSort;
                  const sorted = header.column.getIsSorted();
                  const headerLabel =
                    meta?.headerLabel ??
                    (typeof header.column.columnDef.header === 'string'
                      ? header.column.columnDef.header
                      : header.column.id);

                  return (
                    <th
                      key={header.id}
                      style={{
                        width: meta?.flex
                          ? isPenaltyShown
                            ? DESCRIPTION_WITH_PENALTY_WIDTH
                            : 'auto'
                          : meta?.numeric
                            ? isPenaltyShown
                              ? NUMBER_COL_WITH_PENALTY_WIDTH
                              : NUMBER_COL_WIDTH
                            : header.column.id === 'name'
                              ? nameColWidth
                              : header.getSize(),
                        textAlign: meta?.numeric ? 'right' : 'left',
                        padding: '10px 16px',
                        borderBottom: `1px solid ${LINE_COLOR}`,
                        fontWeight: 500,
                        fontSize: '0.875rem',
                        color: MUTED_TEXT_COLOR,
                        cursor: canSort ? 'pointer' : 'default',
                        userSelect: 'none',
                      }}
                      onClick={
                        canSort
                          ? header.column.getToggleSortingHandler()
                          : undefined
                      }>
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
                        {meta?.tooltip != null && (
                          <Tooltip title={meta.tooltip}>
                            <IconButton
                              aria-label={`Help: ${headerLabel}`}
                              disableRipple
                              onClick={e => e.stopPropagation()}
                              sx={{
                                p: 0,
                                color: 'text.disabled',
                                cursor: 'help',
                              }}>
                              <InfoOutlined sx={{fontSize: 14}} />
                            </IconButton>
                          </Tooltip>
                        )}
                        {!isDualSort && sorted === 'asc' && (
                          <ArrowUpward sx={{fontSize: 16}} />
                        )}
                        {!isDualSort && sorted === 'desc' && (
                          <ArrowDownward sx={{fontSize: 16}} />
                        )}
                      </Box>
                    </th>
                  );
                })}
              </tr>
            ))}
          </thead>
          <tbody>
            {rows.map(row => (
              <tr
                key={row.id}
                style={{backgroundColor: 'white'}}
                onMouseEnter={e => {
                  e.currentTarget.style.backgroundColor = ROW_HOVER_COLOR;
                }}
                onMouseLeave={e => {
                  e.currentTarget.style.backgroundColor = 'white';
                }}>
                {row.getVisibleCells().map(cell => {
                  const meta = cell.column.columnDef.meta as
                    {numeric?: boolean} | undefined;
                  const copyable = cell.column.id !== 'actions';
                  return (
                    <td
                      key={cell.id}
                      className={copyable ? 'group' : undefined}
                      onClick={copyable ? copyOnClick : undefined}
                      onKeyDown={copyable ? copyOnKeyDown : undefined}
                      tabIndex={copyable ? 0 : undefined}
                      style={{
                        position: copyable ? 'relative' : undefined,
                        textAlign: meta?.numeric ? 'right' : 'left',
                        paddingTop: 10,
                        paddingBottom: 10,
                        paddingLeft: 16,
                        paddingRight: copyable ? 24 : 16,
                        borderBottom: `1px solid ${LINE_COLOR}`,
                        fontSize: '0.875rem',
                        whiteSpace: 'normal',
                        wordBreak: 'break-word',
                        overflowWrap: 'anywhere',
                        cursor: copyable ? 'pointer' : undefined,
                      }}>
                      {flexRender(
                        cell.column.columnDef.cell,
                        cell.getContext(),
                      )}
                      {copyable && <CopyCellAffordance />}
                    </td>
                  );
                })}
              </tr>
            ))}
            {rows.length === 0 && (
              <tr>
                <td
                  colSpan={table.getVisibleLeafColumns().length}
                  style={{
                    textAlign: 'center',
                    padding: '24px 12px',
                    color: '#999',
                    fontSize: '0.875rem',
                  }}>
                  {hideUnchanged
                    ? 'All rows are unchanged'
                    : 'No evaluation results'}
                </td>
              </tr>
            )}
          </tbody>
          <tfoot>
            <tr>
              {/* Name */}
              <td style={totalCellStyle}>
                <Typography
                  variant="body2"
                  sx={{fontWeight: 600, fontSize: '0.8125rem'}}>
                  Total
                </Typography>
              </td>
              {/* Description */}
              <td style={totalCellStyle}>
                <Typography
                  variant="body2"
                  sx={{color: 'text.secondary', fontSize: '0.75rem'}}>
                  Sum of the rows shown above. % is recomputed from the A and B
                  totals.
                </Typography>
              </td>
              {/* A */}
              <td style={{...totalCellStyle, textAlign: 'right'}}>
                <PreciseNumber
                  value={totals.srcTotal}
                  highlight={isConstraint}
                />
              </td>
              {/* B */}
              <td style={{...totalCellStyle, textAlign: 'right'}}>
                <PreciseNumber
                  value={totals.dstTotal}
                  highlight={isConstraint}
                />
              </td>
              {/* B - A, with % beneath */}
              <td style={{...totalCellStyle, textAlign: 'right'}}>
                <Box sx={STACKED_BA_SX}>
                  <PreciseNumber
                    value={totals.deltaTotal}
                    highlight={!isConstraint}
                  />
                  <PercentBeneath
                    value={Math.round(totals.percentTotal * 100) / 100}
                  />
                </Box>
              </td>
              {isPenaltyShown && (
                <>
                  {/* Penalty A */}
                  <td style={{...totalCellStyle, textAlign: 'right'}}>
                    <PreciseNumber
                      value={totals.srcPenaltyTotal}
                      highlight={false}
                    />
                  </td>
                  {/* Penalty B */}
                  <td style={{...totalCellStyle, textAlign: 'right'}}>
                    <PreciseNumber
                      value={totals.dstPenaltyTotal}
                      highlight={false}
                    />
                  </td>
                  {/* Penalty B - A */}
                  <td style={{...totalCellStyle, textAlign: 'right'}}>
                    <PreciseNumber value={totals.penaltyDeltaTotal} highlight />
                  </td>
                </>
              )}
              {/* Actions */}
              <td style={totalCellStyle} />
            </tr>
          </tfoot>
        </table>
      </Box>
      {snackbar}
    </Box>
  );
});

export default EvaluationTable;
