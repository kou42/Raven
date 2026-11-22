# Raven Launcher 実装ロードマップ

- ステータス: Phase 5 完了（2026-11-22）
- 対象: Windows / Visual Studio 2026 / Ravenゲームエンジン
- 基本方針: Raven本体と独立したC++23・Win32 API製ランチャー。既存のVisual Studio/MSBuildプロジェクト管理を維持する。

## 1. 目的と完成条件

Raven Launcherから、(1) 最新ソース取得、(2) プロジェクトファイル同期、(3) ビルド、(4) Visual Studio 2026起動を個別に実行できるようにする。最終的には、事前確認付きの「更新して開く」操作で一連の工程を実行する。

**非目標（初期段階）**: CMake/Premakeへの移行、Raven本体への組み込み、既存プロジェクトの無条件な書き換え、Gitローカル変更の自動破棄。

## 2. 事前調査で確認した構成

提供された `プロジェクト構成.zip` のファイル一覧から、以下を確認した。

- ソリューション: `Root.sln`
- Visual Studioプロジェクト: `Root.vcxproj` および `Raven.Audio` / `Raven.Data` / `Raven.Diagnostics` / `Raven.Editor` / `Raven.Foundation` / `Raven.Graphics` / `Raven.Network` / `Raven.Runtime` の各 `.vcxproj`（計9件）
- 共通設定: `Raven.Common.props`、`Raven.Library.props` など
- ソース登録用設定: `Raven.*.Items.props`、`Raven.Root.Items.props`
- フィルター定義: 各 `.vcxproj.filters`
- ZIP内に `CMakeLists.txt` と `premake5.lua` は見当たらない

実チェックアウトとツールの検証結果は Phase 0 に記録する。

## 3. アーキテクチャ

```text
RavenLauncher.exe (Win32 GUI)
  └─ LauncherController（状態遷移、ジョブ制御）
      ├─ ProcessRunner（非同期実行、標準出力/標準エラー、終了コード、キャンセル）
      ├─ ConfigManager（設定の読み書き）
      ├─ LogManager（時刻付きログ）
      ├─ VisualStudioManager（vswhere、devenv起動）
      ├─ GitManager（status/fetch/pull）
      ├─ ProjectManager（Items.props / filters差分解析・反映）
      └─ BuildManager（MSBuild実行）
```

- GUIスレッドでは時間のかかる外部処理を実行しない。
- すべての外部コマンドは共通の `ProcessRunner` 経由で実行し、作業ディレクトリ、引数、タイムアウト、終了コードを管理する。
- ログは画面とファイルの双方に出力し、個人情報や認証情報を記録しない。
- ランチャー自身はRavenの `.sln` に依存せず、別プロジェクトとしてビルドする。

## 4. フェーズ別ロードマップ

### Phase 0 — 環境確認・仕様固定

**タスク**
- [x] Ravenの実際のリポジトリルートと `Root.sln` の位置を確認
- [x] Gitリモート、追跡ブランチ、サブモジュール/LFSの有無を確認
- [x] VS2026、MSBuild、Windows SDK、`v143` 等のインストール状況を確認
- [x] `.Items.props` のImport関係とファイル登録ルールを整理
- [x] ランチャーの配置先・設定ファイル保存先を決定

**成果物**: 環境チェックリスト、設定仕様、対象ファイル一覧。

**完了条件**: 更新対象と実行ツールの検出方法が確定し、既存ファイルを変更せず診断できる。

#### 環境チェックリスト

| 項目 | 実機確認結果 |
|---|---|
| チェックアウト | `D:\Engine`。ソリューションは `Root\Root.sln`、9 プロジェクトの定義は `Root\Root` 配下 |
| Git | Git for Windows 2.54.0。リモートは `origin`（`https://github.com/kou42/Raven.git`）。調査開始時は `master` → `origin/master`、ローカルの追跡参照との差は前後とも 0、作業ツリーはクリーン。リモートへの通信は未実施 |
| サブモジュール / LFS | 追跡中の `.gitmodules` とインデックス・HEAD の gitlink（モード 160000）は 0 件。Git LFS 3.7.1 は導入済みで、`git lfs ls-files --all` は 0 件 |
| VS / MSBuild | Visual Studio 2026 Community 18.11.12328.437。インストール先に `devenv.exe` が存在し、x64 MSBuild 18.11.1.51303 のバージョン取得に成功 |
| MSVC / SDK | 9 プロジェクト × 4 構成の MSBuild プロパティ評価が成功。全構成で `v143` / MSVC 14.44.35207 / SDK 指定 `10.0`。SDK 10.0.19041.0、10.0.22621.0、10.0.26100.0 のヘッダーと x64/x86 のライブラリを確認。ビルド時に使われる SDK の具体版は未検証 |
| 既存スクリプト | `scripts/build-windows.ps1` と `scripts/bootstrap-windows-build.ps1` に VS / MSBuild 検出の先例あり。追跡中のスクリプトから Items.props / filters を更新するツールは確認できなかった |

診断は Git のローカル照会、`vswhere`、`MSBuild -version`、`MSBuild -getProperty`、XML とファイルの照合で行った。36 構成の評価はビルドターゲットを実行しない。コンパイル、リンク、テスト実行、VS 起動は後続フェーズで確認する。

#### ツール検出と設定仕様

- Git は PATH 上の `git.exe` を解決し、`--version` と、選択した作業ディレクトリでの `rev-parse --show-toplevel` を確認する。更新対象の追跡先は現在のブランチから解決する。診断時はローカル参照を使用する。
- `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe` を使い、`-products * -version "[18.0,19.0)" -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath` で候補を列挙する。候補の `Common7\IDE\devenv.exe` と `MSBuild\Current\Bin\amd64\MSBuild.exe` を確認し、両方ある候補からインストールバージョンが最新のものを選ぶ。
- Raven が指定する `v143` は VS2026 の一般的な C++ コンポーネントと別に確認する。対象構成の MSBuild プロパティ `PlatformToolset`、`VCToolsVersion`、`WindowsTargetPlatformVersion`、`WindowsSdkDir` を評価し、対応するコンパイラ、SDK ヘッダーと対象アーキテクチャのライブラリの存在を確認する。評価失敗、IDE 不足、ツールセット不足、SDK 不足はそれぞれ診断に表示する。
- ランチャーのソースはリポジトリ直下の `tools/RavenLauncher/` に配置し、同ディレクトリの独立した Visual Studio プロジェクトで `RavenLauncher.exe` をビルドする。成果物は `out/<Platform>/<Configuration>/` 配下に出力する。
- 設定はユーザーごとの `%LOCALAPPDATA%\RavenLauncher\config.json`（UTF-8 JSON）に保存する。初回はチェックアウトの場所を選択し、診断だけの実行では設定を書き込まない。

| 設定キー | 形式・初期値 |
|---|---|
| `schemaVersion` | 整数。初期値 `1` |
| `repositoryRoot` | Git で検証したチェックアウトルートの絶対パス。初回選択時に設定 |
| `solutionPath` | ルート相対パス。初期値 `Root/Root.sln`。解決後のパスがルート内にあり、既存の `.sln` であることを確認 |
| `configuration` | 初期値 `Debug` |
| `platform` | 初期値 `x64`。構成との組み合わせを `.sln` の `SolutionConfigurationPlatforms` で検証する |

現行ソリューションは `Debug` / `Release` と `x64` / `x86` の全組み合わせを定義する。ソリューションの `x86` はプロジェクトの `Win32` に対応する。ツールのパスは実行環境から検出する。

#### 同期対象とファイル登録規則

以下はすべて `Root/Root/` からの相対パス。各行の Items.props と filters が Phase 3 の同期候補で、所属の調査には現行の登録パスを使う。

| プロジェクト | Items.props | フィルター |
|---|---|---|
| Root | `Raven.Root.Items.props` | `Root.vcxproj.filters` |
| Raven.Audio | `Raven.Audio.Items.props` | `Raven.Audio.vcxproj.filters` |
| Raven.Data | `Raven.Data.Items.props` | `Raven.Data.vcxproj.filters` |
| Raven.Diagnostics | `Raven.Diagnostics.Items.props` | `Raven.Diagnostics.vcxproj.filters` |
| Raven.Editor | `Raven.Editor.Items.props` | `Raven.Editor.vcxproj.filters` |
| Raven.Foundation | `Raven.Foundation.Items.props` | `Raven.Foundation.vcxproj.filters` |
| Raven.Graphics | `Raven.Graphics.Items.props` | `Raven.Graphics.vcxproj.filters` |
| Raven.Network | `Raven.Network.Items.props` | `Raven.Network.vcxproj.filters` |
| Raven.Runtime | `Raven.Runtime.Items.props` | `Raven.Runtime.vcxproj.filters` |

- 8 つの静的ライブラリは `Microsoft.Cpp.Default.props` → `Raven.Library.props`（`Microsoft.Cpp.props` と `Raven.Common.props` を Import）→ 個別 Items.props の順に読み込む。Root は `Microsoft.Cpp.props` と構成別の PropertySheets の後に `Raven.Common.props` と `Raven.Root.Items.props` を読み込む。`RavenUITest.props` の Import は `Debug|x64` に限る。
- Items.props は個別パスを `Raven<モジュール名>Source` / `Header` / `Inline` で登録する。対応する `.vcxproj` で `ClCompile` / `ClInclude` / `None` に展開される。`Inline` を展開する現行プロジェクトは Data、Editor、Foundation、Runtime。Graphics の `src\glad.c` は `Raven/` ツリー外にある明示登録として維持する。
- filters は Visual Studio での表示を管理する。Items.props と同じ相対パスを、展開後の Item 型で既存の論理フォルダーに登録する。9 組すべてで Items.props の登録先ファイルが存在し、対応する filters のパスと Item 型も一致した。
- Root と Runtime は `Animation`、`Character`、`Physics`、`Scene` などを共有し、`Core` や `Renderer` も複数プロジェクトにまたがる。既存の明示登録を維持し、新規ファイルは既存の登録パスと所属が明確な場合に同期候補へ含める。判断が分かれるパスはプレビューで手動確認に回す。
- `.sln`、`.vcxproj`、共通・テスト用 `.props` は解析時の入力とする。プロジェクトに直書きされた条件付き項目（`main.cpp` や UI テスト）、アセット、生成物などは自動反映対象から除外する。

### Phase 1 — GUI・共通基盤・VS起動

**タスク**
- [x] 独立したC++23/Win32ランチャープロジェクトを作成
- [x] メイン画面（Git更新、プロジェクト同期、ビルド、VS起動、ログ）を作成
- [x] `ProcessRunner` を実装（非同期、stdout/stderr、終了コード、キャンセル）
- [x] 設定の読み書きを実装（リポジトリパス、ソリューションパス、ビルド構成）
- [x] 画面・ファイルへのログ出力と状態表示を実装
- [x] `vswhere` でVS2026を検出し、`devenv.exe` で `Root.sln` を開く
- [x] 未インストール・パス不正・起動失敗時のエラー表示を実装

**成果物**: 単独起動できる `RavenLauncher.exe`、設定ファイル、基本GUI。

**完了条件**: GUIが固まらず、VS2026を検出・起動でき、失敗理由が画面とログに残る。

#### 実装と検証

- `tools/RavenLauncher/RavenLauncher.vcxproj` を MSBuild でビルドし、同ディレクトリの `out/x64/<Configuration>/RavenLauncher.exe` を起動する。リポジトリのフォルダーを指定し、`Open Visual Studio` を押すと、ワーカーが Git ルートとソリューションを検証し、VS2026 を検出する。設定を保存して IDE に起動を要求し、画面と `%LOCALAPPDATA%\RavenLauncher\launcher.log` に結果を表示する。
- VS2026 の MSBuild で `Debug|x64` と `Release|x64` をビルドした。実機の GUI でリポジトリ未指定時のエラーを確認し、`D:\Engine` を指定すると VS2026 に `Root` ソリューションが表示された。設定ファイルとログへの記録も確認した。
- 共通処理は正常終了、非ゼロ終了、stdout/stderr 各 1 MiB の取得、タイムアウト、キャンセル、日本語と空白を含む設定値の UTF-8 保存・再読込、不正なリポジトリとソリューションパスを検証した。VS 未導入環境での表示と IDE 起動失敗時の表示は未実機確認。

### Phase 2 — Git連携

**タスク**
- [x] `git status --porcelain` 等で未コミット変更を検出
- [x] 現在のブランチと追跡先、更新前コミットを表示
- [x] `git fetch` を実行し、更新可能か確認
- [x] ローカル変更や分岐がある場合は自動更新を止め、ユーザーに通知
- [x] 安全な場合のみ `git pull --ff-only` を実行
- [x] 更新後コミットと変更ファイル一覧を記録
- [x] 認証失敗、ネットワーク障害、競合時のメッセージを整備

**成果物**: Git更新ボタン、更新前確認、結果ログ。

**完了条件**: クリーンな追跡ブランチでfast-forward更新でき、未コミット変更を破棄しない。追跡先不明・分岐時は停止する。

#### 実装と検証

- `Git Update` はローカル変更、進行中の Git 操作、追跡設定を確認してから、対象ブランチのリモート追跡参照を取得する。ローカル先行と分岐は停止し、同一コミットは更新不要として表示する。反映可能な場合は追跡先と更新前後のコミットを表示して確認を求める。
- 承認後に作業ツリー、ブランチ、追跡先、コミットを再確認し、取得済みのコミットを指定してローカルから `git pull --ff-only` を実行する。無視対象のファイルに追加パスが重なる場合も停止する。取得・確認段階ではキャンセルでき、作業ツリーへの反映が始まると完了を待つ。
- `Debug|x64` と `Release|x64` をビルドした。一時リポジトリで更新成功、更新不要、未コミット変更、無視対象の衝突、追跡先なし、確認後の追跡設定変更、ローカル先行・分岐、detached HEAD、接続失敗、キャンセル済みの要求を検証した。GUI から確認の取り消しと反映を実行し、承認した HEAD と変更ファイル一覧が画面とファイルのログに記録されることを確認した。
- 認証失敗の分類は実サービスの資格情報を使った試験が未実施。同じ作業ツリーで別の Git 操作を並行実行すると、直前の照合後に状態が変わる余地がある。

### Phase 3 — Visual Studioプロジェクト同期

**タスク**
- [x] `.Items.props` / `.vcxproj.filters` / Import関係を解析
- [x] プロジェクト別のファイル所属ルールを明文化
- [x] ソースツリーと登録済みファイルの差分を検出
- [x] 追加・削除・移動の候補をプレビュー（初期は読み取り専用）
- [x] 不明な所属先や条件付きItemを自動変更対象から除外
- [x] 更新前バックアップ、確認ダイアログ、原子的な保存を実装
- [x] XMLの名前空間・条件・順序・コメント等の保持を検証
- [x] 同期後に再解析して整合性を確認

**成果物**: 差分プレビュー、承認付き同期機能、バックアップ。

**完了条件**: 既存設定を破損せず、既知の所属ルールに従う追加・削除を反映できる。再実行しても不要な差分が出ない。

#### 実装と検証

- `Sync Projects` で読み取り専用の差分をログに出す。追加は Git の追跡ファイルと無視設定の対象外の未追跡ファイルのうち、同じディレクトリ内の既存登録から所有プロジェクトと既存の論理フィルターを一意に判断できる C / C++ ファイルに限る。削除は、実ファイルが見つからず、Items.props と filters の単純な登録を一意に対応付けられる場合に限る。同一プロジェクト内で同名の追加と削除が一意に対応する場合は移動候補として表示する。
- 条件付き登録、編集範囲にコメントや追加メタデータがある登録、所有先が曖昧なパスは手動確認に回す。アセット、生成物、サードパーティーのディレクトリは同期対象外。既存の XML を再シリアライズせず、登録の削除・追加に必要な範囲だけを編集する。
- `Apply Sync...` で承認後、差分を再照合する。`%LOCALAPPDATA%\RavenLauncher\sync-backups\` に元ファイルをコピーし、対象と同じディレクトリの一時ファイルを使って 1 ファイルずつ置換する。置換後に再解析と再プレビューを行う。途中失敗時は置換済みのファイルを復元し、復元できない場合は対象とバックアップ位置をログに出す。
- `Debug|x64` と `Release|x64` をビルドした。実チェックアウトの読み取り専用プレビューには自動変更候補が 0 件だった。一時コピーで追加、削除、移動候補、条件付き登録の保持、曖昧な所属の除外、名前空間・コメント・既存の並びの保持、古いプレビューの拒否、置換失敗時の復元、再プレビューで差分ゼロを確認した。GUI でも確認の取り消し、反映、バックアップ位置のログ表示を確認した。
- 複数ファイルの反映中にプロセスを強制終了した場合は、同期開始時のバックアップから利用者が復元する。

### Phase 4 — MSBuild連携

**タスク**
- [x] VS2026に付属するMSBuildを検出
- [x] `Root.sln` のConfiguration/Platformを読み取り、選択肢を表示
- [x] Debug/Release、x64/Win32等の有効な構成でビルド
- [x] stdout/stderrと終了コードを取得し、エラー・警告を表示
- [x] ビルド中の操作制御、キャンセル、再実行を実装
- [x] ツールセット・SDK不足を診断して案内

**成果物**: ビルド画面、構成選択、ログ、成功/失敗表示。

**完了条件**: 有効な構成でMSBuildを実行でき、失敗時に原因追跡に必要なログが残る。

#### 実装と検証

- `Load Configs` で選択中の `.sln` にある `SolutionConfigurationPlatforms` を読み込む。`Build` は選択を再検証し、`vswhere` で検出した VS2026 の x64 MSBuild でインクリメンタルビルドを行う。ソリューションの `x86` は MSBuild にそのまま渡し、各プロジェクトの `Win32` への対応付けにはソリューションの定義を使う。
- 実行中は GUI で出力を確認できる。実行単位の標準出力・標準エラーと結果は `%LOCALAPPDATA%\RavenLauncher\build-logs\` に保存する。MSBuild の診断コードから、C++ ビルドツール・MSVC ツールセット・Windows SDK の不足を案内する。
- `Cancel` は MSBuild とその子プロセスを終了し、同じ構成で再実行できる。キャンセル時に残った中間生成物は、次回のインクリメンタルビルドで MSBuild が利用または更新する。
- ランチャーの `Debug|x64` と `Release|x64` をビルドした。一時 C++ ソリューションで空白を含む構成とパス、x64 と x86、コンパイルエラー、警告、キャンセル後の再実行、MSVC ツールセット・Windows SDK 不足の案内を確認した。
- 実機の GUI から Raven の `Debug|x64` をビルドし、終了コード 0、警告 20 件、`Root/x64/Debug/Root.exe` の生成を確認した。MSBuild で評価された SDK は 10.0.26100.0。vcpkg が試行した `pwsh.exe` は PATH 上に見つからず、ログにその記録が残った。最終版での増分再実行も終了コード 0（新規出力の警告 0 件）。ビルド後の Raven 起動は未確認。

### Phase 5 — 一括実行・仕上げ

**タスク**
- [x] `Git状態確認 → fetch/pull → プロジェクト差分確認・承認 → ビルド → VS起動` を連結
- [x] 各工程を個別実行できる状態を維持
- [x] 工程ごとの進捗、失敗時停止、再試行を実装
- [x] 初回セットアップと環境診断画面を追加
- [x] 通常・失敗・キャンセル時の動作テスト
- [x] READMEとトラブルシューティングを整備

**成果物**: 「環境を更新して開く」ボタン、利用手順書。

**完了条件**: 変更内容を確認しながら一括実行でき、いずれかの工程で失敗した場合は後続工程を開始しない。

- `Update & Open` は既存の処理で各工程を実行し、Git の反映とプロジェクト差分の反映を個別に確認する。停止時は理由と完了済み工程数をログに表示する。再試行は最新の作業ツリーを Git 工程から確認する。
- `Setup` はチェックアウトとソリューション構成を検証して設定を保存する。`Diagnose` は Git、VS2026、MSBuild と、既定の Raven ソリューションの MSVC・SDK を確認する。[ランチャー利用手順](../../tools/RavenLauncher/README.md) に日常操作と同期バックアップの復元手順を記載した。
- `Debug|x64` と `Release|x64` のランチャービルドに成功した。実機の環境診断で v143 と Windows SDK 10.0.26100.0 を確認した。一時リポジトリで Git の fast-forward、承認付き同期、ビルド、VS2026 起動を確認した。未コミット変更、不正な XML、コンパイルエラー、起動前のソリューション欠落で該当工程に停止し、同期確認待ちとビルド中のキャンセル後は再試行で完了した。

## 5. 安全性・品質要件

- **Git**: `reset --hard`、`clean -fd`、強制pushを自動実行しない。
- **プロジェクトファイル**: まず差分表示、ユーザー承認後に変更。バックアップを作る。
- **外部コマンド**: 引数を適切にエスケープし、可能な限りシェル経由の実行を避ける。
- **UI**: 長時間処理はワーカースレッドで実行し、状態更新のみGUIスレッドに通知。
- **障害対応**: 終了コード・エラー内容・対象コマンドを記録。失敗した工程以降は実行しない。
- **再実行性**: 更新不要時や同期済みの場合に不要なファイル変更を行わない。

## 6. テスト計画

| 対象 | 主なテスト |
|---|---|
| ProcessRunner | 正常終了、異常終了、大量ログ、キャンセル、タイムアウト |
| VS起動 | VS2026あり/なし、ソリューションなし、起動失敗 |
| Git | クリーン、未コミット変更、追跡先なし、分岐、通信失敗 |
| プロジェクト同期 | ファイル追加/削除、所属不明、条件付きItem、二重実行、バックアップ復旧 |
| ビルド | Debug/Release、プラットフォーム選択、コンパイルエラー、SDK不足 |
| 一括実行 | 全工程成功、各工程での失敗停止、ユーザーキャンセル |

## 7. 推奨実装順とマイルストーン

1. **M0**: 実環境・プロジェクト依存関係を確認（Phase 0）
2. **M1**: GUIから `Root.sln` をVS2026で開ける（Phase 1）
3. **M2**: 安全なGit更新ができる（Phase 2）
4. **M3**: `.Items.props` の差分プレビューと承認付き同期ができる（Phase 3）
5. **M4**: MSBuildによるビルドとログ表示ができる（Phase 4）
6. **M5**: 一括実行ができる（Phase 5）

## 8. 後続フェーズでの確認事項

- VS 未導入環境での表示と IDE 起動失敗時の表示は、該当環境で確認する。
- 認証が必要な実サービスでの失敗表示を確認する。
- 追跡外の個人用プロジェクト更新ツールの有無は未確認。
- Raven 本体の Release / x86 ビルドと、生成した実行ファイルの起動は未確認。

---

Phase 0〜5 の実装が完了した。未検証の項目は前節に記載する。
