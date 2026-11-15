#!/bin/bash
# SPDX-License-Identifier: MIT
# Copy this project from the Mac to the Radxa (~/vl53l9cx). Radxa has no rsync.
#   scripts/deploy.sh [ssh-host]      default host: radxa-zero3e
set -euo pipefail
HOST=${1:-radxa-zero3e}
SSH=${SSH:-ssh}   # e.g. SSH="ssh -F my.conf"
cd "$(dirname "$0")/.."
# only files tracked by git (their working-tree content, uncommitted edits included):
# no build outputs, no .git, no ST PDFs, no private notes lying in the folder
git ls-files -z | COPYFILE_DISABLE=1 tar --null -T - -czf - | $SSH "$HOST" 'mkdir -p ~/vl53l9cx && tar -xzf - -C ~/vl53l9cx 2>&1 | grep -v "unknown extended header" || true'
echo "copied to $HOST:~/vl53l9cx"
