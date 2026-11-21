# Raven プロジェクト整理 M2 Data・Network・Diagnostics

Data、Network、Diagnosticsの製品コードを、既存のソースパスのまま個別の静的ライブラリに切り出した。

| ライブラリ | C++ | ヘッダー | `.inl` | 直接のプロジェクト参照 |
|---|---:|---:|---:|---|
| Raven.Data | 12 | 14 | 2 | Foundation |
| Raven.Network | 4 | 3 | 0 | Data |
| Raven.Diagnostics | 15 | 15 | 0 | Foundation、Network |

各 `.Items.props` はRootのワイルドカード除外とライブラリの登録で共有する。製品コードのローカルincludeがこの依存方向で解決することを確認した。テストとベンチマークは既存の入口があるRoot側に置いた。診断の表示・Renderer連携を扱うテストもRoot側にある。

ライブラリ間の参照はビルド順を定義し、依存先のオブジェクトを自分のアーカイブへ取り込まない。Rootが各アーカイブとWinsockの `ws2_32.lib` をリンクする。Debugのアーカイブを列挙し、Foundation 21本、Data 12本、Network 4本、Diagnostics 15本で、各ライブラリ自身のオブジェクトだけを含むことを確認した。静的ライブラリ共通の設定は `Raven.Library.props` にまとめ、プロジェクト名で中間・出力ディレクトリを分離した。

`VerifyDiagnosticMessages` はDiagnosticsの `ClCompile` 前に実行する。入力の `Messages.json` とロックファイルをDiagnosticsへ登録し、生成済みヘッダーのパスは維持した。Rootのプロジェクト参照がDiagnosticsの検証・ビルドを先に完了させる。

## M0との比較

MSBuildでx64 Debug／Releaseの通常・CPU UI・GPUおよび各計測付き構成と、Win32 Debug／Releaseの通常構成を評価した。14構成すべてでソースの二重所属がなく、全プロジェクトのコンパイル対象の合計はM0と一致した。通常構成はRoot 404本、ライブラリ合計52本。CPU UIはRoot 405本、GPUは407本。

## 検証結果

| x64の検証 | Debug | Release |
|---|---|---|
| Rootに依存しないライブラリのビルド | 4ライブラリ成功 | 4ライブラリ成功 |
| Rootビルド、DataDocument編集・Scene RuntimeのCLI | 成功 | 成功 |
| Data・Networkの既存テストを新しいライブラリにリンク | 6,125チェック、失敗0 | 6,683チェック、失敗0 |
| Rootの診断セルフテスト | 7スイート、失敗0 | 7スイート、失敗0 |
| Rootを使ったリモート診断 | 5シナリオ、失敗0 | 5シナリオ、失敗0 |
| 最終ビルドでのメタデータ検証 | 1回、成功 | 1回、成功 |

通信テストは実際のTCPループバックを使い、リモート診断の転送、ファイル保存と再生、上書き拒否、上限と待機時間、接続の分離を確認した。ループバックを含む実行は権限を切り替えた。Data・NetworkのReleaseでは割り当て計測も有効にした。追加で試した計測付きDebugのテストランナーは終了コード3で、原因は未特定。通常のDebugランナーで全対象スイートを再実行し、6,125チェックが通った。

ビルドはM0と同じMSPDB回避設定を使った。既存の型変換とCRT関連警告、および回避オプションの上書き警告が出た。Win32とCPU UI・GPU構成はMSBuildでの静的評価まで実施した。生ログは `artifacts/logs/project-org-m2-*` にある。
