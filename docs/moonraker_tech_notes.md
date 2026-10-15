# KlipperDeckZero 技術調査メモ(Moonraker API × CardputerZero Template)

調査日: 2026-10-16

## 1. 全体構成

```
[CardputerZero (CM0 / aarch64 Linux)]
  ├─ Klipper (klippy)  ←Unix socket→  Moonraker (:7125)
  └─ 本アプリ (C++17 / LVGL 9.5, Templateベース)
        └─ WebSocket JSON-RPC  ws://127.0.0.1:7125/websocket
[開発PC (Windows) のデスクトップシミュレーター]
        └─ 同じコードで ws://<プリンターIP>:7125/websocket に接続
```

## 2. Moonraker 接続方式

| 方式 | エンドポイント | 認証 | 備考 |
|---|---|---|---|
| WebSocket (推奨) | `ws://host:7125/websocket` | trusted_clients / `X-Api-Key` / `server.connection.identify` の `api_key` | JSON-RPC 2.0。状態変化の通知(push)が受け取れる |
| Unix socket | `~/printer_data/comms/moonraker.sock` | 不要 | 各メッセージ末尾に ETX(0x03)。ローカル専用、Windowsシミュレーター不可 |
| HTTP | `GET/POST /printer/...` | 同上 | `{result: ...}` でラップ。通知は受け取れない |

→ **WebSocket 一本化**が実装を最小にできる(実機は localhost、シミュレーターはLAN上の実機へ)。
   実機の moonraker.conf `[authorization] trusted_clients` に 127.0.0.1 と開発PCのサブネットを入れておく。

### JSON-RPC 形式
```json
→ {"jsonrpc":"2.0","method":"printer.objects.query","params":{...},"id":12}
← {"jsonrpc":"2.0","result":{...},"id":12}
← {"jsonrpc":"2.0","error":{"code":-32601,"message":"..."},"id":12}
← {"jsonrpc":"2.0","method":"notify_status_update","params":[{...},eventtime]}   // idなし=通知
```

## 3. 起動・再接続シーケンス(公式推奨フロー)

1. `/websocket` に接続できるまでタイマーでリトライ
2. `server.connection.identify` {client_name, version, type:"display", url}(1接続1回)
3. `server.info` → `klippy_state` で分岐
   - `ready` → 4へ
   - `startup` → 2秒後に `server.info` 再実行
   - `error` / `shutdown` → `printer.info` の `state_message` を画面表示(FIRMWARE_RESTART ボタン提示)
   - `disconnected` → Klippy未起動。再試行
4. `printer.objects.subscribe` で必要オブジェクトを購読(レスポンスが初期スナップショット)
5. `notify_klippy_disconnected` / `notify_klippy_shutdown` 受信 → 3に戻る
6. WebSocket切断 → 1に戻る

## 4. 使用する API

### 状態取得
| 用途 | method | params |
|---|---|---|
| サーバー情報 | `server.info` | – |
| Klipper情報 | `printer.info` | – (`state`, `state_message`, `hostname`, `software_version`) |
| 購読 | `printer.objects.subscribe` | `{"objects":{"extruder":["temperature","target","power","can_extrude"], ...}}` |
| 単発取得 | `printer.objects.query` | 同上 |
| オブジェクト一覧 | `printer.objects.list` | – (fan名・センサー名の確認) |

### 操作
| 用途 | method | params / 送信G-code |
|---|---|---|
| G-code実行 | `printer.gcode.script` | `{"script":"..."}` 完了後に `"ok"` が返る(移動系は時間がかかる→タイムアウト長め) |
| 印刷開始 | `printer.print.start` | `{"filename":"xxx.gcode"}` |
| 一時停止/再開/中止 | `printer.print.pause` / `resume` / `cancel` | – |
| 非常停止 | `printer.emergency_stop` | –(M112をscriptで送るとキュー待ちになるので必ずこちら) |
| FW再起動 | `printer.firmware_restart` | – |
| ファイル一覧 | `server.files.list` | `{"root":"gcodes"}` → `[{path, modified, size, permissions}]` |
| メタデータ | `server.files.metadata` | `{"filename":...}` → `estimated_time`, `slicer`, `thumbnails`, `filament_total` など |

### 操作用 G-code(Klipper)
| 操作 | G-code |
|---|---|
| ノズル昇温 | `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200`(または `M104 S200`) |
| ベッド昇温 | `SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=60` |
| 冷却 | `TURN_OFF_HEATERS` |
| 押し出し(Extrude) | `M83` → `G1 E10 F300`(相対E。※`can_extrude`=true、既定 min_extrude_temp 170℃以上が必要) |
| 引き戻し(Retract) | `M83` → `G1 E-10 F1800` |
| ホーミング | `G28` |
| ファン | `M106 S255` / `M107` |
| モーターOFF | `M84` |

※ 押し出し前に `extruder.can_extrude` を UI 側でチェックし、false ならボタン無効化+トースト表示。
※ `SAVE_GCODE_STATE` / `RESTORE_GCODE_STATE` で包むと印刷側のE座標モードを壊さない。

## 5. 通知(サーバー → クライアント)

| method | params | 用途 |
|---|---|---|
| `notify_status_update` | `[{obj:{変化したフィールドのみ}}, eventtime]` | **差分**。クライアント側でマージ必須 |
| `notify_gcode_response` | `[string]` | `!! エラー` / `// 情報` をトースト表示 |
| `notify_klippy_ready` | – | 再購読 |
| `notify_klippy_shutdown` | – | エラー画面 |
| `notify_klippy_disconnected` | – | 再接続フロー |
| `notify_filelist_changed` | `[{action, item:{path,root,...}}]` | ファイル一覧の再取得 |
| `notify_proc_stat_update` | `[{cpu_temp, system_cpu_usage, ...}]` | ホストCPU温度表示(任意) |

## 6. 購読するプリンターオブジェクト(Klipper Status Reference)

| object | フィールド | 画面での用途 |
|---|---|---|
| `webhooks` | `state`, `state_message` | Klippy状態 |
| `print_stats` | `state`(standby/printing/paused/complete/cancelled/error), `filename`, `print_duration`, `total_duration`, `filament_used`, `message`, `info.current_layer/total_layer` | 印刷状態・経過時間・レイヤー |
| `virtual_sdcard` | `progress`, `is_active`, `file_position` | 進捗% |
| `display_status` | `progress`, `message` | M73/M117 の進捗・メッセージ |
| `extruder` | `temperature`, `target`, `power`, `can_extrude` | ノズル温度 |
| `heater_bed` | `temperature`, `target`, `power` | ベッド温度 |
| `toolhead` | `position`, `homed_axes`, `axis_minimum/maximum` | 座標・ホーム済み表示 |
| `gcode_move` | `speed_factor`, `extrude_factor`, `gcode_position` | 速度/流量倍率 |
| `fan` | `speed` | パーツファン |
| `idle_timeout` | `state`(Idle/Ready/Printing) | 動作中判定 |

残り時間の推定: `print_duration / progress - print_duration`(またはメタデータの `estimated_time` を利用)。

## 7. CardputerZero Template 側の制約

- C++17 / LVGL 9.5 / CMake(Presets: `linux-x86-64`, `win32-msvc`, `win32-mingw64`, `darwin-*`, `cp0-cross`)
- 画面 320×170。MVVM 構成: `model` → `viewmodel`(LVGL subject 公開)→ `view/screens`(`BaseScreen` 派生)
- 画面遷移は `ScreenManager` の遅延切替(observer内で画面削除しない)
- ナビバーは物理キー `4`〜`8`、`ESC` で終了、`Z`/`C`/←→ でページ送り
- 既存依存: fmt, libpng, libjpeg-turbo, zlib, SDL2(デスクトップのみ), FreeType。**JSON・WebSocketライブラリは無い**
- `app-builder.json` の `permissions.network` を `true` にする必要あり
- ハードウェア/通信系は `src/platform` に置き、重い処理はLVGLスレッドで行わない(テンプレートの方針)

## 8. 追加ライブラリ候補

| 用途 | 候補 | 理由 |
|---|---|---|
| JSON | nlohmann/json(FetchContent, header-only) | クロスビルドのsysrootに依存しない |
| WebSocket | IXWebSocket(FetchContent, TLS無効でビルド) | Windows/macOS/Linux/aarch64対応、自前スレッドで受信、依存なし(zlib任意) |
| (代替) | 実機は Unix socket を POSIX で直叩き | 依存ゼロだがシミュレーターでは使えない |

### スレッドモデル
- IXWebSocket の受信コールバックは別スレッド → ミューテックス付きキューに積む
- LVGL側は `lv_timer`(例: 50ms)でキューを取り出し、`PrinterState` にマージ → ViewModel の subject を更新
- LVGL API は必ずメインスレッドからのみ呼ぶ

## 9. 想定画面(案)

1. **Status**: 状態/ファイル名/進捗バー/経過・残り時間/ノズル・ベッド温度
2. **Temp**: ノズル・ベッドの目標温度 ±5℃、プリセット(PLA 200/60, PETG 235/80)、OFF
3. **Extruder**: 押し出し/引き戻し(量 1/5/10/50mm, 速度選択)、can_extrude表示
4. **Files**: gcode一覧 → 選択 → 確認 → 印刷開始
5. **Control**(印刷中): 一時停止/再開/中止、非常停止(長押し確認)

## 10. 決定事項(2026-10-16)

- 通信方式: **両方実装して切替**。`MoonrakerTransport` インターフェースを用意し、
  - 実機: `UnixSocketTransport`(`~/printer_data/comms/moonraker.sock`、ETX区切り、認証不要)
  - シミュレーター: `WebSocketTransport`(`ws://<ip>:7125/websocket`、IXWebSocket)
  - 設定ファイル(`[moonraker] transport=unix|websocket`, `socket_path=`, `url=`)で切替。上位の JSON-RPC クライアントは共通
- 第1段階の範囲: **通信層 + 状態表示画面**。温度・押出/引戻し・ファイル・印刷制御の画面は第2段階

## 参考
- Moonraker Web API: https://moonraker.readthedocs.io/en/latest/web_api/
- Moonraker 導入/JSON-RPC/起動フロー: https://moonraker.readthedocs.io/en/latest/external_api/introduction/
- Moonraker 通知一覧: https://moonraker.readthedocs.io/en/latest/external_api/jsonrpc_notifications/
- Klipper Status Reference: https://www.klipper3d.org/Status_Reference.html
- CardputerZero Template: https://github.com/CardputerZero/Template
