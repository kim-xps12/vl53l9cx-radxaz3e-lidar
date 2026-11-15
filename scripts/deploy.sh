#!/bin/bash
# Copy this project from the Mac to the Radxa (~/vl53l9cx). Radxa has no rsync.
#   scripts/deploy.sh [ssh-host]      default host: radxa-zero3e
set -euo pipefail
HOST=${1:-radxa-zero3e}
SSH=${SSH:-ssh}   # e.g. SSH="ssh -F my.conf"
cd "$(dirname "$0")/.."
COPYFILE_DISABLE=1 tar --exclude .git --exclude build --exclude .venv --exclude '*.o' --exclude '*.ko' \
    --exclude '*.cmd' --exclude '*.mod*' --exclude Module.symvers --exclude modules.order \
    --exclude '__pycache__' --exclude '.DS_Store' --exclude 'docs/*.pdf' \
    -czf - . | $SSH "$HOST" 'mkdir -p ~/vl53l9cx && tar -xzf - -C ~/vl53l9cx 2>&1 | grep -v "unknown extended header" || true'
echo "copied to $HOST:~/vl53l9cx"
