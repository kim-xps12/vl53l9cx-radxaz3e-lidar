# VL53L9CX LiDAR on Radxa ZERO 3E

ST の dToF センサー VL53L9CX（54 × 42 区画）を Radxa ZERO 3E（RK3566）のカメラ端子に付属の FFC でつなぎ、
距離データを MIPI CSI-2 で受け取ってブラウザに 3D 点群として表示する環境。

参照元は [jonferran/vl53l9cx-pi-lidar](https://github.com/jonferran/vl53l9cx-pi-lidar)（Raspberry Pi 4 用、
`upstream/UPSTREAM_COMMIT` の版）。センサー起動プログラム、ST のドライバ（ULD）、Web 表示（LiDAR Web Studio）を
取り込み、Raspberry Pi 専用だった受信部分を RK3566 用に作り直した。XSHUT と 12 MHz クロックの扱いは
[adityakamath/vl53l9cx-pi-lidar](https://github.com/adityakamath/vl53l9cx-pi-lidar)（Raspberry Pi 5 版）を参考にした。

## 使い方

電源を入れると約 20 秒で自動的に受信と Web 表示が始まる（systemd のサービス `vl53l9cx-web`）。
ブラウザで `http://<Radxa の IP アドレス>:8080` を開く（mDNS が使えれば `http://radxa-zero3.local:8080`）。
3D 点群（ドラッグで回転、ホイールで拡大）、距離の色分け画像、PLY 形式の点群の保存、記録と再生ができる。
ログは `/tmp/vl53l9cx/{web,v4l2,bringup}.log`。

```bash
sudo systemctl stop vl53l9cx-web       # 止める（XSHUT が Low になり、センサーとレーザーも止まる）
sudo systemctl start vl53l9cx-web      # 始める
sudo systemctl disable vl53l9cx-web    # 起動時に自動で始めないようにする
journalctl -u vl53l9cx-web             # 起動時の出力
```

手で動かす場合は、サービスを止めてから `~/vl53l9cx/scripts/run_web.sh` と `scripts/stop.sh` を使う。

### Web 画面からのシャットダウン

画面右上の「⏻ Shut down」を押し、表示が「Click again to power off」に変わってから 5 秒以内にもう一度押す。
Radxa がシャットダウンする（センサーの停止 → OS の終了）。再び起動するには USB-C の電源を抜き差しする。
実測（2026-11-15）では、要求から OS の記録が止まるまで約 2.2 秒だった。画面は 10 秒の待ち時間を表示する。

- Web サーバ（ユーザ radxa で動作、認証なし）は root 権限を持たない。ボタンの要求を受けると `/run/vl53l9cx/poweroff` を
  作るだけで、root で動く systemd の監視ユニット `vl53l9cx-poweroff.path` がそれを見つけて `systemctl poweroff` を実行する。
- 要求は `POST /poweroff` に専用ヘッダ `X-VL53L9CX: poweroff` が付いたものだけを受け付ける。別のサイトのページからは、
  このヘッダを付けた要求をブラウザが送らせないので、閲覧中の他のサイト経由で勝手に落とされることはない。
  ただし同じ LAN の誰でもこの画面を開けば落とせる。
- 無効にするには `sudo systemctl disable --now vl53l9cx-poweroff.path`。

## 仕組み

```
              FFC 17 XSHUT  ◀── GPIO3_C6        （受信開始のたびに Low→High、停止で Low）
              FFC 18 AP_CLK ◀── GPIO4_C0 = CIF_CLKOUT 12 MHz
VL53L9CX      FFC 20/21 I²C ◀─▶ I²C2-M1 = /dev/i2c-2（センサー 0x29、EEPROM 0x55/0x5d）
(STEVAL)      FFC 2/3, 8/9  ──▶ MIPI CSI-2 1 レーン, RAW8, 1 Gbit/s
                                 └▶ csi2_dphy0 ─▶ mipi_csi2 ─▶ rkcif（VICAP）─▶ /dev/video0
                                                                       │ v4l2-ctl
build/vl53l9_bringup（I²C でファームウェア読込 → 測距設定 → CSI-2 出力開始）  ▼
                                                     /tmp/vl53l9cx/tof_pipe（名前付きパイプ）
                                                                       │
                                     upstream/web/lidar_web.py（HTTP :8080, WebGL 点群）
```

- センサーは距離画像ではなく「結果バッファ」を送る。1 行 100 バイト × 149 行（距離・信号強度・環境光・DSS の 148 行と、
  末尾の状態行 1 行）。VICAP は各行を 256 バイト幅で格納する（`bytesperline 256`、1 フレーム 38,144 バイト）。
- `radxa/driver/vl53l9cx_stub.c` は、受信ドライバ（rkcif、CSI-2、D-PHY）がセンサーに問い合わせる形式・レーン数・
  リンク周波数・フレーム間隔に答えるだけの空ドライバ。加えて 12 MHz の基準クロックを出し、XSHUT を操作する。
  センサー本体の設定はユーザ空間から I²C で行う。
- `radxa/dts/radxa-zero3-vl53l9cx.dts` は I²C2-M1（100 kHz）、CSI-2 の受信経路、空ドライバのノード
  （クロックと XSHUT の端子）を有効にするデバイスツリーの追加定義（オーバーレイ）。

### このボードで分かったこと（作りに反映済み）

| 事項 | 内容 | 根拠 |
|---|---|---|
| 12 MHz が要る | この STEVAL は基板の発振器が無効（R25 実装）で、AP_CLK にホストから 12 MHz を入れる必要がある | UM3655 の部品表。FFC 18 番に信号が来ていないことを観測 |
| XSHUT は受信中に離す | 受信経路（VI 電源区画、D-PHY）が止まった状態で XSHUT を Low→High にしても応答せず（3 通り試行）、受信開始の処理の中で行うと毎回起動した（6/6） | I²C の応答で確認。FFC 11 番（SYNC_IN）が RK3566 の D-PHY レーン 2 につながるのが理由の候補（未検証） |
| 起動引数が要る | rkcif はカーネル初期化の終わり（`/init` の前）に未接続の相手を待機リストから消す。外部モジュールの空ドライバは間に合わないので、`initcall_blacklist=rkcif_clr_unready_dev` で止める | `dmesg` の `clear unready subdev num: 1` と rkcif のソース |
| XSHUT ジャンパーは不要 | STEVAL 側で XSHUT はプルアップ済み。Radxa が FFC 17 番から操作する | ジャンパーを外しても線が High のまま |

## 配線

- STEVAL 付属の 22 ピン FFC を STEVAL の J1 と Radxa ZERO 3E のカメラ端子 J7 へ。端子配列は 1〜22 番まで一致
  （両方とも Raspberry Pi Zero の 22 ピンカメラ端子と同じ並び）。
- **STEVAL の J2 の XSHUT（5 番）と 3V3（8 番）のジャンパーは付けない。** Radxa が GPIO3_C6 で XSHUT を Low にするとき、
  ジャンパーがあると 3.3 V が短絡する。
- J3/JP3（ホスト側 I/O 電圧）は出荷時の 1-2（3.3 V）のまま。

## 導入（Radxa 上で 1 回）

前提：`radxa` ユーザが `i2c` と `video` グループに属している（標準イメージでは属している）。

```bash
# Mac から転送（Radxa に rsync が無いので tar で送る）
scripts/deploy.sh                    # git で管理しているファイルだけを送る。既定の宛先は ssh の別名 radxa-zero3e
                                     # 名前が引けないとき: SSH="ssh -o HostName=<IP> -o HostKeyAlias=radxa-zero3.local" scripts/deploy.sh

# Radxa 上
cd ~/vl53l9cx
make all                             # 起動プログラム, 空ドライバ, オーバーレイ
python3 -m venv --without-pip --system-site-packages .venv
.venv/bin/python -m pip install opencv-python-headless
sudo ./scripts/install_root.sh       # モジュールとオーバーレイの導入, extlinux.conf の追記, 自動起動の登録
sudo reboot
```

`install_root.sh` は `u-boot-update` を使わず、`/boot/extlinux/extlinux.conf` の既定の起動項目（l0）にだけ
`fdtoverlays` 行と起動引数 `initcall_blacklist=rkcif_clr_unready_dev` を足す。変更前のファイルは
`/boot/extlinux/extlinux.conf.orig-before-vl53l9cx` に残る。救済用の起動項目 l0r は変更しない。

## 測定値（2026-11-15、上記の構成、室内）

| 項目 | 値 | 条件・出所 |
|---|---|---|
| 受信フレームレート | 100.5 fps | `v4l2-ctl` の表示。binning 2、露光 4 ms、DSS 無効、周期 10 ms |
| Web 側の処理レート | 93〜94 fps | `/stats.json`。ブラウザ相当の接続 1 つ（色分け画像の連続受信と点群データの毎秒約 18 回の取得）あり・なしとも |
| CPU | Web 表示の Python が約 57%（1 コア換算） | `top`、上と同じ接続あり |
| SoC 温度 | 59 °C | ストリーミング中、ブラウザ 1 台接続 |
| 距離 | 最小 56 mm〜最大 3271 mm、全 2268 区画が有効 | 室内の 1 場面。距離は放射方向の生の値 |

## 既知の注意点

- **カーネル更新が途中で止まっている。** 調査時点で `linux-image-6.1.84-18` などが dpkg 上「展開済み・未設定（iU）」だった。
  `u-boot-update`、`apt upgrade`、`dpkg --configure -a` を実行すると 6.1.84-18 が既定の起動カーネルになり（initrd 無し）、
  `extlinux.conf` が作り直されて起動引数も消える。その場合は `make clean all` → `sudo ./scripts/install_root.sh` をやり直す。
  apt でパッケージを入れるときも同じことが起きうるので、OpenCV は pip で仮想環境に入れている。
- 自動サスペンドは無効化済み（`systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target`）。
  無効化前は、ログイン画面の設定で操作なし 20 分後にサスペンドしていた。
- 距離は放射方向の生の値（15 ビット, mm）。Web 表示側で ST の既定の校正値（個体ごとの OTP 校正ではない）を使って
  直交座標に変換している。
- 54 × 42（binning 2）のみ対応。binning 4（24 × 24）は 1 フレーム 39 行で、rkcif の最小の高さ 64 行を下回る。

## 参考資料

| 資料 | 使った内容 | 入手先 |
|---|---|---|
| STEVAL-VL53L9 回路図（Rev 1） | FFC（J1）の配列（2/6）、発振器と EEPROM（5/6） | https://www.st.com/resource/en/schematic_pack/steval-vl53l9-schematic.pdf |
| UM3655 | 部品表（R25、JP3）、FFC の向き | https://www.st.com/resource/en/user_manual/um3655-getting-started-with-the-stevalvl53l9-board-based-on-the-vl53l9cx-for-stm32-nucleo-n657x0-and-n6570dk-stmicroelectronics.pdf |
| Radxa ZERO 3E 回路図 V1.2 | カメラ端子 J7 の配列、VCCIO6 = 3.3 V | https://dl.radxa.com/zero3/docs/hw/3e/radxa_zero_3e_v1200_schematic.pdf |

ST の資料は著作物なので git には入れていない（手元では `docs/` に置き、`.gitignore` で除外）。資料から確認した事実：

- STEVAL の FFC（J1）と Radxa の J7 は 1〜22 番まで同じ並び。17 = XSHUT、18 = AP_CLK、20/21 = I²C、22 = 3.3 V（STEVAL 回路図 2/6）。
- 基板の 12 MHz 発振器は R25 実装で無効。ホストから AP_CLK に 12 MHz を入れる（STEVAL 回路図 5/6、UM3655 部品表）。
- JP3[1-2] 実装 = ホスト側 I/O 3.3 V（UM3655 部品表）。
- EEPROM M24C64 が 0x55（識別ページ 0x5d）で、センサーと同じ 1.8 V 側の I²C にある（STEVAL 回路図 5/6）。

## ファイル

| パス | 内容 |
|---|---|
| `radxa/driver/vl53l9cx_stub.c` | RK3566 の受信経路用の空センサードライバ（12 MHz クロック、XSHUT 制御を含む。GPL-2.0） |
| `radxa/dts/radxa-zero3-vl53l9cx.dts` | I²C2-M1、CSI-2 受信経路、クロックと XSHUT の端子を有効にするオーバーレイ |
| `scripts/install_root.sh` | root で 1 回：モジュール・オーバーレイ導入、extlinux.conf 追記 |
| `scripts/run_web.sh` / `scripts/stop.sh` | 受信・センサー起動・Web 表示の起動と停止 |
| `scripts/vl53l9cx-web.service` | 起動時に `run_web.sh` を動かす systemd のサービス（`install_root.sh` が登録） |
| `scripts/vl53l9cx-poweroff.path` / `.service` | Web 画面のシャットダウン要求を受けて電源を切る root 側のユニット |
| `scripts/deploy.sh` | Mac から Radxa の `~/vl53l9cx` へ転送 |
| `tools/i2c_probe.py` | I²C2 の応答アドレスとセンサーの型番・状態の読み出し（受信中に使う） |
| `upstream/` | jonferran/vl53l9cx-pi-lidar から取り込んだ部分（下の「クレジットとライセンス」を参照） |
| `LICENSE` / `LICENSES.md` | このリポジトリの MIT ライセンスと、その対象外の部分の一覧 |

参照元からの変更点：

- `upstream/app/vl53l9_bringup.c`：I²C バスを環境変数 `VL_I2C_DEV`（既定 `/dev/i2c-2`）で選べるようにした（元は `/dev/i2c-10` 固定）。
- `upstream/web/lidar_web.py`：
  - 1 行の格納幅、行数、1 フレームのバイト数を環境変数 `VL_STRIDE` / `VL_CSI_H` / `VL_FRAME_BYTES` で与えるようにした
    （元は Raspberry Pi の 128 バイト幅・148 行で固定）。
  - 画面右上のシャットダウンボタンと `POST /poweroff` を追加した。
  - OpenCV のスレッドを 1 本にし、受信用の名前付きパイプを 64 KB から 1 MB に広げた（`VL_PIPE_SZ=0` で元のまま）。直す前は、ブラウザが接続していると
    処理が 47 fps に落ち、パイプが詰まって受信側が毎秒約 50 フレームを取りこぼしていた。

## クレジットとライセンス

| 場所 | 作者 | ライセンス |
|---|---|---|
| `upstream/app`、`upstream/web` | jonferran（[jonferran/vl53l9cx-pi-lidar](https://github.com/jonferran/vl53l9cx-pi-lidar)、commit 36237d0）。`vl53l9_bringup.c` と `lidar_web.py` はこのリポジトリで改変（変更箇所に `Radxa port` の注記、差分は git の履歴） | MIT、Copyright (c) 2026 jonferran（`upstream/LICENSE`） |
| `upstream/vendor/st-uld` | STMicroelectronics（jonferran が待ち時間を変更） | SLA0111（`upstream/vendor/st-uld/LICENSE.txt`）。オープンソースのライセンスではなく、このリポジトリの MIT の対象外。ST のセンサーと組み合わせてのみ使える |
| `radxa/driver` | このリポジトリ | GPL-2.0（Linux カーネルモジュール） |
| `radxa/dts` | このリポジトリ | GPL-2.0+ または MIT |
| 上記以外 | このリポジトリ | MIT（`LICENSE`）。対象外の部分の一覧は `LICENSES.md` |

- 起動プログラム、Web 表示（LiDAR Web Studio）、校正とジェスチャー認識のコードは jonferran の作。このリポジトリで新しく書いたのは、
  Radxa で受信する部分（`radxa/`）、導入・起動のスクリプト、文書と、上記 2 ファイルへの変更。
- XSHUT と 12 MHz クロックの扱いは [adityakamath/vl53l9cx-pi-lidar](https://github.com/adityakamath/vl53l9cx-pi-lidar) の記述を参考にした（コードは使っていない）。
- STMicroelectronics とは無関係で、同社の承認も受けていない。VL53L9CX と STEVAL は ST の商標。

### Credits and license (English)

This repository ports [jonferran/vl53l9cx-pi-lidar](https://github.com/jonferran/vl53l9cx-pi-lidar)
(MIT, Copyright (c) 2026 jonferran) to the Radxa ZERO 3E (RK3566). The bring-up app, the LiDAR Web Studio and
the calibration and gesture code under `upstream/` are jonferran's work; `vl53l9_bringup.c` and `lidar_web.py`
were modified here (changes marked "Radxa port", full diff in the git history). `upstream/vendor/st-uld` is
STMicroelectronics' ULD under SLA0111, which is not an open-source license and is not covered by this
repository's MIT license. New in this repository: the Radxa capture path (`radxa/`, the kernel driver is
GPL-2.0), the install and run scripts, and the documentation (MIT). The XSHUT and 12 MHz clock handling follows
the notes of [adityakamath/vl53l9cx-pi-lidar](https://github.com/adityakamath/vl53l9cx-pi-lidar) (no code taken).
Not affiliated with or endorsed by STMicroelectronics; VL53L9CX and STEVAL are ST trademarks.
