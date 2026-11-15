# 参考資料

ST の資料は著作物なので git には入れていない（`.gitignore` で除外）。必要なら次から保存してこのフォルダに置く。

| ファイル | 内容 | 入手先 |
|---|---|---|
| `steval-vl53l9-schematic.pdf` | STEVAL-VL53L9 回路図（Rev 1） | https://www.st.com/resource/en/schematic_pack/steval-vl53l9-schematic.pdf |
| `um3655-getting-started-with-the-stevalvl53l9-board-....pdf` | UM3655（部品表、FFC の向き、JP3 の設定） | https://www.st.com/resource/en/user_manual/um3655-getting-started-with-the-stevalvl53l9-board-based-on-the-vl53l9cx-for-stm32-nucleo-n657x0-and-n6570dk-stmicroelectronics.pdf |
| Radxa ZERO 3E 回路図 V1.2 | カメラ端子 J7 の配列、VCCIO6 = 3.3 V | https://dl.radxa.com/zero3/docs/hw/3e/radxa_zero_3e_v1200_schematic.pdf |

この設計で使った事実（どのページで確認したか）：

- STEVAL の FFC（J1）と Radxa の J7 は 1〜22 番まで同じ並び。17 = XSHUT、18 = AP_CLK、20/21 = I²C、22 = 3.3 V（回路図 2/6）。
- 基板の 12 MHz 発振器は R25 実装で無効、ホストから AP_CLK に 12 MHz を入れる（回路図 5/6、UM3655 部品表）。
- JP3[1-2] 実装 = ホスト側 I/O 3.3 V（UM3655 部品表）。
- EEPROM M24C64 が 0x55（識別ページ 0x5d）、センサーと同じ 1.8 V 側の I²C（回路図 5/6）。
