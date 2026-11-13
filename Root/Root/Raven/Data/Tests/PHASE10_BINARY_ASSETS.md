# Phase 10 — Binary Document / Compiled Test Asset (試験実装)

## バイト列契約

整数フィールドはすべて4 byte little-endian。構造体のraw dumpは禁止。
現行のHeaderは固定長で、Alignmentは1 byte、OffsetはHeader直後に固定。
バイナリはすべて所有Bufferへ読み込み、mmap / zero-copyは行わない。

| Offset | Binary Document (RVBD) | Compiled Test Asset (RVCA) |
| --- | --- | --- |
| 0..3 | Magic `RVBD` | Magic `RVCA` |
| 4..7 | Version=1 | Version=3 |
| 8..11 | Endian marker | Endian marker |
| 12..15 | Payload Length | Payload Length |
| 16..19 | Payload FNV-1a 32 | Source SHA-256（16..47） |
| 20..23 | Payload begins | Source SHA-256続き |
| 24..47 | Payload continues | Source SHA-256続き |
| 48..79 | Payload continues | Payload SHA-256 |
| 80..111 | Payload continues | Dependency SHA-256 |
| 112.. | Payload continues | Payload begins |

Payloadは既存の決定的MessagePack Writerで符号化する。RVBDは汎用DataDocumentを保持し、
RVCAは専用設定型をv2へMigration・検証した結果のみを保持する。
未対応VersionやEndian marker、破損したLength/Checksumは拒否し、出力を変更しない。

## Cache Keyと依存関係

RVCAのSource Cache識別子は入力JSONの**生Byte列**のSHA-256（32 Byte）。
空白の変更もCache Missとなる。Compiler VersionはHeader Versionにも記録し、識別子にも含める。
専用Fixtureでは `CompileWithDependencies` に安定した依存IDの一覧とResolverを渡す。
Resolverは各IDに対応する現在の依存Asset内容を返し、IDの昇順でID長・ID・内容長・内容を連結しSHA-256指紋を生成する。
同じIDの重複、解決失敗、256件超、ID長1024 Byte超、依存内容64 KiB超は拒否する。
`IsCacheCurrent` はソース・Version・依存指紋・Payload Checksumを照合する。
旧 `Compile` は依存なしのショートカットとして残す。SHA-256への変更によりRVCA Versionを3へ更新し、旧Version 1/2は拒否する。
再帰的な依存は `CompileWithGraph` / `IsGraphCacheCurrent` を利用する。
呼び出し側が指定したRoot IDから `DependencyEnumerator` で子IDを取得し、DFSで共有依存を一度だけ解決する。
探索中ノードへの再訪は循環参照として拒否し、同じ親に重複する子IDも拒否する。
GraphのEdge一覧を依存内容とともに指紋へ含め、子Assetの内容変更とEdge変更をCache Missにする。
1回の処理で取得した依存内容をSnapshot化してCompilerへ渡すため、同一処理中にResolverを再呼び出ししない。
Root集合を予約Manifestとして指紋に含め、到達ノードが同一でもRoot指定の変更をCache Missにする。
Graphの実ノードは最大255件（Root Manifestを含めた依存IDは最大256件）。通常の依存記録は64 KiB/ノードを上限とする。
永続Cache I/Oには `BuildCached(cachePath, source, roots, resolver, enumerate, output, cacheHit, error)` を使用する。
既存CacheのHeader・Version・Length・Payload Checksum・ソース・依存Graphに加え、型付きDeserializeも検証し、有効なら再Compileせず返す。
Miss時は再Compileし、Cacheファイルと同じ親の一時Directory内でWrite / Flush / Close後に置換する。
置換失敗時は既存Cacheと呼び出し側のoutputを維持し、一時ファイルを片付ける。
Cacheの親ディレクトリは事前に存在する必要がある。電源断時の永続性、複数Writer間の排他、
敵対的Pathの差し替え、複数プロセス間の更新競合の解決は保証しない。
専用設定AssetのJSONトップレベル `dependencies` は文字列ID配列として扱う。
`ExtractRootDependencies` はフィールド省略を依存なしとし、重複ID・空ID・不正型・256件超を拒否する。
`CompileFromSource` は抽出したRoot IDをGraph Compilerへ渡す。
`BuildCachedFromSource` は同じ抽出処理を永続Cache経路へ接続する。
依存先の子IDは引き続き呼び出し側 `DependencyEnumerator` が提供する。
この専用Fixture以外のAsset形式からの依存抽出は**未実装**。
SHA-256は衝突リスクを大幅に低減するが、署名・認証機構ではない。敵対的なCacheファイルの真正性は保証しない。

## 検証

`scripts/test-windows-structured-data.ps1` にPhase 10テストを統合した。
テストはRound-trip、v1→v2、決定性、SHA-256既知ベクトル（Source・空依存Manifest）、ソース変更によるCache変更、
Header破損、Version不一致、Length破損、Checksum破損、破損Cache再構築、保存失敗時の出力・既存Cache保持を対象とする。
MSVCビルドと実行はGitHubコネクタ経由では実施していない。
実運用Asset移行、他のAsset形式からの依存ID抽出、mmap、正式なVersion Migrationは未対応。
