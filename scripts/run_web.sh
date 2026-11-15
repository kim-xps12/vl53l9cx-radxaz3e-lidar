#!/bin/bash
# VL53L9CX -> LiDAR Web Studio on the Radxa ZERO 3E (no sudo needed: the user
# must be in the i2c and video groups).
#
#   scripts/run_web.sh [port]          default port 8080
#   env: VL_I2C_DEV (default /dev/i2c-2), VL_EXP_MS (4), VL_PERIOD_US (10000)
#
# Order matters: the VICAP receiver is armed (v4l2-ctl streaming) BEFORE the
# sensor is started over I2C, so the D-PHY sees the start of transmission.
set -u
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
cd "$REPO" || exit 1

PORT=${1:-8080}
RUN=/tmp/vl53l9cx
PIPE=$RUN/tof_pipe
W=100
H=${VL_CSI_LINES:-149}          # binning 2: 148 data lines + 1 status line
USECASE=1                       # ULD use case with binning 2 (54 x 42 zones)
EXP=${VL_EXP_MS:-4}
PERIOD=${VL_PERIOD_US:-10000}   # 100 Hz
POWER=0                         # regular
DSS=0                           # DSS off (needed for 100 Hz)
export VL_I2C_DEV=${VL_I2C_DEV:-/dev/i2c-2}
PY="$REPO/.venv/bin/python"

say() { echo "[+] $*"; }
die() { echo "[-] $*" >&2; exit 1; }

find_node() {   # $1 = video|v4l-subdev, $2 = name in sysfs
    local d
    for d in /sys/class/video4linux/"$1"*; do
        [ -e "$d/name" ] && [ "$(cat "$d/name")" = "$2" ] && { echo "/dev/$(basename "$d")"; return 0; }
    done
    return 1
}

[ -x build/vl53l9_bringup ] || make -s app || die "build failed"
[ -x "$PY" ] || die "python venv missing: see README (setup)"

# At boot (systemd service) the stub module is loaded ~20 s after power-on:
# wait up to VL_WAIT seconds for the stub sub-device to appear.
WAIT=${VL_WAIT:-0}
while [ "$WAIT" -gt 0 ] && ! find_node v4l-subdev vl53l9cx-stub >/dev/null; do
    sleep 1; WAIT=$((WAIT - 1))
done
[ -e "$VL_I2C_DEV" ] || die "$VL_I2C_DEV not found: overlay not active (scripts/install_root.sh + reboot)"
lsmod | grep -q '^vl53l9cx_stub' || die "module vl53l9cx_stub not loaded (kernel $(uname -r)): rebuild with 'make clean all' and rerun scripts/install_root.sh"

VID=$(find_node video stream_cif_mipi_id0) || die "VICAP capture node stream_cif_mipi_id0 not found"
SUB=$(find_node v4l-subdev vl53l9cx-stub) || die "stub sub-device not found"

"$REPO/scripts/stop.sh" quiet
mkdir -p "$RUN"
[ -p "$PIPE" ] || { rm -f "$PIPE"; mkfifo "$PIPE"; }

say "stub $SUB, capture $VID: ${W}x${H} RAW8 (GREY)"
v4l2-ctl -d "$SUB" --set-subdev-fmt pad=0,width=$W,height=$H,code=0x2001 >/dev/null 2>&1 || die "subdev format"
v4l2-ctl -d "$VID" --set-fmt-video=width=$W,height=$H,pixelformat=GREY || die "video format"
FMT=$(v4l2-ctl -d "$VID" --get-fmt-video)
BPL=$(echo "$FMT" | awk -F: '/Bytes per Line/{gsub(/ /,"",$2); print $2; exit}')
SIZE=$(echo "$FMT" | awk -F: '/Size Image/{gsub(/ /,"",$2); print $2; exit}')
GOT_W=$(echo "$FMT" | awk -F'[:/]' '/Width\/Height/{gsub(/ /,"",$3); print $3; exit}')
GOT_H=$(echo "$FMT" | awk -F'[:/]' '/Width\/Height/{gsub(/ /,"",$4); print $4; exit}')
[ -n "$BPL" ] && [ -n "$SIZE" ] || die "cannot read the capture format:\n$FMT"
say "driver format: ${GOT_W}x${GOT_H}, bytesperline $BPL, sizeimage $SIZE"
[ "$GOT_H" = "$H" ] || die "driver changed the height to $GOT_H"

say "web studio on port $PORT"
VL_BINNING=2 VL_PIPE=$PIPE VL_PORT=$PORT VL_STRIDE=$BPL VL_CSI_H=$H VL_FRAME_BYTES=$SIZE \
    nohup "$PY" upstream/web/lidar_web.py > "$RUN/web.log" 2>&1 &
echo $! > "$RUN/web.pid"
sleep 2

say "arming the receiver"
nohup v4l2-ctl -d "$VID" --stream-mmap=4 --stream-to="$PIPE" > "$RUN/v4l2.log" 2>&1 &
echo $! > "$RUN/v4l2.pid"
sleep 1

say "booting the sensor and starting the CSI-2 stream ($VL_I2C_DEV)"
build/vl53l9_bringup $USECASE $EXP $PERIOD $POWER $DSS 2>&1 | tee "$RUN/bringup.log" \
    | grep -E 'Connecting|Initial FSM|vl53l9_init|set_profile|readback|STREAMING|TRANSMITTER|did not|failed|Unable|Aborting'
rc=${PIPESTATUS[0]}
[ "$rc" = 0 ] || echo "[-] bring-up returned $rc; full log: $RUN/bringup.log"

sleep 2
IP=$(hostname -I | awk '{print $1}')
echo
echo "  stats : $(curl -s --max-time 2 http://127.0.0.1:$PORT/stats.json | head -c 160)"
echo "  open  : http://${IP}:${PORT}   (or http://$(hostname).local:${PORT})"
echo "  logs  : $RUN/{web,v4l2,bringup}.log      stop: scripts/stop.sh"
