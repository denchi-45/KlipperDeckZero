# KlipperDeckZero

M5Stack **CardputerZero** に Klipper / Moonraker を載せ、本体の画面とキーボードから 3D プリンターを操作するための LVGL アプリです。

> 🚧 開発中です。第1段階(通信層 + 状態表示)まで実装済みです。

## できるようにしたいこと

- プリンターの状態表示(状態、進捗、経過/残り時間、ノズル・ベッド温度など)
- G-code ファイルの選択と印刷開始、一時停止/再開/中止
- ホットエンド・ベッドの昇温指示
- フィラメントの押し出し(Extrude)と引き戻し(Retract)

## 構成

- ベース: [CardputerZero/Template](https://github.com/CardputerZero/Template)(C++17 / LVGL 9.5 / CMake)
- プリンター制御: [Moonraker](https://moonraker.readthedocs.io/) の JSON-RPC API
  - 実機: Unix ソケット(`~/printer_data/comms/moonraker.sock`)
  - デスクトップシミュレーター: WebSocket(`ws://<printer-ip>:7125/websocket`)

## 画面と操作

| 状態表示(ライト) | 状態表示(ダーク) | 接続待ち |
|---|---|---|
| ![](docs/images/status-printing-light.png) | ![](docs/images/status-printing-dark.png) | ![](docs/images/status-connecting.png) |

| キー | 動作 |
|---|---|
| `4` | 状態表示(ホーム)。ホームで長押し or `ESC` 長押しで終了 |
| `5` / `6` / `7` / `8` | 温度 / 押し出し / ファイル / 印刷制御(第2段階で実装) |
| `ESC` | ホームへ戻る |
| `S` | 接続設定(この端末の Klipper / ネットワーク上のプリンター) |
| `D` | ライト / ダーク切替 |
| `Fn`+`H` / `Fn`+`J` | ヘルプ / スクリーンショット |

## ロードマップ

1. ✅ Moonraker 通信層 + 状態表示画面
2. 温度設定、押し出し/引き戻し、ファイル選択、印刷制御の画面

## ビルド

[CardputerZero/Template](https://github.com/CardputerZero/Template) と同じ CMake Presets を使います。

### Windows デスクトップシミュレーター(MinGW)

前提: CMake 3.31+, Ninja, MinGW-w64(WinLibs など), vcpkg(`VCPKG_ROOT` を設定)

```powershell
cmake --preset win32-mingw64
cmake --build build/mingw64 --config Debug
./build/mingw64/Debug/klipper-deck-zero.exe
```

MSVC の場合は `win32-msvc` プリセット(`build/msvc`)を使います。

### 実機(CardputerZero)向けクロスビルド / パッケージ

```sh
cmake --workflow --preset cp0-cross-package   # dist/KlipperDeckZero_*_arm64.deb
REMOTE_USER=pi REMOTE_HOST=<ip> ./deploy.sh
```

## 設定ファイル

- デスクトップ: `config/klipper-deck-zero.conf`
- 実機: `/etc/klipper-deck-zero.conf`(ユーザー設定は `~/.config/klipper-deck-zero/klipper-deck-zero.conf`)

```ini
[moonraker]
# unix: この端末の Klipper / websocket: 別の機械の Klipper
# (既定: 実機=unix, シミュレーター=websocket で 127.0.0.1)
transport=websocket
socket_path=~/printer_data/comms/moonraker.sock
# websocket のときの接続先(ws://<host>:<port>/websocket)
host=192.168.1.50
port=7125
# trusted_clients に入っていない場合のみ
#api_key=
```

## プリンターなしで動かす(疑似 Moonraker)

デスクトップビルドには `fake_moonraker` が含まれます。ヒーター昇温・押し出し温度チェック・印刷進捗・非常停止・FW再起動を模擬します。

```powershell
./build/mingw64/Debug/fake_moonraker.exe --speed 20   # ws://127.0.0.1:7125/websocket
./build/mingw64/Debug/klipper-deck-zero.exe
```

実機につなぐ場合は `host=<プリンターIP>` に変更します。

## テスト

Moonraker クライアント(JSON-RPC / 状態マージ / 接続シーケンス)と設定読み込みのユニットテストです。LVGL なしでも単独でビルドできます。

```powershell
ctest --test-dir build/mingw64 -C Debug            # プリセットビルドに含まれる
# もしくは単独で
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ./build/tests/kdz_tests
```

## ドキュメント

- [Moonraker API × CardputerZero 技術調査メモ](docs/moonraker_tech_notes.md)
