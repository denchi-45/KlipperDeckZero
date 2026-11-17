# Moonraker 接続

最終更新: 2026-10-16

## 現状

2026-10-16 時点の `dev` での動作。

### 接続先の決め方

接続先は設定ファイルで固定。自動探索はない。起動時に次の順で決まり、後のものが前のものを上書きする。

1. 組み込みの既定値(`src/app/application_config.h`)。既定は「この端末の Klipper につなぐ」設定。
   - 接続方式: 実機は `unix`、デスクトップシミュレーターは `websocket`(Windows では unix ソケットが使えないため)
   - `socket_path=~/printer_data/comms/moonraker.sock`
   - `host=127.0.0.1`, `port=7125`
2. システム設定 `APP_CONFIG_FILE`
   - 実機: `/etc/klipper-deck-zero.conf`
   - デスクトップ: リポジトリの `config/klipper-deck-zero.conf`
3. ユーザー設定(実機のみ。ファイルがあれば読み、書かれているキーだけ上書き)
   - `$XDG_CONFIG_HOME/klipper-deck-zero/klipper-deck-zero.conf`
     または `~/.config/klipper-deck-zero/klipper-deck-zero.conf`
   - 画面で切り替えたダークモードはここに保存される

`PrinterService`(`src/app/printer_service.cpp`)が設定から通信方式を 1 つ作る。

- `unix`: `~` を `$HOME` に展開したソケットパスに接続する。メッセージは ETX(0x03)区切り。
  Windows では使えない。
- `websocket`: `ws://<host>:<port>/websocket` に接続する。`api_key` を設定していれば `X-Api-Key` ヘッダーで送る。

接続先は、接続設定画面(`S` キー)から変えられる。保存するとその場でつなぎ直す。
設定ファイルを直接編集した場合は、再起動すると反映される。

### 設定項目の追加方法

設定項目は `src/app/application_config.cpp` の表(`kSettings`)で一元管理している。
1 項目ごとに、セクション名・キー名・読み込み(値の検証)・書き出しを持つ。
読み込みも保存もこの表を見るので、項目を増やすときは次の 3 か所に足すだけでよい。

1. `ApplicationConfig`(またはその中の構造体)にフィールドを追加する
2. `enum class Setting` に値を追加する
3. `kSettings` に 1 行追加する

保存(`save_application_config`)は、渡した項目だけを書き換える。
ほかのキー、コメント、知らないセクションはそのまま残す。
ユーザー設定には変えた項目だけが入るので、システム設定の値を不必要に上書きしない。

### 接続後の流れ(`src/moonraker/moonraker_session.cpp`)

LVGL のタイマーで 50ms ごとに `poll()` を呼び、状態を進める。

```
Connecting → Initializing → (WaitingKlippy / KlippyError) → Ready
```

1. 接続する。失敗したり切断されたりしたら、3 秒後に再接続する。
2. `server.connection.identify` でクライアント情報を通知する。失敗しても次へ進む。
3. `server.info` で Klippy の状態を確認する。
   - `ready` なら購読へ進む。
   - 起動中なら 2 秒ごとに再確認する(`WaitingKlippy`)。
   - エラーなら `KlippyError` になり、Moonraker からの復帰通知を待つ。
     あわせて 10 秒ごとにも再確認する。
4. `printer.objects.subscribe` で温度や進捗などを購読し、`Ready` になる。
   以降は `notify_status_update` の差分を `PrinterState` に反映し、画面を更新する。

## 決定事項

### 2026-10-16: 同じ LAN 上のプリンターに IP アドレスで接続するのを基本にする

- 使い方: CardputerZero を、別の機械(Raspberry Pi など)で動いている Klipper/Moonraker の
  リモート操作パネルとして使う。同じネットワーク内からつなぐ。
- そのため、接続方式は **WebSocket(`ws://<IP>:7125/websocket`)を主にする**。
- unix ソケット(CardputerZero 自身で Moonraker が動く構成)は補助的な扱いにする。
- 前提の変化: `../moonraker_tech_notes.md` は、Klipper を CardputerZero 上で動かす構成を前提にしていた。
  今後はこの決定を優先する。
- (2026-10-16 改訂)unix ソケットの扱いは下の決定で見直した。

### 2026-10-16: 接続先は「このデバイス」と「ネットワーク」から選ぶ。既定は「このデバイス」

- CardputerZero 自身で Klipper/Moonraker が動いているときは、unix ソケットでつなぎたい。
  そのため unix ソケットを「詳細設定」に隠さず、接続先として最初から選べるようにする。
  - **このデバイス**: unix ソケット。標準のパスなら入力不要。パスは詳細設定で変えられる。
  - **ネットワーク**: WebSocket。IP アドレス(またはホスト名)とポートを入力する。
- 既定値は「このデバイス」(実機では unix)。ソケットの有無による自動判定はしない。
- 設定ファイルでは、接続先を `url` ではなく `host` と `port` で持つ。画面の入力欄とそのまま対応させるため。
  `ws://` と `/websocket` は固定。
- 設定項目は今後も増えるので、1 項目を表の 1 行で追加できる作りにする(上の「設定項目の追加方法」)。

## 検討メモ

### unix ソケットと WebSocket の違い

どちらも運ぶメッセージ(JSON-RPC)は同じ。違うのは通り道だけ。

| | unix ソケット | WebSocket |
|---|---|---|
| 仕組み | 同じ機械の中で、ファイルのような窓口(`moonraker.sock`)を通して直接やり取りする | Moonraker が Web 画面向けに公開しているネットワークの窓口を使う |
| 接続先の指定 | ファイルの場所(ソケットパス) | IP アドレス(またはホスト名)とポート |
| 別の機械から | **使えない** | 使える |
| 速さ・負荷 | 最小 | Wi-Fi の状態に左右される |
| 認証 | 実質不要(ファイルに触れる権限があればよい) | `trusted_clients` に入っていない場合は API キーが必要 |
| 安全性 | ネットワークに出ない | 暗号化なし(家の LAN なら普通は問題ない) |
| Windows シミュレーター | 使えない | 使える |
| 注意点 | 実行ユーザーや `HOME` がずれるとパスが合わない | プリンターの IP が DHCP で変わるとつながらなくなる |

- 同じ LAN 上の別のプリンターにつなぐなら、選択肢は WebSocket だけ。
- WebSocket は Mainsail / Fluidd と同じ経路で、実績も情報も多い。

### 設定項目の意味

- **IP アドレス / ポート**: ネットワーク越しにつなぐときの接続先。ポートは Moonraker の標準の 7125。
  URL の残りの部分(`ws://` と `/websocket`)は固定なので、ユーザーが入力する必要はない。
- **ソケットパス**: 同じ機械の中でつなぐときの窓口ファイルの場所。
  標準的にインストールしていれば `~/printer_data/comms/moonraker.sock` で決まっている。
- **API キー**: Moonraker のアクセス制限を通るための合言葉。
  家の LAN からの接続は、たいてい `moonraker.conf` の `[authorization] trusted_clients` で
  許可されているので、普通は不要。

## 未決事項 / TODO

### 接続設定画面

1. ✅ 設定の読み書きを表で一元管理し、任意の項目を保存できるようにする(PR #6)。
   接続先を `host` / `port` で持つ。既定は「このデバイス」(unix)。
2. ✅ 接続設定画面を作る(`feature/connection-screen`)。詳しくは下の「接続設定画面の作り」。
3. ✅ 接続できないときは、状態表示の接続待ち画面に「S: connection settings」と案内を出す。
4. 実機での確認(未)。特に、Shift を使う記号(`_` `:` `~`)と矢印キーが期待どおり届くか。

### 接続設定画面の作り

```
┌ Connection ─────────────────────┐
│ Connect to   < Network >        │  ← ←/→ か Enter で切り替え
│ Host         192.168.1.50       │  ← Network のときだけ表示
│ Port         7125               │
│ API key      ***                │
│        Save & connect           │
│ [READY] Klipper ready           │  ← 接続状態、または保存結果・入力エラー
└─────────────────────────────────┘
```

- どの画面からでも `S` で開く。`ESC` で状態表示に戻る。
- 「This device」を選ぶと Host / Port / API key が隠れ、代わりに Socket(ソケットパス)が出る。
  最初の案では詳細設定に隠す予定だったが、行を接続先に合わせて出し分けるほうが単純なのでそうした。
- `Enter` で入力を始め、もう一度 `Enter` で確定、`ESC` で取り消す。
  入力中はすべてのキーが文字になるので、`D`(テーマ切替)や数字キー(画面切替)は反応しない。
- 値の検証は設定ファイルの読み込みと同じ処理を使う(例: ポートは 1〜65535)。
- 「Save & connect」で、変わった項目だけをユーザー設定に保存し、その場でつなぎ直す。
- 画面の行は `src/app/settings_form.cpp` の `connection_settings_fields()` の表から作る。
  設定項目を画面に出すときは、この表に 1 行足す。画面のコードは変えなくてよい。
- 文字入力は `lv_textarea` ではなく自前で処理している。
  キーはすべて `ShortcutController` を通るので、そこから画面に先に渡す仕組み
  (`platform::set_screen_key_handler`)を足した。

### 後回しでよいもの

- プリンターの IP が変わる問題への対策。ルーターで IP を固定するか、mDNS(`_moonraker._tcp`)で自動探索する。
- 接続先を複数登録して切り替える機能。
- systemd で動かすときに `HOME` が想定と違う場合の、ソケットパスの扱い。
