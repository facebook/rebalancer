'use client';

import {useMemo} from 'react';

import {Autocomplete, Chip, TextField} from '@mui/material';

import {renderColumnTypeGroup} from '@/app/components/columnTypeGroup';
import {AUTOCOMPLETE_WORD_BREAK_PROPS, CHIP_WORD_BREAK_SX} from '@/lib/format';
import type {ColumnDescription} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';

const STRING_LIKE_TYPES = new Set([
  ColumnType.ASSIGNMENT,
  ColumnType.ENTITY_NAME,
  ColumnType.SCOPE,
  ColumnType.PARTITION,
  ColumnType.STRING,
]);

function isStringLikeColumn(type: ColumnType): boolean {
  return STRING_LIKE_TYPES.has(type);
}

function formatColumnType(type: ColumnType): string {
  switch (type) {
    case ColumnType.ASSIGNMENT:
      return 'Assignment';
    case ColumnType.ENTITY_NAME:
      return 'Entity Name';
    case ColumnType.SCOPE:
      return 'Scope';
    case ColumnType.PARTITION:
      return 'Partition';
    case ColumnType.STRING:
      return 'String';
    default:
      return 'Other';
  }
}

interface GroupBySelectorProps {
  columnDescriptions: ColumnDescription[];
  groupByColumns: string[];
  onGroupByChange: (columns: string[]) => void;
}

export default function GroupBySelector({
  columnDescriptions,
  groupByColumns,
  onGroupByChange,
}: GroupBySelectorProps) {
  const options = useMemo(
    () =>
      columnDescriptions
        .filter(col => isStringLikeColumn(col.type))
        .sort((a, b) => a.type - b.type),
    [columnDescriptions],
  );

  const selectedOptions = useMemo(
    () => options.filter(col => groupByColumns.includes(col.name)),
    [options, groupByColumns],
  );

  if (options.length === 0) {
    return null;
  }

  return (
    <Autocomplete
      multiple
      size="small"
      sx={{flex: 1, minWidth: 240, maxWidth: 360}}
      options={options}
      value={selectedOptions}
      onChange={(_event, newValue) => {
        onGroupByChange(newValue.map(col => col.name));
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
          label="Group by"
          placeholder={groupByColumns.length === 0 ? 'Select columns…' : ''}
        />
      )}
      slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
    />
  );
}
