# 操作画面(第2段階)

最終更新: 2026-10-25

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
- 編集していないときは、使わないキーを返す(`Ignored`)。`4`〜`8` のページ切替(表示中のナビレイヤーのスロット)、`Sym`+`H` / `Sym`+`J` のレイヤー切替、`D`、`S`、`ESC`(戻る)が効く。
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
- 最下行には、ノズル・ベッド以外の温度センサーと、この端末の CPU 温度を並べる(入力中・エラー時は操作説明やエラーを優先)。
  - センサーは購読の前に `printer.objects.query` で `heaters.available_sensors` を聞き、その `temperature` も購読に加える。
    聞けなかったときはセンサーなしで購読する。表示名はオブジェクト名の空白より後(`temperature_sensor mcu` → `mcu`)。
  - CPU 温度は `/sys/class/thermal` のうち type に `cpu` を含むゾーンを読む(バッテリー等の別ゾーンは使わない)。
    キー入力がなくても変わるので、2 秒ごとに描き直す。
  - 1 行に入らないときは横にスクロールする。

### 押し出し / 引き戻し(`6`)

- `app::ExtruderPanel`(`src/app/extruder_panel.*`)と `screen::ExtruderScreen`。
- 行: 長さ(1 / 5 / 10 / 25 / 50 mm、既定 10)/ 速度(2 / 5 / 10 mm/s、既定 5)/ 向き(引き戻し / 押し出し)。
  `←`/`→` で変え、`Enter` はどの行からでも実行する。数字入力はしない(数字キーはページ切替に残す)。
- 実行しない条件(`extrude_block_reason`): 印刷中、`extruder.can_extrude` が false(ノズルが `min_extrude_temp` 未満)。
  一時停止中はフィラメント交換のために実行できる。
- 送る G-code: `SAVE_GCODE_STATE` → `M83` → `G1 E<±長さ> F<速度×60>` → `RESTORE_GCODE_STATE`。
  印刷側の押し出しモード(絶対 / 相対)を変えないため。
- 画面上部に、ノズル温度と「押し出せるか」を出す。

### ファイル選択と印刷開始(`7`)

- `app::FileBrowser`(`src/app/file_browser.*`)と `screen::FilesScreen`。
- 画面を開くたびに `server.files.list`(`root=gcodes`)を取り、更新日時の新しい順に並べる。
  Klipper が ready でなくても、Moonraker につながっていれば一覧は出る。
- 5 行ぶんの行ウィジェットだけを作り、表示範囲をずらして描き直す(ファイルが多くても部品が増えない)。
- `Enter` で `server.files.metadata` を取り、予想時間・フィラメント量・レイヤー高さ(と造形物の高さ)・初層温度を出す。
  メタデータがないファイルも印刷できる。
- 詳細ではファイル名の下、詳細の 4 行の左に 64px 四方のサムネイルを出す。
  - メタデータの `thumbnails` から、枠を満たす最小のもの(なければ最大のもの)を選び、枠に合わせて縮小する。
    サムネイルのないファイルでは枠を隠し、詳細を左に寄せる。
  - 取得元は `app::ThumbnailSource`(`src/app/thumbnail_source.*`)。接続の種類で取り方を変える。
    - unix ソケット: Moonraker は同じ端末にいるので、`server.files.roots` で gcodes のディレクトリを一度聞き、ファイルを直接読む。
    - WebSocket: 同じホストの `http://<host>:<port>/server/files/gcodes/<path>` から HTTP で取る(`moonraker::HttpGetter`、
      IXWebSocket の HttpClient)。結果は `JsonRpcClient` と同じくキューに積み、LVGL のスレッドから `poll()` で渡す。
  - PNG はメモリのまま `lv_image_dsc_t` に包んで libpng デコーダーに渡す(ディスクにキャッシュしない)。
    同じ記述子を使い回すので、差し替える前に `lv_image_cache_drop()` でデコード済みの画像を捨てる。
  - 詳細の 4 行は Noto CJK だと高いので、行間を 2px 詰めてファイル名と合わせて枠に収める。
- もう一度 `Enter` で `printer.print.start`。印刷中・一時停止中は送らない。成功したら状態表示へ移る。
- 応答が画面を閉じた後に届いても触らないよう、画面が `shared_ptr` の生存フラグを持ち、コールバックは `weak_ptr` で確かめる。
- JSON の解析(`src/app/file_list_parser.*`)は moonraker のヘッダーを使うので、画面からは見えない別ファイルに分けた。

### 印刷制御(`8`)

- `app::ControlPanel`(`src/app/control_panel.*`)と `screen::ControlScreen`。
- 上部に状態バッジ・ファイル名・進捗・経過 / 残り時間・レイヤーを出す(状態表示と同じ subject を使う)。
- ボタン: Pause / Resume、Cancel、E-Stop。`←`/`→` で選ぶ。
  - Pause / Resume: `Enter` で `printer.print.pause` / `resume`。一時停止中はラベルが Resume になる。
  - Cancel: 3 秒以内に `Enter` 2 回で `printer.print.cancel`。1 回目でボタンが赤くなり、3 秒たつと戻る。
    ほかのキーを押すと取り消す。
  - E-Stop: `Enter` の長押し(700ms)で `printer.emergency_stop`。短く押すと「押し続けて」と出す。
    非常停止は Klipper が ready でなくても、Moonraker につながっていれば送る。
- 印刷していないときは Pause と Cancel を灰色にし、押すと「No print is running」と出す。
- 長押しは、短押しのイベントの後に届く。Pause / Cancel では長押しで操作を繰り返さない。

### 軸の移動(レイヤー 2 の `4`)

- `app::MovePanel`(`src/app/move_panel.*`)と `screen::MoveScreen`。
- 行: 軸(X / Y / Z、既定 X)/ 移動量(0.1 / 1 / 10 / 50 mm、既定 10)/ ジョグ − / ジョグ + / その軸のホーム /
  全軸ホーム / モーターオフ。`↑`/`↓` で選び、軸と移動量は `←`/`→` で循環する(端で反対側に回る)。
  ほかの行は `Enter` で実行する。数字入力はしない(数字キーはページ切替に残す)。
- 画面は高さが足りないので、軸と移動量を 1 行に、ジョグ −/+ を 1 行に、ホーム 2 つとモーターオフを 1 行に並べる。
  `↑`/`↓` はこの 7 個を順にたどる。
- 送り: XY 3000 mm/min、Z 600 mm/min(`jog_feed`)。
- 実行しない条件(`move_block`。理由はトーストとヒント行に出す):
  - 印刷中: すべて。
  - 一時停止中: ホームとモーターオフ(一時停止位置を失うため)。ジョグはできる(`RESUME` が位置を戻す)。
  - `toolhead.homed_axes` にない軸のジョグ: 「Home the axis first」。Klipper が拒否する移動を送らない。
  - Klipper が ready でないときはパネルを素通りし、`PrinterCommands` が「Printer is not ready」を出す。
- 送る G-code(`src/app/gcode.*`):
  - ジョグ: `SAVE_GCODE_STATE` → `G91` → `G1 <軸><±移動量> F<送り>` → `RESTORE_GCODE_STATE`。
    印刷側の位置決めモード(絶対 / 相対)を変えないため。移動量は末尾の 0 を付けない(`0.1`、`-50`)。
  - ホーム: `G28`(全軸)/ `G28 X`(1 軸)。モーターオフ: `M84`。
- 画面上部にツールヘッドの位置(`toolhead.position`)とホーム済みの軸(`homed_axes`)を出す。
  ビューモデルの `toolhead_position` / `homed_axes` subject で受け取る。
- `fake_moonraker` は `G28` で(軸の指定に関係なく)全軸をホーム済みにし、`M84` で解除する。XYZ のジョグでは位置を動かさない。

### 印刷中のサムネイル(状態表示)

- 印刷中・一時停止中で `print_stats.filename` が分かるとき、ファイル画面の詳細と同じ方法でサムネイルを出す:
  `server.files.metadata` → `pick_thumbnail` → `ThumbnailSource` → `lv_image_dsc_t` に包んで表示
  (差し替え前に `lv_image_cache_drop()`)。
- どのファイルのサムネイルを出すかは `app::StatusThumbnail`(`src/app/status_thumbnail.*`、LVGL 非依存)が決める。
  - 印刷が始まったとき、またはファイルが変わったときに 1 回だけ取る。同じ印刷の間は取り直さない。
  - 印刷が終わったら(完了・中止・待機)、または Klipper が ready でなくなったら消す。つながり直すと取り直す。
  - 前のファイルの応答が後から届いても使わない。
- 位置: 状態バッジ・ファイル名・進捗バー・時間の 3 行の左に 46px 四方(3 行と同じ高さ)。
  サムネイルがないときは枠を隠し、3 行が全幅を使う。画像が届いてから枠を出す。
- 状態表示は `PrinterCommands&` を受け取り、生存フラグ(`shared_ptr` / `weak_ptr`)で閉じた後の応答を捨てる。
- PNG の先頭の確認(`has_png_header`)はファイル画面と共用する。
- `fake_moonraker` は HTTP を持たないので、デスクトップのシミュレーター(WebSocket)ではサムネイルが出ない。

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
- ファイル一覧を `notify_filelist_changed` で自動更新するか(今は画面を開くたびに取り直す)。
- `fake_moonraker` は HTTP を持たないので、デスクトップではサムネイルが出ない。
