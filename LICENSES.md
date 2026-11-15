# Licenses / ライセンスの範囲

The MIT license in [`LICENSE`](LICENSE) (Copyright (c) 2026 Yutaro KIMURA) covers the files of this
repository that carry no other license notice. It does **not** cover:

| Path | Author | License |
|---|---|---|
| `upstream/app`, `upstream/web`, `upstream/README.md` | jonferran ([vl53l9cx-pi-lidar](https://github.com/jonferran/vl53l9cx-pi-lidar), commit 36237d0); `vl53l9_bringup.c` and `lidar_web.py` modified here | MIT, Copyright (c) 2026 jonferran — [`upstream/LICENSE`](upstream/LICENSE) |
| `upstream/vendor/st-uld` | STMicroelectronics (timeouts changed by jonferran) | SLA0111 — [`upstream/vendor/st-uld/LICENSE.txt`](upstream/vendor/st-uld/LICENSE.txt). Not an open-source license; source redistribution keeps ST's notices; use only with ST sensors; must not be relicensed under open-source terms |
| `radxa/driver` | this repository | GPL-2.0 (Linux kernel module), SPDX line in each file |
| `radxa/dts` | this repository | GPL-2.0+ OR MIT, SPDX line in the file |

LICENSE の MIT（Copyright (c) 2026 Yutaro KIMURA）は、ほかのライセンス表示が無いこのリポジトリのファイルに適用する。
上の表の部分は対象外で、それぞれのライセンスに従う。詳しくは README の「クレジットとライセンス」を参照。
