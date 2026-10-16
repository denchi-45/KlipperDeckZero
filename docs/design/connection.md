# Moonraker 接続

最終更新: 2026-10-16

## 現状

2026-10-16 時点の `dev` での動作。

### 接続先の決め方

接続先は設定ファイルで固定。自動探索はない。起動時に次の順で決まり、後のものが前のものを上書きする。

1. 組み込みの既定値(`src/app/application_config.h`)
   - 接続方式: 実機は `unix`、デスクトップシミュレーターは `websocket`
   - `socket_path=~/printer_data/comms/moonraker.sock`
   - `url=ws://127.0.0.1:7125/websocket`
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
- `websocket`: `url` に接続する。`api_key` を設定していれば `X-Api-Key` ヘッダーで送る。

接続先を変える手段は、設定ファイルを編集して再起動することだけ。画面から変える手段はまだない。

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

### 接続設定画面(提案。未着手)

ブランチ案: `feature/connection-settings`

1. 実機でも接続方式の既定値を `websocket` にする。
2. 接続設定画面を作る。
   ```
   ┌ 接続設定 ─────────────────┐
   │ IPアドレス  [192.168.1.50 ] │
   │ ポート      [7125         ] │
   │                            │
   │ 状態: ● 接続済み            │
   │      [保存して接続]          │
   │ ▸ 詳細設定(APIキー等)      │
   └────────────────────────────┘
   ```
   - 物理キーボードで入力する(画面上のキーボードは不要)。LVGL の `lv_textarea` を使う。
   - Enter で保存し、その場でつなぎ直す(`PrinterService` を作り直す。再起動は不要)。
   - 接続できたか、エラーの内容は何かを表示する。
   - 詳細設定には、接続方式(unix / websocket)、ソケットパス、API キーを置く。
3. 保存処理を拡張する。今は `dark_mode` しか書き戻せないので、`[moonraker]` セクションも保存する。
4. 初回起動時、接続先が未設定ならこの画面を最初に表示する。
5. IP アドレスだけでなく、ホスト名(例: `mainsailos.local`)も入力できるようにする。

### 後回しでよいもの

- プリンターの IP が変わる問題への対策。ルーターで IP を固定するか、mDNS(`_moonraker._tcp`)で自動探索する。
- 接続先を複数登録して切り替える機能。
- systemd で動かすときに `HOME` が想定と違う場合の、ソケットパスの扱い。
