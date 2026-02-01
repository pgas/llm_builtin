#!/bin/bash
# Run script for interactive testing of the llm builtin

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SO_PATH="$ROOT_DIR/build/src/llm.so"

if [ ! -f "$SO_PATH" ]; then
  echo "Error: $SO_PATH not found. Please build the project first." >&2
  echo "  mkdir -p build && cd build && cmake .. && make" >&2
  exit 1
fi

# Use a clean subshell and ensure unload on exit
exec bash --noprofile --norc -c "
  set -e
  enable -f '$SO_PATH' llm
  trap 'enable -d llm' EXIT
  echo 'Loaded llm builtin. Type /help for commands, /exit to quit.'
  llm -i
"
