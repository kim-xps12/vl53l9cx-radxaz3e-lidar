#!/bin/bash
# SPDX-License-Identifier: MIT
# Stop the processes started by scripts/run_web.sh (only those, by PID file).
RUN=/tmp/vl53l9cx
for name in v4l2 web; do
    f="$RUN/$name.pid"
    [ -f "$f" ] || continue
    pid=$(cat "$f")
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
        kill -9 "$pid" 2>/dev/null
        [ "${1:-}" = quiet ] || echo "stopped $name ($pid)"
    fi
    rm -f "$f"
done
