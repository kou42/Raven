# Raven Structured Data / Serialization Roadmap

作成日: 2026-10-04  
最終確認日: 2026-10-28  
状態: Phase 0を一部完了。`Core::JsonValue / JsonParser / JsonWriter` と `JobSystem` は既存資産として利用する。Token DOM、MessagePack、Reflection、Network / RPCは未実装。

## 目的

Scene、Material、Physics設定、Animation設定、Asset Metadata、Editor設定を、フォーマットに依存しない共通データ基盤で扱う。将来はMessagePackによるネットワークメッセージにも利用する。

RE:Doxについての議論を設計の着想とするが、以下はRaven向けの提案であり、RE:Dox内部実装の仕様ではない。C++での所有権、寿命管理、エラー処理を優先する。

## 現在の基盤

2026-10-28時点で、Ravenには次の基盤がある。

| 項目 | 状況 | 利用箇所・意味 |
| --- | --- | --- |
| `Core::JsonValue` | 実装済み | Null / Bool / Number (`double`) / String / Array / Objectを、`std::string`、`std::vector`、`std::unordered_map`で所有する可変DOM。 |
| `Core::JsonParser` | 実装済み | JSON構文、Unicode escapeとsurrogate pair、数値範囲、重複キー、末尾データを検証し、byte位置付きのエラーを返す。 |
| `Core::JsonWriter` | 実装済み | Key順を固定した決定的UTF-8 JSON出力。NaN / Infinityは拒否する。 |
| glTF Import | 実運用 | GLB JSONを`JsonValue`へ読み、`GltfDocument`、Mesh、Skin、Animation、Materialの各Importerが型付きデータへ変換する。 |
| UI Dock Snapshot | 実運用 | Dock構造、Tab、選択状態のJSON保存・復元、version検証、破損時の無変更保証、バックアップ復元がある。 |
| Humanoid Animation Profile | 実運用 | type / version検証、必須・任意フィールド、旧データの補完、全検証後の反映を実装済み。 |
| `JobSystem` | 実装済み | CPU専用のWorker Pool。Renderer / Window / SceneなどのMain Thread専用ObjectへWorkerから触れない契約がある。 |
| Reflection / MessagePack / Token DOM | 未実装 | 本ロードマップの対象。 |

`Core::JsonValue`は既存ファイルとImporterの安定した基盤である。新しい`DataDocument`はこれを急に置換しない。まずは新規用途で導入し、互換性・計測結果・移行コストを確認した後に、必要な箇所だけ移行する。

## 基本方針

- Parse、Deserialize、Serialize、Editを分離する。
- 入力はToken Tapeへ変換し、型付きデータへの変換はDataReaderが担当する。
- 型付きデータの出力はDataWriterへ直接書き、不要なDOM構築を避ける。
- Tokenはまず明確なフィールド構成で実装する。16 byte程度を候補とし、サイズは必要な範囲表現と計測結果で決める。
- JSONを最初の入力形式とし、MessagePackを次の主要形式とする。既存`Core::JsonValue`は維持し、Token DOMはsource-backed読み取り、大規模入力、形式変換、複数形式対応が必要な領域へ導入する。
- 読み取り専用Documentを先に完成させ、編集機能は後から独立して追加する。
- 汎用Document用バイナリと、実行時Asset用バイナリは別の用途として設計する。
- 公開ヘッダーは宣言中心、通常実装はcpp、テンプレート・inline実装はinlに記述する。設計意図と寿命条件をコメントに残す。

## 処理の構成

| 経路 | 処理 |
| --- | --- |
| 読み込み | JSON / MessagePack → Parser → DataDocument → DataReader → 型付きデータ |
| 書き込み | 型付きデータ → 明示的Adapter / Reflection → DataWriter → JSON / MessagePack |
| 形式変換 | DataDocument → Document走査 → DataWriter → 対応する出力形式 |
| Assetビルド | ソースDocument → 検証・変換 → Asset Compiler → Runtime Asset |
| ネットワーク | メッセージ型 → MessagePack Writer → 通信層 → Parser → 検証 → メッセージ型 |

## Phase一覧

Phase 0は一部完了。Token DOM以降の実装は未着手であり、既存JSON基盤の置換は目的に含めない。

| Phase | 内容 | 状態 | 前提 |
| --- | --- | --- | --- |
| 0 | 既存調査・用途と型規約の確定 | 一部完了 | なし |
| 1 | Token / Document / Element | 未着手 | 0 |
| 2 | JSON Parser・入力検証 | 未着手 | 1 |
| 3 | Lazy Decode・走査・寿命管理 | 未着手 | 2 |
| 4 | Reader / Writer・JSON出力 | 未着手 | 3 |
| 5 | 型付きSerialization・Reflection接続 | 未着手 | 4 |
| 6 | MessagePack読み書き | 未着手 | 4、型付き利用には5 |
| 7 | エンジン機能への試験導入 | 未着手 | 5 |
| 8 | 計測・並列Deserialize | JobSystemの前提は完了 | 7 |
| 9 | Mutable Document・Editor連携 | 未着手 | 7 |
| 10 | 汎用Documentバイナリ・Compiled Asset | 未着手 | 7 |
| 11 | Network / RPCへの接続 | 未着手 | 5・6、通信層 |

Phase 8〜11は独立性が高いため、実際の必要性に応じて順序を調整する。ネットワーク利用のためにMutable DOMやCompiled Assetの完成を待つ必要はない。

## Phase 0 — 既存調査・規約確定

### 完了済み

- [x] 既存のJSON処理を確認した。`Core::JsonValue / JsonParser / JsonWriter`はCoreにあり、glTF、UI Dock Snapshot、Humanoid Animation Profileが使用している。
- [x] JSON Objectの文字列キーと重複キーの規則を確認した。現在のParserは重複キーを拒否する。
- [x] JSON Numberは`double`として保持し、WriterはNaN / Infinityを拒否する。
- [x] JobSystemの責務を確認した。CPU処理に限定し、Main Thread専用Objectの変更をWorkerから行わない。
- [x] 型付きDeserializeの実例を確認した。Humanoid Profileは、入力を一時データへ読み、全検証後だけ出力へ反映している。

### 残り

- [ ] Asset Importer、Stream、Memory Allocatorの接続点を調査する。
- [ ] 最初の新規用途を決める。推奨は既存保存データを直ちに置換しない独立したEditor設定または専用テストAsset。
- [ ] Null、Bool、Int64、UInt64、Float64、String、Binary、Array、Objectの意味を確定する。
- [ ] MessagePackの非文字列Mapキー、Extension、Timestampの扱いを決める。未対応型は明示的に拒否し、黙って文字列化しない。
- [ ] スキーマVersion、未知フィールド、必須フィールド、既定値の規則を決める。Humanoid Profileの検証・全成功後のCommitを共通の手本にする。
- [ ] 入力サイズ、Token数、深さ、文字列長、コンテナ要素数の上限を定義する。
- [ ] `DataDocument`の直接JSON Parserを作るか、既存`JsonValue`との変換Adapterから始めるかを、source-backed要件と計測対象に基づいて決定する。

完了条件: 最初の利用対象、既存コードとの接続点、型変換と失敗時の契約、移行しない既存経路が文書化されている。

## Phase 1 — Token / Document / Element

- [ ] DataType、DataToken、TokenId、DataDocument、DataElementを実装する。
- [ ] 入力BufferとToken配列をDocumentが所有する基本経路を作る。
- [ ] Source範囲、次の兄弟または部分木終端、子の位置を表現し、ネストした構造を飛ばせるようにする。
- [ ] 無効TokenId、範囲外アクセス、サイズ計算のOverflowを検出する。
- [ ] コピー・Move・破棄に伴うDataElement / Viewの無効化条件を明記する。
- [ ] Tokenの幅と上限を一致させる。特に要素数を16 bitに固定して大きい配列を扱えなくしない。
- [ ] `Core::JsonValue`との一方向変換を必要最小限に用意し、既存のJSON利用箇所を段階移行できるようにする。

完了条件: 手動構築したネスト構造を走査でき、要素ごとのHeap確保を必要としない。

## Phase 2 — JSON Parser

- [ ] JSONのObject、Array、String、Number、Bool、NullをToken Tapeへ変換する。
- [ ] 既存`Core::JsonParser`の規約と同じく、UTF-8、Escape、Unicode surrogate pair、数値文法、重複キー、末尾データを検証する。
- [ ] Token化時に構文を検証し、値の具体的な数値変換・文字列展開は必要に応じて遅延できるようにする。
- [ ] Trailing data、不正Escape、不完全な入力、上限超過を拒否する。
- [ ] エラー種別、入力位置、可能なら行・列を返す。失敗時は部分Documentを公開しない。
- [ ] Reserve / Arenaなどの利用は既存Memory基盤との整合性を確認して決める。
- [ ] 既存`JsonParser`との互換Fixtureを作り、許可・拒否する入力が意図せず変わらないことを確認する。

完了条件: 正常・不正入力のテストが通り、既存Parserと定めた構文規約で一致し、失敗後に壊れたDocumentを残さない。

## Phase 3 — Lazy Decode / Traversal

- [ ] 型確認、キー検索、Array走査、Object走査を実装する。
- [ ] 未Escape文字列はUTF-8 View、Escape文字列は展開後の値として取得する。
- [ ] 生の文字列表記を返すRaw APIと、意味上の文字列を返すDecoded APIを分ける。
- [ ] Int / UInt / Float変換で範囲外、符号不一致、精度損失の規則を明示する。
- [ ] キー検索でもEscape済みキーを正しく比較する。
- [ ] Document共有読み込み時の安全性を確保する。Lazy Cacheは事前展開、同期、呼び出し側Bufferなどから方式を選ぶ。
- [ ] Borrowed Buffer経路は所有経路が安定した後に検討し、受信Bufferの再利用でViewが壊れない契約を定める。

完了条件: Escapeを含む入力を正しく取得でき、Viewの寿命と並列アクセス条件が明確である。

## Phase 4 — DataReader / DataWriter

既存の`Core::JsonWriter`は`JsonValue`をJSONへ出力できる。ここでは`DataDocument`または型付きデータから出力する共通Writer契約を追加し、既存Writerとの接続方法を決める。

- [ ] フォーマット非依存のDataReaderと構造化イベントを受け取るDataWriterを設計する。
- [ ] `DataDocument`からのJsonWriter、または既存`JsonValue`へ変換して既存JsonWriterを利用するAdapterを実装する。
- [ ] 型付きデータからDOMを介さず出力できる経路を追加する。
- [ ] Document走査からWriterへ渡す形式変換経路を追加する。
- [ ] 入れ子、キーと値の対応、出力失敗、最大出力サイズを検証する。
- [ ] JSONで表現できないBinary、NaN、Infinity等は拒否または明示的Encodingを選び、暗黙変換しない。

完了条件: 対応型のJSON → Document → JSONで意味が保持され、既存JsonWriterと必要な決定性・エラー規約が両立する。

## Phase 5 — 型付きSerialization / Reflection

- [ ] 明示的AdapterによるSerialize / Deserializeを先に作る。
- [ ] 既存の個別Deserializeを参考に、必須値、既定値、Version Migration、検証後Commitの共通ヘルパーを導入する。
- [ ] Reflectionが必要な用途が明確になった段階で、必要最小限の型・フィールド記述から接続する。
- [ ] Vector、Quaternion、AssetId、EntityId等のAdapterを必要なものから追加する。
- [ ] 必須・既定値・未知フィールド・Version Migrationを実装する。
- [ ] 生Pointerを保存せず、安定IDと読み込み後の参照解決を使う。
- [ ] 検証済み一時データを作ってから対象へ反映し、途中失敗で既存状態を壊さない。

完了条件: 小さな設定型でRound-tripとVersion更新を確認し、エラーにフィールド経路が含まれる。

## Phase 6 — MessagePack

- [ ] MessagePack Parser / Writerを同じReader / Writer契約へ接続する。
- [ ] 整数幅、Float32 / Float64、StringとBinary、ArrayとMapを正しく扱う。
- [ ] 長さPrefix、切断入力、巨大Count、Overflowを検証する。
- [ ] 非文字列MapキーとExtensionの規約を実装する。初期制限がある場合はエラーとして公開する。
- [ ] 公式仕様に対応したFixtureと独立した実装との相互読み書きで互換性を確認する。
- [ ] JSONとの変換は共通に表現できる範囲に限定し、損失がある変換を検出する。

完了条件: 型付きメッセージをバイト列へ変換・復元でき、不正入力による過剰確保を防げる。

## Phase 7 — Ravenへの試験導入

- [ ] Phase 0で選んだ新規設定または専用テストAssetに導入する。
- [ ] 既存ファイルとの互換性と移行方法を確認する。
- [ ] UI Dock SnapshotまたはHumanoid Profileへは、読み込み互換性・保存形式・回帰テストを確認してから段階導入する。
- [ ] Material、Physics設定、Animation設定、Sceneへ必要順に広げる。
- [ ] glTF等の専用Importerは形式固有処理を維持し、共通化する範囲をMetadataなどに限定して評価する。
- [ ] 読み込み失敗時の既定値、診断表示、保存失敗時の元ファイル保護を確認する。

完了条件: 1つの実用途で保存・再読み込みが成立し、既存動作の回帰がない。

## Phase 8 — 計測・並列Deserialize

`JobSystem`は利用できる。並列Deserializeは、逐次処理の正しさと計測基盤を完成させてから導入する。

- [ ] 小設定、大配列、深いネスト、Escapeの多い文字列を計測用データにする。
- [ ] Parse、Decode、型変換、Writeの時間と、Allocation回数・Peak Memory・Token数を分けて計測する。
- [ ] 必要な場合のみキー索引、Reserve、Token圧縮を導入する。
- [ ] 独立した配列要素の変換を`JobSystem`へ投入する。
- [ ] 要素数とToken量を基に閾値を実測する。特定ライブラリの既定値をそのまま採用しない。
- [ ] 出力領域を事前確保し、順序保持、エラー集約、取消、共有Cache、参照解決を扱う。
- [ ] SceneやGPU等への変更は変換Jobで直接行わず、必要なThreadでCommitする。
- [ ] Diagnostics基盤へ計測結果を出す場合は、ゲーム中のログ負荷を増やさない集計単位と出力頻度を定める。

完了条件: 逐次処理と結果が一致し、対象負荷で改善を確認できる。小さい入力は逐次経路を維持する。

## Phase 9 — Mutable Document / Editor

- [ ] 不変Documentから編集用Documentへ移行する境界を設計する。
- [ ] 変更値用Storage、挿入・削除・置換、Slot再利用を追加する。
- [ ] HandleのGeneration等で削除・再利用後の古い参照を検出する。
- [ ] 変更後のView無効化と、読み取りThreadとの関係を明記する。
- [ ] Editor CommandによるUndo / Redoへ接続する。
- [ ] 編集後のCompact / Freezeと保存を追加する。

完了条件: 編集、Undo / Redo、保存・再読み込みが成立し、Slot再利用で古いHandleが別の値を指さない。

## Phase 10 — Binary Document / Compiled Asset

- [ ] 汎用Document保存形式と、用途別Runtime Asset形式を別々に定義する。
- [ ] Magic、Version、Endian、Alignment、Offset、Length、互換性規則を定める。
- [ ] C++ structのMemory Dumpをファイル仕様にせず、各フィールドのEncodingを固定する。
- [ ] Asset Compilerで検証、参照解決、変換、依存関係とCache Key生成を行う。
- [ ] memory mapping / zero-copyは形式と寿命が対応する場合にのみ導入する。
- [ ] 壊れたHeader、Offset範囲外、Version不一致を検証する。

完了条件: 少なくとも1つのAssetでソースからRuntime形式を生成・読み込みでき、再ビルドとVersion変更を扱える。

## Phase 11 — Network / RPC連携

MessagePackはデータ表現を担当する。接続、配送、暗号化、認証、信頼性は通信層の責務とする。

- [ ] 通信層とはCodec境界で接続し、Data層へSocket依存を入れない。
- [ ] MessageType、ProtocolVersion、RequestId等を持つEnvelopeを定義する。
- [ ] TCP等のStreamでは長さによるFraming、UDPではDatagramサイズ上限を定める。
- [ ] Lobby、制御メッセージ、RPC引数等からMessagePackを試験導入する。
- [ ] 受信した型と値を検証し、許可されたメッセージだけDispatchする。
- [ ] Buffer所有権、Queue投入、Thread切替、切断時の破棄を定める。
- [ ] 高頻度同期はMessagePackを基準として計測し、必要ならQuantization、BitStream、Delta、Snapshot方式を別Codecとして追加する。

完了条件: Loopback等で型付きメッセージの送受信が成立し、切断入力、不正型、Version不一致、上限超過を安全に拒否できる。

## 推奨配置案

既存構造の調査後に確定する。ファイル名はRavenの命名規約に合わせる。

| 配置候補 | 責務 |
| --- | --- |
| Root/Root/Raven/Data/ | Token、Document、Element、Reader / Writer、Error / Limits |
| Root/Root/Raven/Data/Formats/Json/ | JSON Parser / Writer |
| Root/Root/Raven/Data/Formats/MessagePack/ | MessagePack Parser / Writer |
| Root/Root/Raven/Data/Serialization/ | 型Adapter、Reflection接続、Version Migration |
| Root/Root/Raven/Data/Tests/ | 構文・型・寿命・互換性テスト |
| 既存Assets配下 | Asset Compiler、Compiled Assetとの接続 |
| 将来のNetwork配下 | Envelope、RPC Dispatch、通信層、Replication Codec |

`Core::JsonValue`、`Core::JsonParser`、`Core::JsonWriter`は既存の`Core`配下に残す。`Data`はToken DOMと複数形式対応の追加層として開始する。

## 最初の実装範囲

まずPhase 0の残項目を完了させ、その後Phase 1〜3を最初の実装区切りとする。

- [ ] Asset / Memory / Streamとの接続点と最初の用途を確定する。
- [ ] 小さいJSONをDataDocumentへ読み込める。
- [ ] RootからObject / Arrayを走査できる。
- [ ] Escapeを含むString、Bool、整数、浮動小数を取得できる。
- [ ] 不正入力と上限超過を位置付きエラーで返せる。
- [ ] DocumentとViewの寿命規則が説明・検証されている。
- [ ] 既存`Core::JsonParser`との互換Fixtureが通る。

この段階ではReflectionの新規大規模実装、編集DOM、並列化、ネットワーク通信は含めない。基礎が安定したらPhase 4〜6で型付き読み書きとMessagePackを追加する。
