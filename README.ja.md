# AnyaDance

<p>
  <img src="docs/images/ui_main.png" alt="AnyaDance メイン UI" width="50%"><img src="docs/images/ui_mmd.png" alt="AnyaDance MMD UI" width="50%">
</p>

[English](README.md) | [简体中文](README.zh-CN.md) | **日本語**

AnyaDance は、VRChat アバターの全身を操作してアニメーションさせる Windows 用ツールキットです。手動でポーズを付けたり、リアルタイムに操作したり、MMD ダンスを再生したりできます。中核となるのは SteamVR/OpenVR 仮想デバイスドライバーと、そこへデータをストリーミングするコンパニオン UI（`AnyaDance.exe`）です。ドライバーは、VRChat のフルボディテスト用に 6 台の仮想デバイスを公開します。

- HMD
- 左コントローラー
- 右コントローラー
- 腰トラッカー
- 左足トラッカー
- 右足トラッカー

ドライバーは `127.0.0.1:39570` で、UDP JSON によるポーズとコントローラー入力のフレームを受信します。コンパニオン実行ファイル `AnyaDance.exe` は、起動するとすぐに 6 台のデバイスで構成される T ポーズを 60 Hz でストリーミングし始めます。

AnyaDance は Pipira による大規模プロジェクト **Project Anya** の一部として公開されています。Project Anya 自体はプロプライエタリです。このドライバーを使用、または基にして開発する際に Project Anya のクレジットを記載していただけると幸いです。

## 免責事項

本ソフトウェアは、正当かつ許可されたテストおよび開発のみを目的として提供されます。

ライブのオンラインゲームへ仮想デバイスや偽装したトラッキング情報を入力すると、そのゲームの利用規約に違反する可能性があります。また、アンチチートシステムに検出され、アカウントの一時停止または永久停止につながる可能性があります。

ドライバーを登録すると SteamVR の構成が変更されます。AnyaDance の完全仮想 HMD、コントローラー、トラッカーが有効化され、`steamvr.vrsettings` が書き換えられます。登録時にバックアップが作成され、登録解除時に元の構成が復元されます。仮想 HMD は SteamVR コンポジターを通じて左右の目を継続的に描画するため、追加の GPU と CPU リソースを消費します。レンダー解像度を上げると、その負荷も増加します。

本ソフトウェアは、すべて自己責任で使用してください。本ソフトウェアは明示・黙示を問わずいかなる保証もなく「現状有姿」で提供され、作者はアカウントの停止やアクセス権の喪失を含め、使用または誤用によって生じるいかなる結果についても責任を負いません。

本プロジェクトは VRChat、Valve、Steam、SteamVR のいずれとも提携しておらず、それらから承認を受けたものでもありません。すべての商標は各権利者に帰属します。

全文は [DISCLAIMER.md](DISCLAIMER.md) を参照してください。これは安全上の確認事項であり、追加のライセンス条件ではありません。コンパニオン UI は初回起動時に同意を求めます。

## 状態

コードは Windows と Visual Studio 2022 でビルドでき、自動テストに合格しています。配布されるツールは SteamVR ドライバーを登録し、6 台の仮想デバイスをストリーミングして、同じビルドからドライバーバンドルを作成します。

## 必要な環境

- Windows 10 以降
- SteamVR
- [Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)](https://aka.ms/vc14/vc_redist.x64.exe)

ソースからビルドする場合は、さらに次のものが必要です。

- Visual Studio 2022、または「C++ によるデスクトップ開発」ワークロードを含む Visual Studio Build Tools
- CMake 3.22 以降
- ローカルの依存関係パスを指定しない場合、初回の標準ビルドにネットワーク接続

固定されている依存関係：

- Valve OpenVR SDK `2.2.3`
- Dear ImGui `v1.90.9`

## リリース版のインストール

1. [GitHub Releases](https://github.com/anyapipira/AnyaDance/releases) ページから `AnyaDance-<version>-windows-x64.zip` をダウンロードします。
2. `anyadance` フォルダー全体を今後も保持する場所へ展開します。ZIP 内から実行ファイルを直接起動しないでください。また、ドライバーが登録されている間はフォルダーを移動または削除しないでください。
3. `AnyaDance.exe` を起動し、安全上の注意事項を確認して同意した後、**Register Driver** をクリックします。
4. **Restart SteamVR** をクリックします。SteamVR は AnyaDance の HMD、コントローラー、トラッカーを使用する完全仮想モードで起動します。

使用を終えたら、フォルダーを移動または削除する前に **Unregister Driver** をクリックし、SteamVR を再起動してください。これにより SteamVR の設定バックアップが復元され、実デバイスのトラッキングへ戻ります。詳しくは[インストール](docs/installation.ja.md)を参照してください。

## ビルド

```powershell
.\scripts\build_driver.ps1
```

出力：

```text
build\out\anyadance\AnyaDance.exe
build\out\anyadance\driver.vrdrivermanifest
build\out\anyadance\bin\win64\driver_anyadance.dll
build\out\anyadance\resources\...
build\out\anyadance\LICENSE
build\out\anyadance\NOTICE
build\out\anyadance\THIRD_PARTY_NOTICES.md
build\out\anyadance\DISCLAIMER.md
build\out\anyadance\TRADEMARKS.md
build\out\anyadance\README.md
build\out\anyadance\README.zh-CN.md
build\out\anyadance\README.ja.md
build\out\AnyaDance.zip
```

UI はドライバーフォルダー内に配置されるため、`build\out\anyadance\` はライセンスと通知を含む配布可能な 1 つのバンドルになります。ビルド処理は他の人へ直接渡せる `build\out\AnyaDance.zip` も作成します。実行ファイルは自身が置かれているフォルダーを SteamVR ドライバーとして登録するため、OpenVR は同じ場所にある `driver.vrdrivermanifest` と `bin\win64\driver_anyadance.dll` を検出します。OpenVR は、ドライバールートにあるマニフェストを使って `bin\win64\` からドライバー DLL を読み込みます。

ローカルにある依存関係のチェックアウトを使用するには、次を実行します。

```powershell
.\scripts\build_driver.ps1 -OpenVRSdkRoot F:\deps\openvr -ImguiRoot F:\deps\imgui
```

## テスト

```powershell
cmake --build build --config Release --target anyadance_tests
ctest --test-dir build -C Release --output-on-failure
```

テストはプロトコル検証、安全クランプ、T ポーズへのリセット計算、キーボード操作、マウス操作の計算、MMD リマッピング、`.nya` クリップ処理、および UDP ログの動作を対象としています。

## 登録

```powershell
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

`register_driver.ps1` はドライバーを登録し、完全仮想モードの設定（仮想 HMD、コントローラー、トラッカーを有効化）を適用します。その前に `steamvr.vrsettings` を `%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup` へバックアップします。

登録を変更した後、およびドライバー DLL を再ビルドした後は、SteamVR を再起動する必要があります。

> **使用後は登録を解除してください。** 登録すると AnyaDance の完全仮想デバイス一式が有効になります。元の SteamVR 設定へ戻す方法は[登録解除](#登録解除)を参照してください。

## HMD のレンダー解像度

仮想 HMD は既定で片目あたり `1920x1080` で描画し、SteamVR コンポジターの負荷を抑えます。上級ユーザーは 2 つの設定キーを上書きすることで、ドライバーを再ビルドせずに解像度を上げられます。

グローバルの `steamvr.vrsettings`（`<Steam>\config\steamvr.vrsettings`）を編集し、`driver_anyadance` セクションを追加または拡張します。

```json
"driver_anyadance": {
    "headset_render_width": 3840,
    "headset_render_height": 2160
}
```

その後、SteamVR を再起動します（`.\scripts\restart_steamvr.ps1`）。`3840x2160` は 4K です。モニターに合わせるなど、どのアスペクト比も使用できます。投影は設定されたレンダー解像度に適応するため、画像は引き伸ばされません。高解像度での GPU 描画時間は、おおむね倍率の 2 乗で増加します。4K は 1080p の約 4 倍です。

`steamvr.vrsettings` の値は `resources\settings\default.vrsettings` にあるドライバーの既定値より優先されます。同じセクションには、デスクトップのミラーウィンドウ用に `headset_window_width`、`headset_window_height`、`headset_window_eye_mode`、`headset_window_preserve_aspect` もあります。詳しくは[デバイスモデル](docs/device-model.ja.md)を参照してください。

ドライバーは処理済みの UDP コマンドも、既定ではループバック専用のマルチキャストグループへレポートします。グループとポートは同じ設定セクションで変更できます。

```json
"driver_anyadance": {
    "command_log_enabled": true,
    "command_log_multicast_group": "239.255.39.71",
    "command_log_port": 39571
}
```

UI の **Monitor driver commands** スイッチは初期状態でオフになっており、選択内容は次回起動時にも保持されます。切り替えると、既定のグループへただちに参加または退出します。複数のローカルアプリケーションが同時に購読できます。参照実装のリスナーを動かすには `.\scripts\listen_driver_log.ps1` を実行します。`-Validate -ListenerCount 3` を追加すると、独立した 3 つのレシーバープロセスを検証できます。独自のアプリケーションから購読する場合は、ソケットの設定、実例、何も届かない場合の確認事項について[レシーバーの実装](docs/protocol.ja.md#レシーバーの実装)を参照してください。パケットのスキーマと配信動作については[ドライバーコマンドログ](docs/protocol.ja.md#ドライバーコマンドログ)を参照してください。

## テスト UI の実行

```powershell
.\build\out\anyadance\AnyaDance.exe
```

UI の動作：

- 60 Hz の UDP ストリーミングを自動的に開始します
- 最小化またはフォーカスを失ってもストリーミングを続けます
- 正常終了時に、入力がニュートラルな最終フレームを送信します
- UDP ログを英語で保持し、ドライバーコマンドの監視を実行中に切り替えられます。監視中、ドライバーの処理レポートには UI と第三者の UDP 送信元から届いたコマンドが、送信成功行の重複なしで表示されます
- ポインターを合わせた行、または固定した行の詳細に、3 つのペイロード操作を表示します。Copy（元のリクエスト本文）、Copy resend command（実行可能な PowerShell UDP 1 行コマンド）、Resend（UI 自身のソケットからまったく同じデータグラムを再送）です
- UI のボタンから、自身のフォルダーを SteamVR ドライバーとして登録／登録解除し、確認後に SteamVR を再起動できます
- **Always on top** チェックボックスで他のウィンドウより手前に固定できます。選択内容は次回起動時にも保持されます
- MMD ダンスをリアルタイム再生できます。**Dance (MMD)** ボタンでダイアログを開き、`.vmd` モーションと `.pmx`／`.pmd` モデルを選びます。Analyze と Play を実行すると、ダンスが 6 台のデバイスへストリーミングされます（[MMD ダンス](docs/mmd-dance.ja.md)を参照）
- ポーズとダンスを `.nya` クリップとして保存および復元できます。メインウィンドウの **Save Pose**／**Load Pose** は現在のポーズを保存、復元します。Dance ダイアログでは、解析したダンスを **Save .nya** で保存し、**Load .nya** で再計算せずに再生できます
- `src/ui/localization.*` を通じて英語、簡体字中国語、日本語の UI 文字列に対応しています
- Windows のタイトルバーとフッターにビルドバージョンを表示します。GitHub のリリースビルドではリリースタグと同じ値です

キー割り当て：

```text
WASD  左ジョイスティックの移動
Q/E   右ジョイスティックの旋回
Space 押している間は右 A
M     押している間は右 B
V     押している間は左 A
Z     押している間は左トリガー
X     押している間は右トリガー
```

マウス操作には 6 つのデバイスボックスを使用します。キャプチャーパネルとボックスは UI ウィンドウに合わせてサイズが変わります。HMD ボックスでは回転に加えて、右ドラッグで垂直方向（Y）へ移動できます。左＋右の同時ドラッグでも同じ垂直移動ができます。他のデバイスでは、左ドラッグでローカル X/Y 方向へ移動し、中ドラッグで回転、右ドラッグで奥行き方向へ移動します。デバイスの Y 座標の上限は `25 m` で、下限が `0 m` に設定されているのは HMD だけです。他の 5 台は地面より下の `-30 m` まで移動できます。HMD／Global 座標系のラジオボタンで、HMD のヨーを基準に操作するか、固定されたワールド軸を使用するかを選びます。手と足の左右ペアにあるミラーチェックボックスも同じ座標系設定を使用します。HMD モードでは HMD のヨーに基づく YZ 平面を基準に、Global モードでは HMD の位置を中心とするワールド軸を基準に反射します。マウスホイールで両手の指を開閉します。数字キーを押しながらスクロールすると、1 本の指だけを曲げられます。`1`～`5` は左手の小指から親指、`6`～`0` は右手の親指から小指です（`5`／`6` が親指、`1`／`0` が小指）。各指は `[0, 1]` にクランプされ、一方向へ最後までスクロールするとすべての指が完全に開いた状態または閉じた状態へリセットされます。片手のすべての指を十分に曲げて拳を作ると、その手のグリップが押されます。いずれかの指を開くとグリップが解放され、VRChat のつかむ操作を駆動します。

ボディパネルの空いている領域を左ドラッグすると、右サムスティックとして機能します。押し始めた位置がスティックの中心となり、ドラッグすると各軸が ±1 の範囲で倒れ、離すとニュートラルへ戻ります。これは `M` を押し続けて開く右手用クイックメニューの操作を意図しています。空いている領域では、リグ全体も一度に操作できます。中ドラッグで HMD の位置を中心に 6 台すべてのデバイスをヨー／ピッチ回転し、中＋右ドラッグで同じ中心の周りにロール回転、右ドラッグで形を保ったままリグ全体を垂直移動します。下方向では HMD が `0 m` に達すると停止します。リグの回転はデバイスごとの操作と同じ HMD／Global 座標系設定を使用します。

## MMD ダンス

<p>
  <img src="docs/images/anya_dance.gif" alt="AnyaDance MMD ダンス再生" width="25%"><img src="docs/images/anya_dance.gif" alt="AnyaDance MMD ダンス再生" width="25%"><img src="docs/images/anya_dance.gif" alt="AnyaDance MMD ダンス再生" width="25%"><img src="docs/images/anya_dance.gif" alt="AnyaDance MMD ダンス再生" width="25%">
</p>

**Dance (MMD)** ボタンは、6 台の仮想デバイス上で MMD ダンスをメモリからリアルタイム再生します。Blender と MMD Tools は、ユーザーが指定したモデル（PMX/PMD）に対して `.vmd` モーションを計算します。UI はハードコードされたリグへ小規模なリマッピングを行い、60 Hz でストリーミングします。ダイアログで VMD とモデルを選び、Analyze、続いて Play を押します。Blender と MMD Tools を独自の場所へインストールしている場合は **Advanced** でパスを指定します。UI はこれらのパスを記憶します。

必要なもの：[Blender](https://www.blender.org/)、[MMD Tools](https://github.com/MMD-Blender/blender_mmd_tools) アドオン（どちらも自動検出）、およびご自身で用意したモデル。MMD モデルはそれぞれ独自のライセンスを持つ第三者の著作物です。詳しいパラメーターと実行時の動作については、[MMD ダンス](docs/mmd-dance.ja.md)を参照してください。

ダンスの解析後、**Save .nya** を押すと結果がクリップファイルへ保存されます。**Load .nya** はクリップを読み込み、すぐに Play を有効にします。読み込み時は Blender による計算とリマッピングの両方が省略されるため、保存済みのダンスを即座に再生できます。

## クリップファイル（.nya）

`.nya` ファイルは、デバイスレベルのフレーム（6 台のデバイスポーズと左右の手指の曲げ量）を格納する小さな JSON クリップで、追加変換なしにストリーミングできます。ポーズとアニメーションは同じ形式です。**ポーズ**は 1 フレームのクリップ（その 1 フレームを保持ループとして再生）、**アニメーション**（保存した MMD ダンスなど）は時刻情報を持つ複数フレームです。読み込み時には、デバイスの Y 座標が HMD では `0–25 m`、他の 5 台では `-30–25 m` に、指の曲げ量が `[0, 1]` にクランプされます。

## 安全性と継続動作

6 台すべてのデバイスで、Y 座標の上限は `25 m` です。下限が `0 m` に設定されているのは HMD だけです。HMD はプレイスペース上の頭なので、地面より下に置くと視点も地中へ入ります。他の 5 台は正当に地面より下へ移動する場合（足が床面を通過する、腰を床へ近づける動きなど）があるため、共有の位置範囲 `±30 m` だけで制限されます。UI はシリアライズ前にクランプし、ネイティブドライバーはパケット検証後に再度クランプします。

6 台すべてのデバイスは、接続済みかつ有効なニュートラルポーズで開始します。受理されたパケットは最新のポーズとコントローラー入力を更新します。パケットが停止しても、SteamVR からは各デバイスが最後に受理されたポーズで接続済み、有効、かつ `TrackingResult_Running_OK` として見え続けます。

## プロトコルの概要

- `127.0.0.1:39570` への UDP
- UTF-8 JSON
- `version` は `1`
- 送信して完了するデータグラム
- 受理されるデータグラムは 8192 バイト未満
- クォータニオンの順序は XYZW
- 認識されるデバイス ID は `hmd`、`left_controller`、`right_controller`、`hip`、`left_foot`、`right_foot`

完全なプロトコルは [UDP プロトコル](docs/protocol.ja.md)を参照してください。

## 登録解除

```powershell
.\scripts\uninstall.ps1
```

**登録解除すると元の SteamVR 構成が復元されます。** 登録中、SteamVR は AnyaDance の 6 デバイス仮想リグを使用します。登録解除すると、登録時に作成された設定バックアップが復元されます。

`uninstall.ps1` は復旧用スナップショットを保存し、ドライバーエントリを削除します。利用可能であれば登録時のバックアップから `steamvr.vrsettings` を復元し、削除を検証して SteamVR を再起動します。AnyaDance のアプリケーションファイル自体は残ります。

UI の **Unregister Driver** ボタンから登録解除することもできます。同じ復旧ファイルを使用し、SteamVR の再起動を促します。

## アンインストール

1. 上記の[登録解除](#登録解除)を実行して SteamVR を再起動し、実デバイスを復元します。
2. 展開した AnyaDance フォルダーを削除します。
3. 必要であれば、AppData に保存された UI の状態を削除します。
   - `%LOCALAPPDATA%\AnyaDance\ui_state.ini` — 保存された設定（ウィンドウサイズ、パス、常に手前に表示する設定など）
   - `%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup` — 登録時に作成された SteamVR 設定のバックアップ（登録解除時に自動削除）
   - `%LOCALAPPDATA%\AnyaDance\registered_driver_path.txt` — バンドルを移動しても登録解除時に見つけられるよう、登録時に記録されたパス（登録解除時に自動削除）

手順 1 を行わずにフォルダーを削除すると、SteamVR は存在しなくなったドライバーパスを参照し続けます。スクリプトフォルダーのコピーから `uninstall.ps1` を実行するか、`%LOCALAPPDATA%\openvr\openvrpaths.vrpath` からドライバーエントリを手動で削除してください。

## ライセンス

AnyaDance はオープンソースであり、Apache License, Version 2.0 の下でライセンスされています。

ライセンス全文は [LICENSE](LICENSE)、帰属表示は [NOTICE](NOTICE) を参照してください。再配布物では NOTICE の内容を保持し、変更したファイルを明示する必要があります。同梱される第三者コンポーネントには、それぞれのライセンスが適用されます。詳しくは [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。

メディアの所有権、ライセンス、および EmoteLab ダンス GIF に適用される個別の条件は [ASSETS.md](ASSETS.md) に記載されています。

AnyaDance と Project Anya のブランド表示については [TRADEMARKS.md](TRADEMARKS.md) を参照してください。

![Project Anya バナー](driver/resources/images/anya_banner.png)
