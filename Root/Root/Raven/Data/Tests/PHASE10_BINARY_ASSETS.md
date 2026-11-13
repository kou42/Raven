# Phase 10 — Binary Document / Compiled Test Asset (試験実装)

## バイト列契約

整数フィールドはすべて4 byte little-endian。構造体のraw dumpは禁止。
現行のHeaderは固定長で、Alignmentは1 byte、OffsetはHeader直後に固定。
バイナリはすべて所有Bufferへ読み込み、mmap / zero-copyは行わない。

| Offset | Binary Document (RVBD) | Compiled Test Asset (RVCA) |
| --- | --- | --- |
| 0..3 | Magic `RVBD` | Magic `RVCA` |
| 4..7 | Version=1 | Version=2 |
| 8..11 | Endian marker=0x01020304 | Endian marker=0x01020304 |
| 12..15 | Payload Length | Payload Length |
| 16..19 | Payload FNV-1a 32 | Source Cache識別子 |
| 20..23 | Payload begins | Payload FNV-1a 32 |
| 24..27 | Payload continues | 依存Asset指紋 FNV-1a 32 |
| 28.. | Payload continues | Payload begins |

Payloadは既存の決定的MessagePack Writerで符号化する。RVBDは汎用DataDocumentを保持し、
RVCAは専用設定型をv2へMigration・検証した結果のみを保持する。
未対応VersionやEndian marker、破損したLength/Checksumは拒否し、出力を変更しない。

## Cache Keyと依存関係

RVCAのSource Cache識別子は入力JSONの**生Byte列**のFNV-1a 32とCompiler Versionの積（16777619）とのXOR。
空白の変更もCache Missとなる。Compiler VersionはHeader Versionにも記録し、識別子にも含める。
専用Fixtureでは `CompileWithDependencies` に安定した依存IDの一覧とResolverを渡す。
Resolverは各IDに対応する現在の依存Asset内容を返し、IDの昇順でID長・ID・内容長・内容を連結して指紋を生成する。
同じIDの重複、解決失敗、256件超、ID長1024 Byte超、依存内容64 KiB超は拒否する。
`IsCacheCurrent` はソース・Version・依存指紋・Payload Checksumを照合する。
旧 `Compile` は依存なしのショートカットとして残す。依存指紋の追加によりRVCA Versionを2へ更新し、旧Version 1は拒否する。
再帰的な依存は `CompileWithGraph` / `IsGraphCacheCurrent` を利用する。
呼び出し側が指定したRoot IDから `DependencyEnumerator` で子IDを取得し、DFSで共有依存を一度だけ解決する。
探索中ノードへの再訪は循環参照として拒否し、同じ親に重複する子IDも拒否する。
GraphのEdge一覧を依存内容とともに指紋へ含め、子Assetの内容変更とEdge変更をCache Missにする。
1回の処理で取得した依存内容をSnapshot化してCompilerへ渡すため、同一処理中にResolverを再呼び出ししない。
最大256ノード・依存記録64 KiB/ノードの上限を持つ。
永続Cache I/Oには `BuildCached(cachePath, source, roots, resolver, enumerate, output, cacheHit, error)` を使用する。
既存CacheのHeader・Version・Length・Payload Checksum・ソース・依存Graphを検証し、有効なら再Compileせず返す。
Miss時は再Compileし、Cacheファイルと同じ親の一時Directory内でWrite / Flush / Close後に置換する。
置換失敗時は既存Cacheと呼び出し側のoutputを維持し、一時ファイルを片付ける。
Cacheの親ディレクトリは事前に存在する必要がある。電源断時の永続性、複数Writer間の排他、
敵対的Pathの差し替え、Hash衝突への耐性は保証しない。
依存IDのソースからの自動抽出、強いCache Keyは**未実装**。
FNV-1a 32は衝突耐性を保証しないため、信頼できない入力を扱う共有Cacheの同一性検証には使わない。

## 検証

`scripts/test-windows-structured-data.ps1` にPhase 10テストを統合した。
テストはRound-trip、v1→v2、決定性、ソース変更によるCache変更、
Header破損、Version不一致、Length破損、Checksum破損、出力不変を対象とする。
MSVCビルドと実行はGitHubコネクタ経由では実施していない。
実運用Asset移行、ソースからの依存ID自動抽出、mmap、正式なVersion Migrationは未対応。
