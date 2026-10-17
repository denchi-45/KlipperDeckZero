# 操作画面(第2段階)

最終更新: 2026-10-17

## 現状

### 画面から命令を送る仕組み

- `app::PrinterCommands`(`src/app/printer_commands.*`)が、画面からの操作を Moonraker に送る。
  - 接続中のセッションを持つ。接続先を変えたときは `app.cpp` の `connect` で差し替える。
  - セッションが `Ready` でなければ送らず、「Printer is not ready」をトーストで出す。
  - 結果(成功 / Klipper のエラーの 1 行目)をトーストで出す。
- トーストは `Application` が 1 つ持ち、ショートカット処理と `PrinterCommands` で共用する。
- 画面は `ScreenManager` から `PrinterCommands&` を受け取る。
- G-code の文字列は `src/app/gcode.*` で作る(LVGL に依存しないので単体テストできる)。

### キー操作の決まり

- 画面のキー処理(`platform::set_screen_key_handler`)が、グローバルなショートカットより先にキーを受け取る。
- 編集していないときは、使わないキーを返す(`Ignored`)。`4`〜`8` のページ切替、`D`、`S`、`ESC`(戻る)が効く。
- 数字を打ち込んでいる間は、すべてのキーを画面が受け取る。`6` を打ってもページは切り替わらない。
- 画面の操作ロジック(フォーカス、下書き、値の範囲)は LVGL に依存しないクラスに分け、単体テストする。
  設定画面の `SettingsForm` と同じ形。

### 温度設定(`5`)

- `app::TemperaturePanel`(`src/app/temperature_panel.*`)と `screen::TemperatureScreen`。
- 行: ノズル / ベッド / プリセット。ベッドがない構成ではベッドの行を隠す。
- ノズル・ベッド: `←`/`→` で 5 刻み(5 の倍数にそろえる)、`Enter` で数字入力(最初の数字で置き換え)、
  もう一度 `Enter` で送信、`ESC` で取消。上限はノズル 350、ベッド 150。実際の上限は Klipper の `max_temp` が判定する。
- プリセット: PLA 200/60、PETG 235/80、Off。コードに固定。
- 送信する G-code: `SET_HEATER_TEMPERATURE HEATER=extruder|heater_bed TARGET=<n>`。プリセットは 2 行を 1 回で送る。

### 押し出し / 引き戻し(`6`)

- `app::ExtruderPanel`(`src/app/extruder_panel.*`)と `screen::ExtruderScreen`。
- 行: 長さ(1 / 5 / 10 / 25 / 50 mm、既定 10)/ 速度(2 / 5 / 10 mm/s、既定 5)/ 向き(引き戻し / 押し出し)。
  `←`/`→` で変え、`Enter` はどの行からでも実行する。数字入力はしない(数字キーはページ切替に残す)。
- 実行しない条件(`extrude_block_reason`): 印刷中、`extruder.can_extrude` が false(ノズルが `min_extrude_temp` 未満)。
  一時停止中はフィラメント交換のために実行できる。
- 送る G-code: `SAVE_GCODE_STATE` → `M83` → `G1 E<±長さ> F<速度×60>` → `RESTORE_GCODE_STATE`。
  印刷側の押し出しモード(絶対 / 相対)を変えないため。
- 画面上部に、ノズル温度と「押し出せるか」を出す。

### 共通部品

- キーの対応(`screen::to_panel_input`、`src/view/screens/panel_keys.*`)と、
  行・選択肢の見た目(`view::widgets::style_panel_row` / `style_chip`、`src/view/widgets/panel_style.*`)は画面間で共用する。

### 実機での確認方法

実機の Klipper を動かさずに確かめるため、実機上で疑似 Moonraker(`fake_moonraker`)をポート 7126 で動かし、
`XDG_CONFIG_HOME` で一時的なユーザー設定を読ませて、アプリをそちらにつなぐ。
クロスビルドで `-DKDZ_BUILD_FAKE_MOONRAKER=ON` を付けると aarch64 版ができる。

## 決定事項

### 2026-10-17: 第2段階は 1 画面ずつ PR にする

- 温度設定 → 押し出し → ファイル → 印刷制御 の順。
- 共通の仕組み(`PrinterCommands`、共用トースト)は温度設定の PR に入れる。

### 2026-10-17: 温度は数字で表示し、「°」を付けない

- デスクトップのシミュレーターが使う Montserrat フォントに「°」がないため。状態表示画面と同じ。

## 未決事項 / TODO

- プリセットを設定ファイルで変えられるようにするか。
- ファイル / 印刷制御の画面。
