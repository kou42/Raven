# Raven Structured Data / Serialization Roadmap

作成日: 2026-10-04  
最終確認日: 2026-10-30\
状態: Phase 0〜3完了。Phase 4以降の実装は未着手。`Core::JsonValue / JsonParser / JsonWriter` と `JobSystem` は既存資産として利用する。Token DOMの基礎型、手動構築・既存DOM変換、Coreと字句処理を共有する直接JSON Parser、Decoded取得・型変換・キー検索を実装済み。MessagePack、Reflection、Network / RPCは未実装。

## 目的

Scene、Material、Physics設定、Animation設定、Asset Metadata、Editor設定を、フォーマットに依存しない共通データ基盤で扱う。将来はMessagePackによるネットワークメッセージにも利用する。

RE:Doxについての議論を設計の着想とするが、以下はRaven向けの提案であり、RE:Dox内部実装の仕様ではない。C++での所有権、寿命管理、エラー処理を優先する。

## 現在の基盤

2026-10-30時点で、Ravenには次の基盤がある。

| 項目 | 状況 | 利用箇所・意味 |
| --- | --- | --- |
| `Core::JsonValue` | 実装済み | Null / Bool / Number (`double`) / String / Array / Objectを、`std::string`、`std::vector`、`std::unordered_map`で所有する可変DOM。 |
| `Core::JsonParser` | 実装済み | JSON構文、Unicode escapeとsurrogate pair、数値範囲、重複キー、末尾データを検証し、byte位置付きのエラーを返す。 |
| `Core::JsonWriter` | 実装済み | Key順を固定した決定的UTF-8 JSON出力。NaN / Infinityは拒否する。 |
| glTF Import | 実運用 | GLB JSONを`JsonValue`へ読み、`GltfDocument`、Mesh、Skin、Animation、Materialの各Importerが型付きデータへ変換する。 |
| UI Dock Snapshot | 実運用 | Dock構造、Tab、選択状態のJSON保存・復元、version検証、破損時の無変更保証、バックアップ復元がある。 |
| Humanoid Animation Profile | 実運用 | type / version検証、必須・任意フィールド、旧データの補完、全検証後の反映を実装済み。 |
| `JobSystem` | 実装済み | CPU専用のWorker Pool。Renderer / Window / SceneなどのMain Thread専用ObjectへWorkerから触れない契約がある。 |
| Token DOM / JSON入力 / 読み取りAPI | Phase 1〜3実装済み | 所有Document、手動Tape検証、既存JsonValueからの一方向変換、source-backed直接JSON Parser、Raw / Decoded取得、厳密数値変換、キー検索と走査。 |
| Reflection / MessagePack | 未実装 | 本ロードマップの対象。 |

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

Phase 0〜3は完了（2026-10-30）。Reader / Writer以降は未着手であり、既存JSON基盤の置換は目的に含めない。

| Phase | 内容 | 状態 | 前提 |
| --- | --- | --- | --- |
| 0 | 既存調査・用途と型規約の確定 | 完了（設計契約） | なし |
| 1 | Token / Document / Element | 完了（CPU単体検証） | 0 |
| 2 | JSON Parser・入力検証 | 完了（CPU単体検証） | 1 |
| 3 | Lazy Decode・走査・寿命管理 | 完了（CPU単体・並列読み取り検証） | 2 |
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

### 追加調査・確定事項（2026-10-30）

以下はPhase 1以降が満たす設計契約。実装済みの範囲は各Phaseの実装結果を参照する。

- [x] Asset Importer、Stream、Memory Allocatorの接続点を調査した。
- [x] 最初の新規用途を専用テストAssetに決定した。
- [x] Null、Bool、Int64、UInt64、Float64、String、Binary、Array、Objectの意味と変換規則を確定した。
- [x] MessagePackの非文字列Mapキー、Extension、Timestampは初期版で明示的に拒否する。
- [x] スキーマVersion、未知・必須フィールド、既定値、検証後Commitの規則を確定した。
- [x] 入力サイズ、Token数、深さ、文字列長、コンテナ要素数の初期上限を定義した。
- [x] source-backed要件を満たす直接JSON Parserから開始する。既存DOM変換は互換用途に限定する。

#### 接続点と責務

| 調査対象 | 現在の実装 | Data層との接続契約 |
| --- | --- | --- |
| `Gltf/GlbReader.cpp`、`Gltf/GltfDocument.cpp` | `std::ifstream`でGLBを読み、所有する`GlbData::JsonText`を既存Parserへ渡す。 | 将来接続する場合も、GLBのChunk検証はGlbReaderに残し、JSON部分だけをData層へ渡す。今回の移行対象にはしない。 |
| `Assets/TextureAssetImporter.cpp` | 画像DecodeとRuntime Texture生成を担当する。 | Data層には画像Decode、Renderer、GPU生成を持ち込まない。Metadataの解釈・検証はImporter側で行う。 |
| `UI/Document/IDocumentImporter.h` | 形式固有Importerから正規化済み`UIDocument`を返す。 | UI用Documentと構造化データ用DataDocumentは別責務。既存インターフェースは変更しない。 |
| `Animation/HumanoidAnimationProfileSerialization.cpp` | File I/OとJSON変換を分離し、一時Profileの検証成功後だけ反映する。 | 新規の型付き読み込みもこの境界を採用する。既存Profile形式・読み書きは維持する。 |
| `Core/Memory/Allocator.h`、`STLAllocatorAdapter.h`、`FrameAllocator.h` | AdapterはAllocatorを所有せず、FrameAllocatorはResetFrame後の参照保持を禁止する。 | 初期Documentは標準の所有Bufferと連続Token配列を用いる。FrameAllocator由来の領域を保持しない。独自Allocator接続は寿命・Move・失敗処理を検証した後に追加する。 |

Core / Assetsに共通の入力Stream抽象は今回の調査では見つからなかった。初期Parserの入口は長さ付きメモリ入力とし、File I/Oは呼び出し側に残す。呼び出し側はファイルサイズを確保前に検査し、短いReadも拒否する。Parserも受け取ったサイズを再検査してからDocument所有Bufferへコピーする。Streamの新設、逐次Parse、Borrowed Bufferは初期実装に含めない。

#### 最初の用途とスキーマ規則

最初の用途は、将来の`Data/Tests/`で扱う専用設定テストAssetとする。実運用のEditor設定・Scene・glTF・Dock Snapshot・Humanoid Profileは置換しない。Phase 1では手動Token構築、Phase 2〜3ではJSONメモリ入力と走査を検証し、型付き設定への導入はPhase 5・7で行う。

設定Fixtureの例は`{"type":"Raven.StructuredDataTest","version":1,"enabled":true,"label":"test","samples":[1,2,3]}`とする。これはテスト契約であり、Runtime設定機能の追加ではない。

- `type`と`version`は必須。`version`は正の整数で、対応外Versionを黙って読み込まない。
- Fixtureの`enabled`は必須Bool、`label`は省略時に空文字列、`samples`は省略時に空配列とする。明示的なNullを欠落と同一視しない。
- 既知フィールドの型不一致は既定値で隠さず拒否する。未知フィールドは検証済みDocument上で無視するが、再保存時の保持は保証しない。
- Migrationは対応する旧Versionを明示して一時データへ適用する。必須値・参照・値域を全検証した後だけ出力へCommitし、失敗時は既存出力を変更しない。

#### 型と変換の契約

| 型 | 意味・初期規則 |
| --- | --- |
| Null / Bool | Nullは明示的な値。Boolへの数値・文字列の暗黙変換は行わない。 |
| Int64 | 符号付き64 bit整数。JSONの負の整数表記を割り当てる（`-0`は整数0）。範囲外はParse時に拒否する。 |
| UInt64 | 符号なし64 bit整数。JSONの符号なし整数表記を割り当てる。Int64上限を超える値も保持する。UInt64上限を超えた値をFloatへ丸めない。 |
| Float64 | 小数点または指数を含むJSON数値。有限doubleのみ。OverflowおよびUnderflowによる範囲エラーを拒否する。`-0.0`の符号は保持する。 |
| String | 意味上は妥当なUTF-8文字列。長さを持ち、Escape由来のNULも許容する。Raw表記とDecoded値のAPIは分離する。 |
| Binary | 文字列と異なる任意バイト列。JSON入力では生成しない。JSON出力への暗黙Base64変換はしない。 |
| Array | 順序付きの値列。異なる型の混在を許容し、型制約は利用側で検証する。 |
| Object | 順序付きの文字列キーと値の組。Decodedキーが同一の重複（`"a"`と`"\u0061"`を含む）は拒否する。キー検索はDecoded値で比較する。 |

整数の構文・範囲検査はToken化時に行い、具体的な値の取得は遅延可能とする。Int64とUInt64の相互取得は取得先の範囲内だけ許可する。Float64から整数への取得は有限・整数値・取得先範囲内の場合だけ許可し、切り捨てない。整数からFloat64への取得は正確に表現できる場合だけ許可する（単なるdouble経由の整数往復で範囲外変換を起こさない）。文字列から数値への暗黙変換はしない。

既存`JsonValue`からDataDocumentへの一方向Adapterは、既存Numberがdoubleである事実を維持してFloat64へ変換する。既に失われた整数精度を復元したとは扱わない。将来逆方向のAdapterを追加する場合も、Binaryや正確にdoubleへ変換できない整数は拒否する。

MessagePackのMapは文字列キーに限定し、Decodedキー重複を拒否する。非文字列キー、全Extension（Timestampを含む）は未対応型エラーとし、文字列化や読み飛ばしを行わない。Float32はFloat64へ正確に拡張し、NaN / Infinityは拒否する。これらの実装はPhase 6で行う。

#### 所有権・走査・失敗時の契約

- DataDocumentが入力BufferとToken配列を所有し、公開後は不変とする。TokenはSource範囲、子の位置、部分木の次の位置を保持し、ネストした値を飛ばして走査できるようにする。
- TokenId・Offset・Length・Countは32 bit unsignedを基本とし、無効TokenIdには`UINT32_MAX`を予約する。終端位置はToken数と等しい値まで許可し、実Token参照と区別する。16 byteへの固定は要求せず、必要な情報と実測に基づいて決める。
- DataElementはDocumentへの非所有参照とTokenIdを持つ。Document破棄・置換・Moveで既存ElementとViewは無効となり、呼び出し側は再取得する。Copyを初期APIでは禁止し、明示的な所有権移動だけを許可する。破棄済みDocumentへのアクセス検出は保証しない。
- 未Escape文字列のViewはDocumentの入力Bufferを参照する。Escape展開結果は呼び出し側Bufferへ書き、共有Document内に可変Lazy Cacheを置かない。独立Bufferを使う読み取り同士のみ並列利用可能とし、Move・破棄・出力Buffer共有と競合させない。
- Parseは一時Documentで完結させ、成功時だけ出力へMoveする。失敗時は既存DocumentとそのViewを維持し、部分Documentを公開しない。型取得失敗も出力値を変更しない。
- エラーは種別（構文、Encoding、重複キー、数値範囲、上限、未対応型、メモリ不足等）と0始まりのByte位置を持つ。EOFエラー位置は入力長とする。行・列は後続の補助情報とし、型付き変換ではフィールド経路を追加する。
- 無効TokenId・範囲外は無効Elementまたは明示的な取得失敗として返す。Source範囲と部分木範囲は使用前に検証する。確保失敗は一時状態を破棄してメモリ不足として返し、エラー報告自体に追加確保が必要な設計を避ける。

#### 初期上限と互換性の検証計画

初期`DataLimits`は以下の値を既定値とする。用途側で小さくでき、拡大時もToken表現の上限を超える設定は拒否する。これは受理上限であり、一括Reserve量ではない。

| 対象 | 既定上限 | 数え方 |
| --- | --- | --- |
| 入力 | 64 MiB | コピー前の入力Byte数。 |
| Token | 1,048,576 | Root・コンテナ・Objectキーを含む全Token。終端専用Tokenは作らない。 |
| ネスト深さ | 128 | Rootコンテナを1、スカラーRootを0とする同時Openコンテナ数。 |
| String / Binary | 16 MiB / 値 | Stringは引用符を除くRaw Byte数とDecoded UTF-8 Byte数の両方。キーにも適用する。BinaryはPayload Byte数。 |
| コンテナ要素 | 262,144 / コンテナ | Arrayは値数、ObjectはKey / Value組数。 |

Offset + Length、Token数増加、Count × 要素サイズは加算・乗算前に検査する。上限に達した後のPushやReserveを行わない。深さ上限は再帰呼び出し前に検査する。上限ちょうど・1超過・巨大な長さPrefix・EOF・失敗後の出力不変を後続テストに含める。

新Parserは妥当なUTF-8、Escape、surrogate pair、数値文法、Decodedキー重複、末尾データを検証する。ただし現行`Core/JsonParser.cpp`の`ParseString`は非Escapeバイトをそのまま格納し、生UTF-8の妥当性を検証していない。新Parserでの不正UTF-8拒否は意図した厳格化としてFixtureに記録し、既存Parserを今回変更しない。

Phase 2〜3の互換Fixtureは次を区別する。

- 共通の正常・不正構文：空コンテナ、Unicode Escape、surrogate pair、Escapeキー重複、制御文字、不完全入力、末尾データ、数値文法。
- 意図した差：不正UTF-8の拒否、整数のInt64 / UInt64保持と範囲外拒否、既存double経路の精度損失、上記DataLimitsによる拒否。
- 寿命・走査：空 / ネスト構造、65,535を超える要素、兄弟へのSkip、Raw / Decoded取得、失敗後の既存Document維持、Move後の参照再取得、独立Bufferでの並列読み取り。

完了条件: 最初の利用対象、既存コードとの接続点、型変換と失敗時の契約、移行しない既存経路が文書化されている。Phase 0ではこの文書の静的確認までを完了とし、上記API・Fixtureの実装と実行はPhase 1以降の完了条件として扱う。

## Phase 1 — Token / Document / Element

- [x] DataType、DataToken、TokenId、DataDocument、DataElementを実装する。
- [x] 入力BufferとToken配列をDocumentが所有する基本経路を作る。
- [x] Source範囲、次の兄弟または部分木終端、子の位置を表現し、ネストした構造を飛ばせるようにする。
- [x] 無効TokenId、範囲外アクセス、サイズ計算のOverflowを検出する。
- [x] コピー・Move・破棄に伴うDataElement / Viewの無効化条件を明記する。
- [x] Tokenの幅と上限を一致させる。特に要素数を16 bitに固定して大きい配列を扱えなくしない。
- [x] `Core::JsonValue`との一方向変換を必要最小限に用意し、既存のJSON利用箇所を段階移行できるようにする。

完了条件: 手動構築したネスト構造を走査でき、要素ごとのHeap確保を必要としない。

### Phase 1実装結果（2026-10-30）

- `Data/DataToken.h`にDataType、TokenId、DataToken、DataLimits、確保不要のDataErrorを追加した。
- `DataDocument::Create`は手動Preorder TapeとSourceを検証して所有コピーする。構造・範囲・Count・深さ・UTF-8・重複キー・有限Float64を検証し、失敗時は既存出力を維持する。
- `DataElement`は型を含むconst Token参照、Source View、子・兄弟の走査を提供する。Object直下のキーと値は別Tokenで、値の部分木をSkipして次のキーへ進める。
- `DataDocument::FromJsonValue`は既存NumberをFloat64として保持する一方向Adapter。Objectキーをソートし、元のJSONテキストではなく展開済み文字列を連結して所有する。
- Tokenは現在32 byte（一般的な64 bit ABI）。入力・Token・作業Stack・キー索引は連続領域で、要素ごとの所有Heap Objectを持たない。検証・Adapterは再帰呼び出しを使わない。
- Source ViewとToken参照はDocumentの破棄・置換・Moveで無効。Move元は空にする。破棄済み参照や置換前Handleの検出は保証しない。
- GCC C++17の警告をエラーにした単体ビルドと69チェックが成功した。ASan / UBSanでも成功（環境のptrace制約によりLeakSanitizerは無効化）。一時テストHarnessによる確保失敗注入13箇所でも出力維持を確認した。
- MSBuildが環境にないため、Visual Studioでの全体ビルド・リンクは未検証。実行方法と寿命規則は`Data/Tests/README.md`に記載した。

JSONのRaw表記・Escape位置の保持、直接Parser、Lazy Decode、型付き取得API、実運用Assetの移行はこのPhaseに含めない。これらはPhase 2以降で実装する。

## Phase 2 — JSON Parser

- [x] JSONのObject、Array、String、Number、Bool、NullをToken Tapeへ変換する。
- [x] 既存`Core::JsonParser`とEscape、Unicode surrogate pair、数値文法を共有し、重複キー・末尾データを検証する。生UTF-8はData経路で厳密に検証する。
- [x] Token化時に構文を検証する。数値は範囲検査とPayload確定を同時に行い、文字列値はRaw表記とEscape情報を保持して展開を遅延する。
- [x] Trailing data、不正Escape、不完全な入力、上限超過を拒否する。
- [x] エラー種別と入力Byte位置を返す。行・列は未提供。失敗時は部分Documentを公開しない。
- [x] Reserve / Arenaなどの利用は既存Memory基盤との整合性を確認して決める。
- [x] 既存`JsonParser`との互換Fixtureを作り、許可・拒否する入力が意図せず変わらないことを確認する。

完了条件: 正常・不正入力のテストが通り、既存Parserと定めた構文規約で一致し、失敗後に壊れたDocumentを残さない。

### Phase 2実装結果（2026-10-30）

- `Core/JsonLexicalReader.h/.cpp`へ既存ParserのString / Unicode Escape / Number文法を切り出した。`Core::JsonParser`の公開APIとJsonValue構築経路は維持し、CoreからDataへの依存を追加しない。
- `Data/Formats/Json/JsonParser.h/.cpp`を追加した。`Data::JsonParser`は共通字句Readerを使い、再帰せずにTokenを生成し、`DataDocument::Create`の検証・所有コピー・成功後Commitへ接続する。
- JSON入力全体と数値のRaw範囲を所有する。Stringは引用符を除いたRaw範囲を指し、Payloadが`DecodedUtf8String`なら未Escape UTF-8、`JsonEscapedString`なら未展開JSON表記を示す。Tokenの32 byte幅は維持する。
- Escape文字列は妥当性と展開後Byte数のみ検査する。Decodedキーの重複検査に必要なEscapeキーは一時Bufferへ展開し、そのBufferも入力サイズ上限で制限する。公開Decoded取得・キー検索APIはPhase 3で追加する。
- 負の整数をInt64、符号なし整数をUInt64、小数点・指数付き表記をFloat64にする。整数範囲外をFloatへ丸めず拒否する。Float変換はlocale非依存の`from_chars`を使い、表現可能なsubnormalを保持し、0へのUnderflowを拒否する。subnormalをERANGEとして拒否し得る既存strtod経路との差は意図したものとしてテストした。
- 入力・Token・深さ・文字列Raw / Decoded Byte数・コンテナ要素数を検査する。巨大入力の先行ReserveやFrameAllocator利用は追加しない。構文、UTF-8、重複キー、数値範囲、上限、EOF、末尾データ、確保失敗を位置付きエラーで返す。
- Phase 1の69チェックとPhase 2の276チェックがGCC C++17・警告をエラーにするビルドで成功した。ASan / UBSanも成功（ptrace制約によりLeakSanitizerは無効）。一時Harnessで変更前と変更後のCore Parserを20,000ケース比較し、受理・拒否と成功時の値が一致した。
- 既存Profile JSON 2件とTerrain GLBのJSON部分を両Parserで読み込んだ。確保失敗注入はDocument / Adapter / JSON Parserを合わせて25箇所で出力維持を確認した。実運用の読込経路は変更していない。
- 新規ファイルのFilter登録と`structure.txt`を更新した。MSBuildがないためVisual Studio全体ビルド・リンクは未検証。実行手順と意図した互換性差は`Data/Tests/README.md`へ記載した。

制限: エラー文言・位置の既存Core経路との完全一致は保証しない。構造の組み立ては出力の責務が異なるためCore DOMとData Tapeで分離している。共通化は字句規則が対象。Document検証を再利用するため、Parse中のTapeと確定DocumentのTapeが一時的に共存する。性能・Peak memory計測は未実施。

## Phase 3 — Lazy Decode / Traversal

- [x] 型確認、キー検索、Array走査、Object走査を実装する。
- [x] 未Escape文字列はUTF-8 View、Escape文字列は展開後の値として取得する。
- [x] 生の文字列表記を返すRaw APIと、意味上の文字列を返すDecoded APIを分ける。
- [x] Int / UInt / Float変換で範囲外、符号不一致、精度損失の規則を明示する。
- [x] キー検索でもEscape済みキーを正しく比較する。
- [x] Document共有読み込み時の安全性を確保する。Lazy Cacheは事前展開、同期、呼び出し側Bufferなどから方式を選ぶ。
- [x] 初期APIは所有Buffer経路のみとする。Borrowed Bufferは導入せず、将来導入時に受信Buffer再利用とView無効化の契約を別途定義する。

完了条件: Escapeを含む入力を正しく取得でき、Viewの寿命と並列アクセス条件が明確である。

### Phase 3実装結果（2026-10-30）

- `DataElement.h/.cpp`へ`IsType / TryGetType`、`TryGetBool / Int64 / UInt64 / Float64`、`TryGetRawString / StringView / DecodedString`を追加した。無効Elementや型不一致は既定値へ置き換えず失敗し、すべてのTry APIは失敗時に出力を維持する。
- 未Escape文字列はDocument参照のViewで取得する。Escape文字列は既存の共通字句Readerを使い、呼び出し側所有のStringへ必要時だけ展開する。既存DOM Adapter由来の文字列に含まれるBackslashを二重展開しない。
- 整数相互変換は取得先範囲内だけ許可する。Floatから整数は整数値かつ範囲内だけ許可し、上端を含まない`[-2^63, 2^63)` / `[0, 2^64)`を検査してからCastする。`-0.0`は整数0へ取得できる。
- 整数からFloatは有効bitと捨てられる下位bitで精度を検査する。doubleを経由した整数往復を行わず、Int64 / UInt64上端での未定義動作を防ぐ。`2^53 + 1`は拒否し、`2^53 + 2`や`2^63`は正確に取得できる。
- `TryGetCount / TryGetArrayElement / TryGetObjectMember / TryFind`を追加した。Objectは組単位で取得でき、キー検索はDecoded UTF-8 Byte列を比較し、NUL・surrogate pair・literal Backslashを区別する。見つからない場合も出力を変更しない。
- index取得はTapeを走査するためO(index)。連続走査は既存の子・兄弟APIで行う。Objectのキーと値を同じ出力変数へ取得する指定は拒否する。
- Document内に可変Cacheや共有Scratchは追加しない。検索の一時Bufferは呼び出し内だけで所有する。Source ViewはDocumentの破棄・置換・Moveで無効になり、Decoded StringへのViewは呼び出し側Stringの変更・破棄で無効になる。
- `DataElementTests.h/.cpp`を追加した。GCCでは2,253チェックが成功し、固定の数値境界に加えlong doubleによる独立した精度判定とも照合した。long doubleの精度がdoubleと同じ環境では、この追加照合だけをスキップする。
- 4 Worker × 500回の共有Document読み取りを確認した。既存のPhase 1・2を合わせた2,598チェックと既存Asset JSONの読み取りが、警告をエラーにするビルドとASan / UBSan / float-cast-overflowで成功した。ptrace制約によりLeakSanitizerは無効化した。
- 一時HarnessでDecoded出力・Escapeキー検索の確保失敗6箇所を注入し、出力維持を確認した。Escapeキーを調べない検索とString View取得の無確保経路も確認した。
- Filter、`structure.txt`、`Data/Tests/README.md`を更新した。ThreadSanitizerとVisual Studio全体ビルドは未実施。Borrowed Buffer、性能計測、Reader / Writer、実運用Assetの移行は対象外。

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

Phase 0〜3は完了。最初の読み取り基盤が成立したため、次はPhase 4のDataReader / DataWriterとJSON出力を承認対象とする。各Phaseの開始はAGENTS.mdのマイルストーン承認ゲートに従う。

- [x] Asset / Memory / Streamとの接続点と最初の用途を確定した（Phase 0）。
- [x] 小さいJSONをDataDocumentへ読み込める（Phase 2）。
- [x] RootからObject / Arrayを走査できる（Phase 1・2）。
- [x] Escapeを含むString、Bool、整数、浮動小数を取得できる（Phase 3）。
- [x] 不正入力と上限超過を位置付きエラーで返せる（Phase 2）。
- [x] DocumentとViewの寿命規則が説明・検証されている（Phase 1〜3、Borrowed Bufferは未導入）。
- [x] 既存`Core::JsonParser`との互換Fixtureが通る（Phase 2、意図した差を含む）。

この段階ではReflectionの新規大規模実装、編集DOM、並列化、ネットワーク通信は含めない。基礎が安定したらPhase 4〜6で型付き読み書きとMessagePackを追加する。
