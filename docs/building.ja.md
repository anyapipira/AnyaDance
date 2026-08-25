# ビルド

[English](building.md) | [简体中文](building.zh-CN.md) | **日本語**

## 前提条件

- Windows 10 以降
- Visual Studio 2022、または「C++ によるデスクトップ開発」ワークロードを含む Build Tools
- CMake 3.22 以降
- 実行時テスト用の SteamVR

BGM バックエンドは、Windows と Windows SDK が提供する Windows Media Foundation、
Core Audio（MMDevice）、XAudio2 2.9 を使用します。CMake がこれらをダウンロードする
ことはなく、AnyaDance もそれらの DLL を同梱しません。Windows 10 以降にはランタイム
コンポーネントが含まれ、Visual Studio/CMake が選択する Windows SDK からヘッダーと
インポートライブラリが提供されるため、音声用 SDK や再頒布可能パッケージを別途
インストールする必要はありません。

## 標準ビルド

```powershell
.\scripts\build_driver.ps1
```

初回の標準ビルドでは、バージョンを固定した次のソースアーカイブをダウンロードします。

- Valve OpenVR SDK `v2.2.3`
- Dear ImGui `v1.90.9`

## ローカルの依存関係

依存関係のルートを渡すと、そのチェックアウトを直接使用します。

```powershell
.\scripts\build_driver.ps1 `
  -OpenVRSdkRoot F:\deps\openvr `
  -ImguiRoot F:\deps\imgui
```

## ビルド出力

```text
build/out/anyadance/AnyaDance.exe
build/out/anyadance/driver.vrdrivermanifest
build/out/anyadance/bin/win64/driver_anyadance.dll
build/out/anyadance/resources/input/anyadance_controller_profile.json
build/out/anyadance/resources/input/anyadance_hmd_profile.json
build/out/anyadance/resources/settings/default.vrsettings
build/out/anyadance/scripts/uninstall.ps1
build/out/anyadance/scripts/unregister_driver.ps1
build/out/anyadance/scripts/restart_steamvr.ps1
build/out/anyadance/scripts/common_steamvr.ps1
build/out/anyadance/LICENSE
build/out/anyadance/NOTICE
build/out/anyadance/THIRD_PARTY_NOTICES.md
build/out/anyadance/README.md
build/out/anyadance/README.zh-CN.md
build/out/anyadance/README.ja.md
```

UI はドライバーフォルダー内にビルドされるため、`build/out/anyadance/` はライセンスと通知を含む自己完結したバンドルになります。実行ファイルは自身が置かれているフォルダーを SteamVR ドライバーとして登録します。

## テスト

```powershell
ctest --test-dir build -C Release --output-on-failure
```

依存関係のないテスト専用ビルドを作成するには、次を実行します。

```powershell
cmake -S . -B build-tests -DANYADANCE_BUILD_DRIVER=OFF -DANYADANCE_BUILD_UI=OFF
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

## UI のバージョン表示

UI はビルドバージョンを Windows のタイトルバーとフッターに表示します。CMake は既定で `git describe --tags --always --dirty` を使用するため、タグそのものから作成したビルドにはそのタグが表示されます。GitHub のリリースワークフローは `github.ref_name` の値を `ANYADANCE_VERSION` として明示的に設定するので、UI の表示はリリースタグと完全に一致します。カスタムビルドでは同じ値を手動で指定できます。

```powershell
cmake -S . -B build -DANYADANCE_VERSION=v0.0.8
```
