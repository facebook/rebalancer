'use client';

import {useState} from 'react';

import {Box, Chip, IconButton, TextField, Tooltip} from '@mui/material';
import {Plus} from 'lucide-react';

import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {CHIP_WORD_BREAK_SX} from '@/lib/format';

import EntityTypeahead from './EntityTypeahead';

function capitalize(s: string): string {
  if (s.length === 0) {
    return s;
  }
  return s.charAt(0).toUpperCase() + s.slice(1).toLowerCase();
}

interface Move {
  variable: string;
  container: string;
}

interface MoveListProps {
  moves: Move[];
  onChange: (moves: Move[]) => void;
}

export default function MoveList({moves, onChange}: MoveListProps) {
  const {metadata} = useProblemMetadata();
  const variableName = metadata?.variableName;
  const containerName = metadata?.containerName;

  const [variable, setVariable] = useState<string | null>(null);
  const [container, setContainer] = useState<string | null>(null);

  const isDuplicate =
    variable != null &&
    container != null &&
    moves.some(m => m.variable === variable && m.container === container);
  const canAdd = variable != null && container != null && !isDuplicate;

  const handleAdd = () => {
    if (variable == null || container == null) {
      return;
    }
    onChange([...moves, {variable, container}]);
    setVariable(null);
    setContainer(null);
  };

  return (
    <div>
      {moves.length > 0 && (
        <Box
          sx={{
            maxHeight: 200,
            overflowY: 'auto',
            mb: 1,
          }}>
          <div className="flex flex-wrap gap-1">
            {moves.map((move, index) => (
              <Chip
                key={`${move.variable}|${move.container}`}
                label={
                  <span>
                    Move <strong>{move.variable}</strong> to{' '}
                    <strong>{move.container}</strong>
                  </span>
                }
                size="small"
                variant="outlined"
                // Chips default to user-select:none; re-enable so the move
                // text can be selected and copied (e.g. to share or reuse).
                sx={{
                  ...CHIP_WORD_BREAK_SX,
                  userSelect: 'text',
                  '& .MuiChip-label': {
                    ...CHIP_WORD_BREAK_SX['& .MuiChip-label'],
                    userSelect: 'text',
                    cursor: 'text',
                  },
                }}
                onDelete={() => {
                  onChange(moves.filter((_, i) => i !== index));
                }}
              />
            ))}
          </div>
        </Box>
      )}
      <div className="grid grid-cols-[1fr_1fr_max-content] gap-2 items-end mt-2">
        {variableName != null && containerName != null ? (
          <>
            <EntityTypeahead
              entity={variableName}
              label={capitalize(variableName)}
              value={variable}
              onChange={setVariable}
            />
            <EntityTypeahead
              entity={containerName}
              label={`Destination ${containerName}`}
              value={container}
              onChange={setContainer}
            />
          </>
        ) : (
          <>
            <TextField size="small" label="Variable" disabled />
            <TextField size="small" label="Container" disabled />
          </>
        )}
        <Tooltip title={isDuplicate ? 'This move already exists' : 'Add move'}>
          <span>
            <IconButton
              size="small"
              color="primary"
              disabled={!canAdd}
              onClick={handleAdd}>
              <Plus className="size-5" />
            </IconButton>
          </span>
        </Tooltip>
      </div>
    </div>
  );
}
