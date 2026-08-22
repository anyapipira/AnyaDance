# インストール

[English](installation.md) | [简体中文](installation.zh-CN.md) | **日本語**

## リリース版のインストール

必要な環境：

- Windows 10 以降
- SteamVR
- [Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)](https://aka.ms/vc14/vc_redist.x64.exe)

[GitHub Releases](https://github.com/anyapipira/AnyaDance/releases) から `AnyaDance-<version>-windows-x64.zip` をダウンロードし、`anyadance` フォルダー全体を今後も保持する場所へ展開します。ZIP 内から `AnyaDance.exe` を直接実行しないでください。

`AnyaDance.exe` を起動して安全上の注意事項に同意し、**Register Driver**、続いて **Restart SteamVR** を実行します。アプリケーションは自身が置かれているフォルダーを登録するため、ドライバーが登録されている間はそのフォルダーを移動または削除しないでください。

## ソースからのインストール

先にビルドしてから、アプリケーションの **Register Driver** と **Restart SteamVR** ボタンを使用するか、次を実行します。

```powershell
.\scripts\build_driver.ps1
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

通常、登録されるフォルダーは次の場所です。

```text
build\out\anyadance
```

## 完全仮想モードの設定

`register_driver.ps1` は、仮想 HMD がアクティブな HMD になるように完全仮想モードの設定も適用します。

```text
virtual HMD enabled
controllers enabled
trackers enabled
activateMultipleDrivers enabled
forcedDriver = anyadance
requireHmd = true
```

## バックアップ

`register_driver.ps1` はこれらの設定を書き込む前に、現在の SteamVR 設定ファイルをバックアップします。既定のバックアップ先は次のとおりです。

```text
%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup
```

最初の登録時にバックアップが作成され、登録を繰り返しても元の未変更の設定ファイルが保持されます。`unregister_driver.ps1` はそのバックアップを復元してから削除するため、次回の登録時には新しい基準ファイルが保存されます。

登録または起動設定を変更した後は SteamVR の再起動が必要です。

## 登録解除

リリース版では、**Unregister Driver** をクリックしてから **Restart SteamVR** をクリックし、変更を適用します。展開したフォルダーを移動または削除する前に実行してください。

ソースからインストールした場合、対応するスクリプトは次のとおりです。

```powershell
.\scripts\uninstall.ps1
```

`uninstall.ps1` は `%LOCALAPPDATA%\AnyaDance\uninstall-recovery` にタイムスタンプ付きの復旧スナップショットを保存し、ドライバーの登録を解除します。利用可能であれば元の設定バックアップを復元し、バックアップがなければ既知の AnyaDance オーバーライドを修復して、削除を検証した後に SteamVR を再起動します。SteamVR を停止したままにするには `-NoRestart` を渡してください。アプリケーションファイル自体は削除されません。
