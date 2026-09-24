#!/bin/bash
# Sourced by the build and sync scripts.
PUBKY_ROOT="$(builtin cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="$(builtin cd "$PUBKY_ROOT/.." && pwd)"
if [[ -n "${DEPOT_TOOLS_DIR:-}" ]]; then
  export PATH="$DEPOT_TOOLS_DIR:$PATH"
else
  # Support both the documented checkout and the original local workspace.
  for candidate in "$SRC_ROOT/../depot_tools" "$SRC_ROOT/../../depot_tools"; do
    if [[ -x "$candidate/gclient" ]]; then
      export PATH="$candidate:$PATH"
      break
    fi
  done
fi
export DEPOT_TOOLS_UPDATE=0
