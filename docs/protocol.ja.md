# UDP プロトコル

[English](protocol.md) | [简体中文](protocol.zh-CN.md) | **日本語**

## トランスポート

```text
UDP
127.0.0.1:39570
UTF-8 JSON
version: 1
fire-and-forget datagrams
accepted datagram size: less than 8192 bytes
```

ドライバーはループバックだけにバインドします。送信側では、`sendto` の成功をローカルソケットでの送信成功として扱います。

## ドライバーコマンドログ

処理したポーズデータグラムがコマンドの要求内容を変更したとき、ドライバーはベストエフォート方式のテレメトリーデータグラムを送信します。ローカル IPv4 マルチキャストグループへ 1 つのデータグラムを送るため、コンパニオン UI と複数の独立したアプリケーションプロセスが同じレポートを受信できます。

```text
UDP
multicast group: 239.255.39.71
port: 39571
interface: 127.0.0.1 (loopback only)
UTF-8 JSON
logging version: 1
maximum logging datagram size: 65507 bytes
```

同じグループには複数種類のイベントが流れます。すべてのデータグラムは共通のエンベロープを持ち、`event` フィールドによって識別されます。レシーバーはこのフィールドでイベントを絞り込みます。エンベロープ、イベントの一覧、および両者を安定させる互換性規則については、[イベントスキーマ](#イベントスキーマ)を参照してください。

ロググループはコマンドレシーバーから独立しています。`steamvr.vrsettings` の `driver_anyadance` セクションで設定し、変更の適用後に SteamVR を再起動してください。

```json
"driver_anyadance": {
    "command_log_enabled": true,
    "command_log_multicast_group": "239.255.39.71",
    "command_log_port": 39571,
    "haptic_log_enabled": true
}
```

`command_log_multicast_group` は IPv4 マルチキャストアドレスでなければなりません。各ローカル購読者は、それぞれ個別にそのグループとポートへ参加します。グループを変更すると、別のリスナー一式がレポートを受信できます。`command_log_enabled` はレポートの生成を制御します。これらの値の既定値は `resources/settings/default.vrsettings` に含まれるエントリです。ドライバーは常にループバックインターフェイスとマルチキャスト TTL 0 を選ぶため、レポートがマシン外へ出ることはありません。

付属の PowerShell リスナーは、動作する参照実装です。

```powershell
# Monitor reports until Ctrl+C.
.\scripts\listen_driver_log.ps1

# Launch three independent listener processes and require every one to receive
# the same locally generated multicast probe.
.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3
```

## レシーバーの実装

どのローカルプロセスでも購読できます。ドライバーへの登録は不要で、コンパニオン UI を実行する必要もありません。ドライバーはリスナーの有無に関係なくマルチキャストを行います。UI の **Monitor driver commands** スイッチは、UI 自身がグループへ参加するかどうかだけを制御します。

ソケットでは、次の 4 手順をこの順序で行います。どれかを省略しても明示的なエラーにならない場合があり、ソケットは開いてバインドも成功したように見えますが、データグラムは一切届きません。

1. **UDP ソケットを作成**します（`AF_INET`、`SOCK_DGRAM`）。
2. **バインド前に `SO_REUSEADDR` を設定**します。設計上、複数のプロセスがこのポートを共有します。設定しない場合、後から起動したプロセスはバインドに失敗するか、何も受信しません。
3. グループアドレスではなく、**`0.0.0.0` のポート `39571` へバインド**します。マルチキャストアドレス自体へのバインドは一部のプラットフォームで動作しますが、Windows では動作しません。
4. **`127.0.0.1` インターフェイスで `239.255.39.71` へ参加**します（`IP_ADD_MEMBERSHIP`）。インターフェイスの指定は重要です。ドライバーはループバックから TTL 0 で送信するため、他のインターフェイスで参加してもトラフィックを受信できません。

その後、データグラムを読み、UTF-8 をデコードし、JSON を解析して `event` で絞り込みます。次のレシーバーは現在の 2 種類のイベントを処理し、未知のイベントを読み飛ばし、シーケンス番号から損失を報告します。

```python
import datetime
import json
import socket
import struct

GROUP = "239.255.39.71"
PORT = 39571
INTERFACE = "127.0.0.1"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
# Before bind: lets other receivers share the port.
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
# Bind the port on any address, not on the group address.
sock.bind(("", PORT))
# Join on the loopback interface: the driver sends with TTL 0 from 127.0.0.1.
sock.setsockopt(
    socket.IPPROTO_IP,
    socket.IP_ADD_MEMBERSHIP,
    struct.pack("=4s4s", socket.inet_aton(GROUP), socket.inet_aton(INTERFACE)),
)

expected = None
while True:
    datagram, _ = sock.recvfrom(65507)
    try:
        event = json.loads(datagram.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        continue

    if event.get("version") != 1:
        continue  # a version this receiver does not know

    sequence = event.get("sequence")
    if expected is not None and sequence > expected:
        print(f"  (lost {sequence - expected} event(s))")
    expected = sequence + 1

    # Driver wall clock. Absent from a sender predating the field.
    stamp = event.get("timestamp_ms")
    when = (
        datetime.datetime.fromtimestamp(stamp / 1000, datetime.timezone.utc)
        .astimezone()
        .strftime("%H:%M:%S.%f")[:-3]
        if stamp
        else "--:--:--.---"
    )

    name = event.get("event")
    if name == "haptic_vibration":
        haptic = event["haptic"]
        print(
            f"{when} #{sequence} haptic {event['device']}: "
            f"{haptic['duration_seconds']:.3f}s "
            f"{haptic['frequency_hz']:.1f}Hz "
            f"amplitude {haptic['amplitude']:.2f}"
        )
    elif name == "command_processed":
        command = event["command"]
        print(
            f"{when} #{sequence} command from {event['source']['host']}: "
            f"{'accepted' if command['accepted'] else 'rejected'} - {event['detail']}"
        )
    else:
        # An event type added after this receiver was written. The envelope is
        # common to every event, so it is still safe to read and skip.
        print(f"{when} #{sequence} {name} - {event['detail']}")
```

この例は意図的に最小限にしてあります。本番環境で使用する前に、[順序と配信](#順序と配信)および[スキーマの安定性](#スキーマの安定性)を参照してください。レシーバーは到着順ではなく `sequence` に従ってイベントを配置し、重複を破棄する必要がありますが、この例ではそこまで実装していません。

### データグラムが届かない場合

ソケットが正常に開いただけでは何も証明できないため、次の順に確認してください。

- **ドライバーは動作していますか？** レポートは SteamVR がドライバーを読み込んでいる間だけ生成されます。SteamVR の Web コンソールで `driver_anyadance` を確認するか、SteamVR のドライバーログで `Command logging multicasts on loopback` の行を探してください。
- **`127.0.0.1` で参加しましたか？** 既定のインターフェイスや LAN インターフェイスで参加することが、何も届かないソケットの最も一般的な原因です。
- **グループアドレスではなく `0.0.0.0` にバインドしましたか？**
- **`SO_REUSEADDR` はバインド後ではなく、バインド前に設定しましたか？**
- **レポートがオフになっていませんか？** `steamvr.vrsettings` の `command_log_enabled` と `haptic_log_enabled` はどちらも既定で `true` です。変更後は SteamVR の再起動が必要です。
- **グループとポートは一致していますか？** `command_log_multicast_group` または `command_log_port` を変更した場合、購読者側も合わせる必要があります。
- **トラフィック自体は存在しますか？** `.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3` を実行してください。このコマンドは独自のマルチキャストプローブを生成します。成功すればこのマシン上のグループは機能しており、問題はレシーバー側にあります。失敗した場合はマシンのマルチキャスト設定に問題があります。

## イベントスキーマ

グループ上の各データグラムが 1 つのイベントです。種類に関係なくすべてのイベントが次のエンベロープを共有するため、レシーバーはイベント固有の形式を知らなくても、未知のイベントを含め、どのイベントでも識別、絞り込み、並べ替え、表示できます。

```json
{
  "version": 1,
  "event": "<event name>",
  "sequence": 42,
  "timestamp_ms": 1700000000123,
  "suppressed": 0,
  "detail": "compact English summary"
}
```

| エンベロープフィールド | 型 | 意味 |
| --- | --- | --- |
| `version` | 数値 | ドライバーログプロトコルのバージョン。現在はバージョン 1 です。レシーバーが認識できない `version` のイベントは無視する必要があります。 |
| `event` | 文字列 | **絞り込みに使うフィールドです。** イベントの種類を示し、エンベロープと並ぶ種類固有フィールドの形式を決定します。 |
| `sequence` | 数値 | イベント種類ごとではなく、ドライバーが送信する*すべて*のイベントを通して単調増加するため、全種類のイベントが 1 つの順序を共有します。ドライバーの起動時にリセットされます。欠番はデータグラムの損失、または上流でのイベント種類の除外を意味します。 |
| `timestamp_ms` | 数値 | ドライバーがイベントを送信した時刻。Unix エポックからのミリ秒数（UTC）です。`sequence` と同時にウォールクロックから取得されるため、データグラムの到着時刻ではなく、イベントの発生時刻を表します。**これは「いつ」を示すものであり、「どの順序」を示すものではありません。** 詳しくは後述します。任意フィールドであり、存在しない場合は送信側が指定していないことを意味します。 |
| `suppressed` | 数値 | 同じ種類の直前のレポートから今回までに吸収された同一イベントの数。任意フィールドであり、省略時は `0` です。抑制を行わないイベント種類では常に `0` です。 |
| `detail` | 文字列 | イベントの短い英語要約。常に存在するため、レシーバーは種類固有フィールドを解析せずに任意のイベントを 1 行で表示できます。人間が読むためだけの値であり、解析しないでください。 |

各イベントはエンベロープに加えて、種類固有のフィールドを持ちます。

| `event` | 種類固有フィールド | 説明 |
| --- | --- | --- |
| `command_processed` | `source`、`command` | [ドライバーコマンドログ](#ドライバーコマンドログ) |
| `haptic_vibration` | `device`、`haptic` | [ハプティックイベント](#ハプティックイベント) |

### 順序と配信

配信には通常の UDP を使用するため、トランスポートは何も保証しません。データグラムは損失、重複、順序の入れ替わりが起こり得ます。それを回復可能にするのが `sequence` であり、イベント種類ごとではなく全体でカウントする理由でもあります。

ドライバーはできる限り遅い時点、データグラムをソケットへ渡す直前にシーケンス番号を取得します。番号の取得と送信を 1 つのアトミック操作にはできません。このグループへは 2 つのスレッドがレポートします。`command_processed` を扱う UDP レシーバースレッドと、`haptic_vibration` を扱う SteamVR の `RunFrame` スレッドです。両者を直列化すると、どちらの高頻度処理にもロックが入ります。そのため、ネットワークの動作とは無関係に、2 つのイベントが番号とは逆の順序で送信される小さな時間窓が残ります。

したがって、レシーバーは到着順ではなく `sequence` で並べる必要があります。

- **並べ替え。** 到着時に末尾へ追加するのではなく、`sequence` に従って保持または挿入します。実際には遅れたイベントは数件後ろにある程度なので、有界のウィンドウで十分です。コンパニオン UI は最大 64 行前まで遡ります。
- **重複排除。** 同じ `sequence` の再出現は新しいイベントではなく、重複したデータグラムです。破棄してください。
- **損失の検出。** 欠けた `sequence` はデータグラムの損失を示します。カウンターは全体で共有されるため、1 種類のイベントだけを抽出するレシーバーでも欠番が見えます。保持する番号はまばらになります。自分で除外した番号を記録し、損失と区別してください。
- **`timestamp_ms` でも並べ替えないでください。** これはウォールクロックの値なので、NTP 補正、手動での時刻変更、仮想マシンのレジュームによって逆行する可能性があり、2 つのイベントが同じミリ秒になることもあります。発生時刻の表示と間隔の計測には使えますが、順序付け、重複排除、損失検出には `sequence` を使ってください。両者が矛盾する場合は `sequence` が正しい値です。
- **到着時刻や `detail` で並べ替えないでください。** ドライバーがイベントを生成した順序を表すのは `sequence` だけです。

再送は行われず、確認応答も読み取りません。そのため、失われたイベントは戻りません。これはテレメトリーであり、高負荷時にドライバーを遅延させるのではなく、破棄できるように設計されています。

### スキーマの安定性

次の規則は契約です。これらを守るレシーバーは、AnyaDance のリリースが変わっても修正なしで動作し続けます。

- **`event` で絞り込みます。** 他のフィールドの有無から種類を推測しないでください。残りの形式を決定するフィールドは `event` だけです。
- **未知の `event` 値を無視します。** このグループにはどのリリースでも新しいイベント種類が追加される可能性があります。未知のイベントはレシーバーが処理しない通常のイベントであり、壊れたデータグラムでもプロトコルエラーでもありません。完全なエンベロープを持つため、汎用的なログ記録や表示はできます。
- **未知のフィールドを無視します。** エンベロープまたは既存イベントの種類固有オブジェクトに、新しいフィールドが追加される可能性があります。レシーバーはそれらを理由に失敗してはいけません。
- **同じ `version` 内では、既存フィールドは変わりません。** 公開後はフィールドの名前、型、意味が固定されます。フィールドの削除、用途変更、取り得る値の範囲縮小には新しい `version` が必要です。
- **任意フィールドが存在しない場合は、文書化された既定値を意味します。** エラーではありません。
- **`detail` を解析しないでください。** 文言は契約の一部ではなく、いつでも変更される可能性があります。種類固有フィールドを読み取ってください。

`scripts/listen_driver_log.ps1` の参照リスナーはこれらの規則に従っており、最も短い動作例です。

### `command_processed`

各パケットは次の形式です。

```json
{
  "version": 1,
  "event": "command_processed",
  "sequence": 42,
  "suppressed": 613,
  "detail": "accepted 2 device entries; clamped Y for 1",
  "source": {
    "host": "127.0.0.1",
    "port": 54321
  },
  "command": {
    "protocol": "pose_frame",
    "bytes": 347,
    "accepted": true,
    "devices": ["hmd", "left_controller"],
    "y_clamped": ["hmd"],
    "payload": "{\"version\":1,...}"
  }
}
```

エンベロープフィールドは[イベントスキーマ](#イベントスキーマ)で説明しています。このイベントの `suppressed` は、直前の `command_processed` レポートから今回までに吸収された同一コマンドの数を表し、`detail` は処理結果を要約します。種類固有フィールドは次のとおりです。

| フィールド | 意味 |
| --- | --- |
| `source.host`、`source.port` | 元のポーズデータグラムをポート `39570` へ送信したエンドポイント。 |
| `command.protocol` | コマンドプロトコル名。バージョン 1 では `pose_frame`。 |
| `command.bytes` | 元のコマンドデータグラムのバイト長。 |
| `command.accepted` | 認識対象のデバイスエントリが 1 つ以上受理、保存された場合は `true`。 |
| `command.devices` | このデータグラムから受理された認識対象のデバイスエントリ。 |
| `command.y_clamped` | Y 値がクランプされた受理済みデバイスエントリ。 |
| `command.payload` | 調査および再送のために保持された、JSON 文字列としての元のコマンドデータグラム。 |

レポート用ソケットはノンブロッキングであり、テレメトリー配信は意図的に損失を許容します。ソケットバッファの満杯、無効なマルチキャスト設定、ローカル配信の失敗が起きるとレポートは破棄されますが、コマンド処理は続行します。シリアライズと送信は SteamVR の `RunFrame` 経路外にある UDP レシーバースレッドで実行されます。購読者数に関係なく、ドライバーはレポート対象のコマンドごとに 1 回だけ送信します。

### 保持中のコマンド

保持中のポーズをストリーミングする送信側は、同じコマンドをストリームレートで繰り返します。すべてのデータグラムをレポートすると、何も変化していなくても毎秒 `kStreamRateHz` 件のレポートが発生します。そのため、ドライバーは直前にレポートしたものとコマンドが異なる場合だけレポートします。受理結果、認識されたデバイスエントリの集合、クランプされたエントリの集合、および存在する各デバイスのポーズとコントローラー入力がすべて一致すると、繰り返しと判定します。到着時刻は比較対象に含まれず、比較は厳密です。保持中のポーズは変化していない状態から再度シリアライズされるため同一になりますが、ゆっくり意図的に動かした場合は各フレームがレポートされます。検証に失敗したデータグラムは信頼できる解析済み状態を残さないため、代わりにバイト列で比較します。

抑制は変更フィルターであり、レート制限ではありません。異なるコマンドは常にレポートされるため、移動シーケンスを記録するリスナーはすべてのフレームを受け取ります。吸収された繰り返しの数は次のレポートの `suppressed` フィールドへ付加されるので、保持中のポーズと停止した送信側を区別できます。

UI の **Monitor driver commands** スイッチは既定でオフであり、選択内容は `%LOCALAPPDATA%\AnyaDance\ui_state.ini` に保存されます。切り替えると、既定のマルチキャストグループへただちに参加または退出します。リスナーが参加している間、コマンド成功行ではドライバーレポートが正しい情報源となり、UI 自身の送信成功行は抑制されます。ドライバーは受け取っていないコマンドをレポートできないため、ローカルソケットの失敗は引き続き表示されます。同じ送信元から高速で届く受理済みレポートは 100 ms の表示ウィンドウに集約され、拒否されたレポートは個別の行になります。集約は UI 表示だけに限定され、ドライバーの保持中コマンド抑制に重ねて適用されます。そのため、移動中のポーズは 100 ms ごとに 1 行、保持中のポーズは行なしとなります。レポートに 0 以外の `suppressed` が含まれている場合、その行には直前まで保持されていた同一コマンド数が表示されます。

## ハプティックイベント

2 台の仮想コントローラーは、実物の Index コントローラーと同様に `/output/haptic` コンポーネントを公開するため、SteamVR はハプティック要求をそれらへルーティングします。ドライバーにモーターはなく、実際の振動は再生しません。外部ツールがインジケーターを駆動したり物理ハードウェアへ転送したりできるよう、要求をレポートするだけです。

レポートはコマンドレポートと同じマルチキャストグループおよびポートへ送られるため、リスナーは 1 つのグループへ参加するだけで両方を監視できます。`driver_anyadance` セクションの `haptic_log_enabled` が送信を制御し、既定値は `true` です。`/output/haptic` コンポーネント自体は常に作成されるため、レポートをオフにしても SteamVR やゲームから見える状態は変わりません。SteamVR の `RunFrame` スレッド上で行うレポートはベストエフォートかつノンブロッキングです。ソケットバッファが満杯の場合やリスナーがいない場合は、フレームを遅延させずにレポートを破棄します。確認応答は期待せず、読み取りません。

```json
{
  "version": 1,
  "event": "haptic_vibration",
  "sequence": 7,
  "suppressed": 0,
  "detail": "0.125 s at 160.5 Hz, amplitude 0.75",
  "device": "right_controller",
  "haptic": {
    "duration_seconds": 0.125,
    "frequency_hz": 160.5,
    "amplitude": 0.75
  }
}
```

エンベロープフィールドは[イベントスキーマ](#イベントスキーマ)で説明しています。このイベントは繰り返しを抑制しないため、`suppressed` は常に `0` です。種類固有フィールドは次のとおりです。

| フィールド | 意味 |
| --- | --- |
| `device` | 振動を要求されたコントローラー。`left_controller` または `right_controller`。 |
| `haptic.duration_seconds` | 要求されたパルスの長さ（秒）。`0` は正当な停止要求です。 |
| `haptic.frequency_hz` | 要求された振動周波数（Hz）。 |
| `haptic.amplitude` | 要求された強さ。`0.0`～`1.0`。 |

値は SteamVR から渡されたまま使用します。有限でない数値を拒否することを除き、ドライバー独自のポリシーは適用しません。

ハプティックだけを必要とするレシーバーは、`event` が `haptic_vibration` のデータグラムを保持し、後から追加されたイベント種類を含む残りを無視します。

## 座標系の前提

位置の単位はメートルで、このドライバーに対して SteamVR が想定するドライバーポーズ座標空間を使用します。クォータニオンの順序は XYZW です。

```json
"rotation_xyzw": [x, y, z, w]
```

## パケット形式

AnyaDance コンパニオンは、すべてのデータグラムで完全なスナップショットを送信します。6 台すべてのデバイスエントリと、両コントローラーの入力エントリが含まれます。`finger_bends` は指データがある場合だけ送信されます。最小構成の第三者送信側は、認識対象の有効なデバイスエントリが 1 つ以上あれば、それより少ないデバイスだけを含めることもできます。

```json
{
  "version": 1,
  "devices": {
    "hmd": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.0, 1.5, 0.0],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "left_controller": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [-0.26, 1.10, -0.54],
        "rotation_xyzw": [0.77, 0.10, -0.16, 0.61]
      }
    },
    "right_controller": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.27, 1.57, -0.54],
        "rotation_xyzw": [0.77, -0.10, 0.16, 0.61]
      }
    },
    "hip": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.0, 1.07, -0.05],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "left_foot": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [-0.09, 0.26, 0.10],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "right_foot": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.09, 0.26, 0.10],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    }
  },
  "inputs": {
    "left_controller": {
      "trigger_click": false,
      "trigger_value": 0.0,
      "menu_click": false,
      "system_click": false,
      "a_click": false,
      "b_click": false,
      "grip_click": false,
      "grip_value": 0.0,
      "joystick_x": 0.0,
      "joystick_y": 0.0,
      "trackpad_x": 0.0,
      "trackpad_y": 0.0,
      "finger_bends": {
        "thumb": 0.0,
        "index": 0.0,
        "middle": 0.0,
        "ring": 0.0,
        "pinky": 0.0
      }
    },
    "right_controller": {
      "trigger_click": false,
      "trigger_value": 0.0,
      "menu_click": false,
      "system_click": false,
      "a_click": false,
      "b_click": false,
      "grip_click": false,
      "grip_value": 0.0,
      "joystick_x": 0.0,
      "joystick_y": 0.0,
      "trackpad_x": 0.0,
      "trackpad_y": 0.0
    }
  }
}
```

認識されるデバイス ID は、正確に次の 6 つです。

```text
hmd
left_controller
right_controller
hip
left_foot
right_foot
```

パーサーは認識対象のデバイス ID とフィールドを使用します。認識対象でも不正なデバイスエントリは読み飛ばしますが、同じパケット内にある他の有効な認識対象エントリは引き続き使用できます。

## 必須デバイスフィールド

各デバイスエントリには次のフィールドが必要です。

```json
{
  "valid": true,
  "connected": true,
  "pose": {
    "position": [0.0, 0.0, 0.0],
    "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
  }
}
```

| フィールド | 型 | 要件と動作 |
| --- | --- | --- |
| `valid` | 真偽値 | エントリの解析に必須です。バージョン 1 は起動後のすべての仮想デバイスを有効に保つため、現在のドライバーはこの値に関係なく SteamVR へ `true` を報告します。 |
| `connected` | 真偽値 | エントリの解析に必須です。バージョン 1 は起動後のすべての仮想デバイスを接続済みに保つため、現在のドライバーはこの値に関係なく SteamVR へ `true` を報告します。 |
| `pose.position` | 3 数値の配列 | 必須です。単位はドライバーポーズ空間のメートルで、各成分は有限かつ `-30.0`～`30.0` の範囲内でなければなりません。Y は `hmd` では `0.0`～`25.0`、それ以外のすべてのデバイスでは `-30.0`～`25.0` にクランプされます。 |
| `pose.rotation_xyzw` | 4 数値の配列 | 必須です。XYZW 順のクォータニオンで、値は有限、長さの 2 乗は `0.5`～`1.5` の範囲内でなければなりません。受理された値は正規化されます。 |

`inputs` オブジェクトは任意です。OpenVR のコントローラー状態へ影響するのは、`left_controller` と `right_controller` の入力エントリだけです。コントローラー入力エントリ内の各メンバーは任意で、省略したメンバーには次の既定値またはフォールバックを使用します。入力エントリは、同じデータグラムの `devices` に対応する有効なコントローラーエントリがある場合だけ適用されます。デバイスエントリは存在するが入力エントリがない場合、通常のボタンと軸は既定値へリセットされます。デバイスエントリ自体がない、または不正な場合は、直前のポーズと入力が保持されます。

| 入力フィールド | 型 | 範囲／既定値 | ドライバーの動作 |
| --- | --- | --- | --- |
| `trigger_click` | 真偽値 | 既定値 `false` | `/input/trigger/click` を駆動します。 |
| `trigger_value` | 数値 | `0.0`～`1.0` にクランプ。`trigger_click` が true の場合の既定値は `1.0`、それ以外は `0.0` | `/input/trigger/value` を駆動します。 |
| `menu_click` | 真偽値 | 既定値 `false` | `/input/application_menu/click` を駆動します。 |
| `system_click` | 真偽値 | 既定値 `false` | バージョン 1 のパケットで保持されます。 |
| `a_click` | 真偽値 | 既定値 `false` | `/input/a/click` を駆動します。 |
| `b_click` | 真偽値 | 既定値 `false` | `/input/b/click` を駆動します。 |
| `grip_click` | 真偽値 | 既定値 `false` | `/input/grip/click` と `/input/grip/touch` を駆動します。 |
| `grip_value` | 数値 | `0.0`～`1.0` にクランプ。`grip_click` が true の場合の既定値は `1.0`、それ以外は `0.0` | `/input/grip/value` と `/input/grip/force` の両方を駆動します。 |
| `joystick_x`、`joystick_y` | 数値 | それぞれ `-1.0`～`1.0` にクランプ。既定値 `0.0` | `/input/thumbstick/x` と `/input/thumbstick/y` を駆動します。 |
| `trackpad_x`、`trackpad_y` | 数値 | それぞれ `-1.0`～`1.0` にクランプ。省略した軸は対応するジョイスティック軸へフォールバック | `/input/trackpad/x` と `/input/trackpad/y` を駆動します。 |
| `finger_bends` | オブジェクト | 任意。省略時は最後に適用された曲げ量を保持（最初の有効なオブジェクトまでは開いた状態） | 手のスケルトンを駆動します。存在する場合、下記 5 つのメンバーがすべて正常に解析できなければ、オブジェクト全体を無視します。 |
| `finger_bends.thumb`、`.index`、`.middle`、`.ring`、`.pinky` | 数値 | それぞれ `0.0`（開く）～`1.0`（完全に曲げる）にクランプ | `/input/skeleton/left` または `/input/skeleton/right` の対応する指へ適用します。 |

## 検証

ドライバーは次のパケットを拒否します。

- `version` がない、または正しくない
- JSON の形式が不正
- データグラムサイズが 8192 バイト以上
- 有効で認識対象のデバイスが 0 台

デバイスエントリに次の問題がある場合、そのエントリは無視されます。

- 必須フィールドがない
- 位置またはクォータニオンの値が有限でない
- 位置成分の絶対値が `30.0 m` を超える
- クォータニオンの長さの 2 乗が許容範囲 `0.5`～`1.5` の外にある

受理されたクォータニオンは使用前に正規化されます。

## 位置の制限

すべての位置成分は有限で `±30 m` の範囲内でなければなりません。デバイスの Y 座標は、コンパニオン UI とドライバーの両方によってさらに `25 m` を上限として制限されます。

Y 座標の下限はデバイスごとに異なります。`0 m` 以上に保たれるのは `hmd` だけです。これはプレイスペース上の頭であり、地面より下に置くと視点が地中へ入るためです。他の 5 台は正当に地面より下へ移動する場合があります。たとえば、足が床面を通過する、腰を床へ近づける動き、原点が地面より上にある計算済みモーションなどです。そのため、これらの Y 座標は共有の位置範囲 `±30 m` だけで制限されます。

| デバイス | Y の範囲 |
| --- | --- |
| `hmd` | `0 m`～`25 m` |
| `left_controller`、`right_controller`、`hip`、`left_foot`、`right_foot` | `-30 m`～`25 m` |

## ポーズの継続性

6 台すべての仮想デバイスは、最初のパケットが届く前からニュートラルポーズで接続済みかつ有効です。パケットが到着すると、各デバイスについて最新の有効なパケットがポーズとコントローラー入力を更新します。パケットが停止しても、SteamVR からはデバイスが最後のポーズで接続済みかつ有効に見え続けます。

受理されたデバイスサンプルは、接続済み、有効、かつ `TrackingResult_Running_OK` として報告されます。

## コントローラー入力

ドライバーは Valve Index 互換のコントローラーコンポーネントを公開します。

```text
/input/trigger/click
/input/trigger/value
/input/application_menu/click
/input/a/click
/input/b/click
/input/grip/click
/input/grip/touch
/input/grip/value
/input/grip/force
/input/thumbstick/x
/input/thumbstick/y
/input/trackpad/x
/input/trackpad/y
/input/skeleton/left
/input/skeleton/right
```

`grip_click` は `/input/grip/touch` を、`grip_value` は `/input/grip/force` を駆動します。スケルトンコンポーネントは `finger_bends` からトランスフォームを生成します。
