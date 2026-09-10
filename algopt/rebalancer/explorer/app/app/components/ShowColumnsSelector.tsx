'use client';

import {useMemo} from 'react';

import {Autocomplete, Chip, TextField} from '@mui/material';

import {renderColumnTypeGroup} from '@/app/components/columnTypeGroup';
import {AUTOCOMPLETE_WORD_BREAK_PROPS, CHIP_WORD_BREAK_SX} from '@/lib/format';
import type {ColumnDescription} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';

interface ShowColumnsSelectorProps {
  columnDescriptions: ColumnDescription[];
  groupByColumns: string[];
  showColumns: string[];
  onShowColumnsChange: (columns: string[]) => void;
}

function formatColumnType(type: ColumnType): string {
  switch (type) {
    case ColumnType.DIMENSION:
      return 'Dimension';
    case ColumnType.UTILIZATION:
      return 'Utilization';
    case ColumnType.ASSIGNMENT:
      return 'Assignment';
    case ColumnType.SCOPE:
      return 'Scope';
    case ColumnType.PARTITION:
      return 'Partition';
    case ColumnType.IDENTIFIER:
      return 'Identifier';
    case ColumnType.DOUBLE:
      return 'Double';
    case ColumnType.INTEGER:
      return 'Integer';
    case ColumnType.STRING:
      return 'String';
    default:
      return 'Other';
  }
}

export default function ShowColumnsSelector({
  columnDescriptions,
  groupByColumns,
  showColumns,
  onShowColumnsChange,
}: ShowColumnsSelectorProps) {
  // Entity name columns are always shown, and group-by columns are always
  // shown, so neither should appear as selectable options.
  const options = useMemo(
    () =>
      columnDescriptions
        .filter(
          col =>
            col.type !== ColumnType.ENTITY_NAME &&
            !groupByColumns.includes(col.name),
        )
        .sort((a, b) => a.type - b.type),
    [columnDescriptions, groupByColumns],
  );

  const selectedOptions = useMemo(
    () => options.filter(col => showColumns.includes(col.name)),
    [options, showColumns],
  );

  if (options.length === 0) {
    return null;
  }

  return (
    <Autocomplete
      multiple
      size="small"
      sx={{flex: 1, minWidth: 280}}
      options={options}
      value={selectedOptions}
      onChange={(_event, newValue) => {
        onShowColumnsChange(newValue.map(col => col.name));
      }}
      getOptionLabel={option => option.name}
      groupBy={option => formatColumnType(option.type)}
      renderGroup={renderColumnTypeGroup}
      isOptionEqualToValue={(option, value) => option.name === value.name}
      renderTags={(value, getTagProps) =>
        value.map((option, index) => {
          const {key, ...chipProps} = getTagProps({index});
          return (
            <Chip
              key={key}
              label={option.name}
              size="small"
              sx={CHIP_WORD_BREAK_SX}
              {...chipProps}
            />
          );
        })
      }
      renderInput={params => (
        <TextField
          {...params}
          label="Columns to show"
          placeholder={
            showColumns.length === 0
              ? 'Type the name of a dimension, scope, partition…'
              : ''
          }
        />
      )}
      slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
    />
  );
}
