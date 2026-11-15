#!/bin/bash
# SPDX-License-Identifier: MIT
# One-time root setup on the Radxa ZERO 3E (run AFTER `make all` as the normal user):
#     sudo ./scripts/install_root.sh
# then reboot.
#
#  1. installs the stub sensor module into /lib/modules/<running kernel>/updates
#     (loaded automatically at boot through its device-tree compatible)
#  2. checks the overlay against the board's DTB, copies it to /boot/dtbo
#  3. in /boot/extlinux/extlinux.conf, default entry only (when it boots the
#     running kernel; a backup is kept next to it; the rescue entry is left as is):
#       - fdtoverlays /boot/dtbo/radxa-zero3-vl53l9cx.dtbo
#       - kernel argument initcall_blacklist=rkcif_clr_unready_dev
#     rkcif is built into the kernel and, at the end of kernel init (before
#     /init), drops every camera sub-device that has not appeared yet. The stub
#     is an out-of-tree module loaded later, so without this argument it is
#     never linked into the capture graph.
#  4. installs and enables scripts/vl53l9cx-web.service (capture + web viewer
#     on port 8080 at every boot; the service runs as the user "radxa") and
#     vl53l9cx-poweroff.path/.service (the web "Shut down" button).
#
# u-boot-update is deliberately NOT run: on this board linux-image-6.1.84-18
# is unpacked but not configured (dpkg state "iU"), and u-boot-update would
# make that kernel, which has no initrd, the default boot entry.
set -euo pipefail

REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KVER="$(uname -r)"
DTBO_NAME=radxa-zero3-vl53l9cx.dtbo
DTBO_SRC="$REPO/build/$DTBO_NAME"
KO_SRC="$REPO/radxa/driver/vl53l9cx_stub.ko"
EXTLINUX=/boot/extlinux/extlinux.conf
BASE_DTB="/usr/lib/linux-image-$KVER/rockchip/rk3566-radxa-zero-3e.dtb"

[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
for f in "$DTBO_SRC" "$KO_SRC" "$BASE_DTB" "$EXTLINUX"; do
    [ -f "$f" ] || { echo "missing: $f  (run 'make all' as the normal user first)"; exit 1; }
done
modinfo -F vermagic "$KO_SRC" | grep -q "^$KVER " || {
    echo "module was built for another kernel than $KVER; run 'make clean all'"; exit 1; }

echo "[1/4] kernel module -> /lib/modules/$KVER/updates/"
install -D -m 0644 "$KO_SRC" "/lib/modules/$KVER/updates/vl53l9cx_stub.ko"
depmod -a "$KVER"

echo "[2/4] overlay check against $(basename "$BASE_DTB"), copy to /boot/dtbo/"
fdtoverlay -i "$BASE_DTB" -o /tmp/vl53l9cx-merged.dtb "$DTBO_SRC"
rm -f /tmp/vl53l9cx-merged.dtb
install -m 0644 "$DTBO_SRC" "/boot/dtbo/$DTBO_NAME"

echo "[3/4] $EXTLINUX: overlay + kernel argument in the default entry (kernel $KVER)"
# one copy of the file as it was before the first run of this script
BACKUP="$EXTLINUX.orig-before-vl53l9cx"
[ -e "$BACKUP" ] || cp -p "$EXTLINUX" "$BACKUP"
python3 - "$EXTLINUX" "$KVER" "/boot/dtbo/$DTBO_NAME" "initcall_blacklist=rkcif_clr_unready_dev" <<'PY'
import sys
path, kver, dtbo, karg = sys.argv[1:5]
lines = open(path).read().split("\n")
out, changed, matched = [], 0, 0
# split into label blocks
blocks, cur = [], []
for ln in lines:
    if ln.startswith("label ") and cur:
        blocks.append(cur); cur = []
    cur.append(ln)
blocks.append(cur)
# only the default entry; the rescue entry stays as it was (fallback)
default = next((l.split()[1] for l in lines if l.startswith("default ")), None)
for b in blocks:
    if not b or b[0].split()[:2] != ["label", str(default)] or not any(
            l.strip() == f"linux /boot/vmlinuz-{kver}" for l in b):
        out.extend(b); continue
    matched += 1
    ov = [k for k, l in enumerate(b) if l.strip().startswith("fdtoverlays")]
    if ov:
        k = ov[0]
        if dtbo not in b[k].split():
            b[k] = b[k].rstrip() + " " + dtbo; changed += 1
    else:
        k = next((k for k, l in enumerate(b) if l.strip().startswith(("fdtdir", "fdt "))), None)
        if k is None:
            sys.exit(f"no fdt/fdtdir line in block {b[0]!r}")
        b.insert(k + 1, "\tfdtoverlays " + dtbo); changed += 1
    ap = [k for k, l in enumerate(b) if l.strip().startswith("append ")]
    if not ap:
        sys.exit(f"no append line in block {b[0]!r}")
    if karg not in b[ap[0]].split():
        b[ap[0]] = b[ap[0]].rstrip() + " " + karg; changed += 1
    out.extend(b)
if not matched:
    sys.exit(f"default entry {default!r} does not boot {kver}; extlinux.conf not changed")
open(path, "w").write("\n".join(out))
print(f"    entries changed: {changed}")
PY
echo "    backup: $BACKUP"
grep -n "label\|fdtoverlays\|initcall_blacklist" "$EXTLINUX" | sed 's/^/    /'

echo "[4/4] systemd units: vl53l9cx-web (auto-start at boot), vl53l9cx-poweroff (shutdown button)"
for u in vl53l9cx-web.service vl53l9cx-poweroff.path vl53l9cx-poweroff.service; do
    install -m 0644 "$REPO/scripts/$u" "/etc/systemd/system/$u"
done
systemctl daemon-reload
systemctl enable vl53l9cx-web.service vl53l9cx-poweroff.path

echo
echo "done. reboot to apply:  sudo reboot"
