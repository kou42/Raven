# Raven プロジェクト整理 M1 Foundation

`Raven.Foundation` をMSVCの静的ライブラリとして追加した。移管したファイルは既存のパスに置き、Rootは `ProjectReference` でFoundationをビルド・リンクする。Foundationにはプロジェクト参照がない。

## 所有と設定

| Foundationの所有範囲 | 内容 |
|---|---|
| Math | ベクトル、行列、クォータニオン、Transform、共通の数値処理 |
| Core直下 | Base、CPUProfiler、汎用JSONの字句処理・値・パーサー・ライター |
| Coreのサブディレクトリ | Containers、割り当て器とMemoryTracker、JobSystem |

[所有一覧](../../Root/Root/Raven.Foundation.Items.props) はC++ソース21本、ヘッダー41本、`.inl` 15本を明示する。RootのワイルドカードとFoundationの項目はこの一覧を共有する。ヘッダーから参照するテンプレート定義も一覧に登録し、既存のincludeパスで利用する。

Application、Window、Input、テストとベンチマークの入口はRootが保持する。割り当て計測の `AllocationMeasurement.cpp` もRootに置く。計測時にグローバル `new/delete` を置換する翻訳単位を実行ファイルに直接リンクし、通常・CPU UI・GPUそれぞれの入口でアーカイブからの抽出条件が変わる影響を避けるため。Foundationは計測フラグに依存せず、計測付き構成でも同じ設定で利用する。

警告レベル、SDL、C++標準、UTF-8、適合モード、共通マクロは `Raven.Common.props` を両プロジェクトで使う。Foundationのinclude設定は標準ライブラリと既存ソースのルートで完結し、GLFW・Vulkan SDK・DirectXの設定はRoot側にある。Foundation専用の `IntDir`・`OutDir` を設け、同名のCoreとDataのJSON実装を別々のディレクトリでコンパイルする。ソリューションに中間・出力ディレクトリを一括指定した場合もFoundation用のサブディレクトリを使う。

## M0との静的比較

[M0の一覧](RAVEN_PROJECT_ORGANIZATION_M0_SOURCES.csv) とMSBuildの評価を照合した。x64 Debug／Releaseそれぞれの通常・CPU UI・GPUと各割り当て計測付き構成、Win32 Debug／Releaseの通常構成で、RootとFoundationを合わせたコンパイル対象はM0と一致した。Foundationへ移管した21本とRootのコンパイル対象に重複はない。

| 入口 | Root | Foundation | 合計 |
|---|---:|---:|---:|
| 通常 | 435 | 21 | 456 |
| CPU UI | 436 | 21 | 457 |
| GPU | 438 | 21 | 459 |

Foundationの所有ファイルから参照するローカルincludeはすべて所有一覧の中に解決した。Foundationのソース・ヘッダー・`.inl` の登録とVisual Studioのフィルターも照合した。

## x64の検証結果

| 検証 | Debug | Release |
|---|---|---|
| Foundation単独ビルド | 成功 | 成功 |
| 既存のTransformMath・JobSystemテスト | 両方成功 | 両方成功 |
| Rootの通常ビルドとOpenGL起動・終了 | 成功 | 成功 |
| DataDocument編集・Scene RuntimeのCLI | 両方成功 | 両方成功 |
| String・Name・FlatHashMap・FlatHashSet・MemoryのCLI | 全件成功 | Debug専用 |
| 割り当て計測付きCPU UI | ビルド・テスト成功 | ビルド・テスト成功 |

MathとJobsのテストは各構成のFoundationを全オブジェクト込みでリンクし、Rootや上位ライブラリへのリンクなしで実行した。両方のテストでassertを有効にした。通常起動は引数なしでOpenGLウィンドウの表示と正常終了を確認した。

計測付きCPU UIの `--measure-ui-widgets` では、プールなしの100ボタン生成で `allocation_available=1` を確認した。割り当て回数はDebug 300回、Release 100回でM0と一致した。

サンドボックス内の複数ノードビルドが進まなかったため、初回はMSBuildの単一ノードで検証した。Debugは権限を切り替えて複数ノードのビルドを再試行し、成功した。同じ環境での変更なし再ビルドはDebug／Releaseとも終了コード0で、`Root.exe` と `Raven.Foundation.lib` の更新時刻は変わらなかった。

M0で記録したMSPDB問題を回避するため、`/Z7`、リンクデバッグ情報なし、インクリメンタルリンクなしで検証した。Rootのビルドには既存の `C4244` とDebug CRTの `LNK4098`、回避オプションに伴う `D9025` が出た。Win32とGPUテスト構成はソース・参照の静的評価まで実施した。Win32の既存リンク制約は [M0の結果](RAVEN_PROJECT_ORGANIZATION_M0_BASELINE.md) を参照。

ログは `artifacts/logs/project-org-m1-*` に保存した。
