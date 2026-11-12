# Structured Data Phase 1〜4 単体テスト

GPU・Window・外部ライブラリに依存しないC++17のテストです。通常アプリケーションでは
`RunDataDocumentTests()`を提供し、`RAVEN_DATA_TEST_MAIN`指定時だけ専用mainを定義します。
Phase 2の直接JSON ParserとPhase 3のDecoded取得・数値変換・キー検索APIを含みます。

リポジトリルートで実行します。

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DRAVEN_DATA_TEST_MAIN \
  -IRoot/Root Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Data/DataUtf8.cpp Root/Root/Raven/Data/DataDocument.cpp \
  Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/Tests/DataDocumentTests.cpp -o /tmp/raven-data-tests
/tmp/raven-data-tests
```

Visual Studio Developer Command Promptでの単体ビルド例です（実行未検証）。
出力用ディレクトリを作成してから実行してください。

```bat
cl /nologo /std:c++17 /EHsc /W4 /DRAVEN_DATA_TEST_MAIN /IRoot\Root /Fo:build\data-tests\ /Fe:build\data-tests\DataDocumentTests.exe Root\Root\Raven\Core\JsonLexicalReader.cpp Root\Root\Raven\Data\DataUtf8.cpp Root\Root\Raven\Data\DataDocument.cpp Root\Root\Raven\Data\DataElement.cpp Root\Root\Raven\Data\Tests\DataDocumentTests.cpp
build\data-tests\DataDocumentTests.exe
```

`DataDocument::Create`は手動のPreorder Tapeを検証してコピーします。Objectは
StringキーTokenと値の部分木を交互に持ち、`Count`は組数です。String範囲は
`Payload == DecodedUtf8String`なら展開済みUTF-8、`JsonEscapedString`なら引用符を除いたJSON表記です。
Binary範囲は任意バイト列です。数値は`Payload`に保持し、Int64 / Float64のbit表現の
格納・取得には`memcpy`を使います。JSON直接入力では数値のRaw表記とStringのEscape情報も保持します。

`DataElement::GetToken()`のポインタと`TryGetSource()`のViewは非所有参照です。
Documentの破棄・置換・Move後は使わず、新しい所有先から再取得してください。
`IsValid()`は破棄済みDocumentや置換前Handleの検出を保証しません。
Objectの`GetNextSibling()`はキー→値→次のキーの順です。値の部分木はまとめてSkipします。

Tapeの検証とAdapterは再帰を使わず、一時Stackとキー索引を連続領域で保持します。
要素ごとのHeap Objectは作りません。Tokenは現在32 byte（一般的な64 bit ABI）で、
サイズ固定のファイル形式ではありません。独自Allocator・Borrowed Bufferは未導入です。
`FromJsonValue`のSourceは展開済み文字列を連結したBufferであり、元のJSONテキストではありません。
Adapter失敗のOffsetはこのBuffer内、または入力位置を持たない場合は0です。


## Phase 2 JSON Parser

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DRAVEN_JSON_TEST_MAIN \
  -IRoot/Root Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Core/JsonParser.cpp \
  Root/Root/Raven/Data/DataUtf8.cpp Root/Root/Raven/Data/DataDocument.cpp Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Tests/JsonParserTests.cpp -o /tmp/raven-json-tests
/tmp/raven-json-tests
```

Visual Studioの単体ビルド例（実行未検証）です。

```bat
cl /nologo /std:c++17 /EHsc /W4 /DRAVEN_JSON_TEST_MAIN /IRoot\Root /Fo:build\data-tests\ /Fe:build\data-tests\JsonParserTests.exe Root\Root\Raven\Core\JsonLexicalReader.cpp Root\Root\Raven\Core\JsonParser.cpp Root\Root\Raven\Data\DataUtf8.cpp Root\Root\Raven\Data\DataDocument.cpp Root\Root\Raven\Data\DataElement.cpp Root\Root\Raven\Data\Formats\Json\JsonParser.cpp Root\Root\Raven\Data\Tests\JsonParserTests.cpp
build\data-tests\JsonParserTests.exe
```

`Core::JsonParser`と`Data::JsonParser`は`Core/JsonLexicalReader`のString / Number文法を共有します。
既存Core APIはJsonValueを返し、Data APIはJsonValueを経由せずToken Tapeを作ります。
既存Core経路のdouble変換、生UTF-8の受理規則は維持します。エラー文言・Byte位置の完全一致は保証しません。

```cpp
Raven::Data::DataDocument document;
Raven::Data::DataError error;
const bool parsed = Raven::Data::JsonParser::Parse(jsonText, document, &error);
```

Data経路では入力全体をDocumentが所有します。`TryGetSource()`はStringの引用符を除いたRaw表記を返し、
`JsonEscapedString`は未展開です。Unicode / Escapeの妥当性と展開後Byte数はParse時に検査しますが、
文字列値の展開済みCacheは保持しません。重複検査に必要なEscapeキーだけを一時Bufferへ展開します。
公開Decoded取得・キー検索APIは下記のPhase 3を参照してください。

数値は範囲検査とPayload確定をParse時に行います。負の整数はInt64、符号なし整数はUInt64、
小数点・指数を含む表記はFloat64です。Float変換はlocale非依存の`from_chars`を使います。
不正UTF-8、整数範囲外、各DataLimits上限超過は明示的に拒否します。
表現可能なsubnormalは保持し、0へ丸められるUnderflowは拒否します。
これはsubnormalでERANGEを返し得る既存strtod経路との意図した差です。

Parserは反復Stackで処理し、巨大Countを先行Reserveしません。既存のDocument検証・Commitを再利用するため、
Parse中のTapeと確定DocumentのTapeが一時的に共存します。性能・Peak memoryの計測は未実施です。
File I/O、FrameAllocator、Borrowed Buffer、実運用Assetへの移行は追加していません。
エラーは0始まりの入力Byte位置を返し、EOFは入力長、位置を特定できない確保失敗は0です。
行・列は未提供です。失敗時は既存DocumentとそのViewを維持します。

Phase 1は69チェック、Phase 2は276チェックです。サニタイザを使う場合は上記g++へ
`-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`を追加します。
今回の環境はptrace制約でLeakSanitizerを実行できず、`ASAN_OPTIONS=detect_leaks=0`で検証しました。

## Phase 3 DataElement

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
  -DRAVEN_DATA_ELEMENT_TEST_MAIN -IRoot/Root \
  Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Data/DataUtf8.cpp Root/Root/Raven/Data/DataDocument.cpp Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Tests/DataElementTests.cpp -o /tmp/raven-element-tests
/tmp/raven-element-tests
```

Visual Studio Developer Command Promptの例（実行未検証）です。

```bat
cl /nologo /std:c++17 /EHsc /W4 /DRAVEN_DATA_ELEMENT_TEST_MAIN /IRoot\Root /Fo:build\data-tests\ /Fe:build\data-tests\DataElementTests.exe Root\Root\Raven\Core\JsonLexicalReader.cpp Root\Root\Raven\Data\DataUtf8.cpp Root\Root\Raven\Data\DataDocument.cpp Root\Root\Raven\Data\DataElement.cpp Root\Root\Raven\Data\Formats\Json\JsonParser.cpp Root\Root\Raven\Data\Tests\DataElementTests.cpp
build\data-tests\DataElementTests.exe
```

| API | 契約 |
| --- | --- |
| `IsType` / `TryGetType` | 無効Elementはどの型にも一致しない。Nullは明示的な値。 |
| `TryGetBool` | Boolだけを受理し、NumberやStringを暗黙変換しない。 |
| `TryGetRawString` | StringだけのRaw内容。JSON入力では引用符を含まずEscapeを保持する。 |
| `TryGetStringView` | `DecodedUtf8String`のStringだけをDocument参照Viewで返す。Escape済み表記は受理しない。 |
| `TryGetDecodedString` | 呼び出し側のStringへコピーまたはEscape展開する。既存DOM由来の文字列は再展開しない。 |
| `TryGetCount` | Arrayは値数、ObjectはKey / Value組数。スカラーでは失敗する。 |
| `TryGetArrayElement` | 範囲内のArray要素を取得する。indexへの移動はO(index)。 |
| `TryGetObjectMember` | 範囲内のObjectのキーと値を取得する。出力2つに同じ変数は指定できない。 |
| `TryFind` | DecodedキーのByte列で比較する。NULやUnicodeにも対応し、大文字小文字を区別する。 |

すべてのTry APIは失敗時に出力を維持します。型不一致、数値範囲・精度損失、存在しないキー・index、
確保失敗はfalseです。詳細なエラー経路は後続のReader / 型Adapterの責務とします。
`TryFind`は対象のObjectだけを検索し、子孫は再帰検索しません。

| 数値取得 | 許可する変換 |
| --- | --- |
| `TryGetInt64` | Int64そのもの、Int64範囲内のUInt64、整数値かつ`[-2^63, 2^63)`のFloat64。 |
| `TryGetUInt64` | UInt64そのもの、非負のInt64、整数値かつ`[0, 2^64)`のFloat64。 |
| `TryGetFloat64` | Float64そのもの、doubleで正確に表現できるInt64 / UInt64。 |

整数からFloatへの変換は、`2^53`を超えた値を一律に拒否するのではなく、有効bitと下位bitで判定します。
例として`2^53 + 1`は失敗し、`2^53 + 2`や`2^63`は成功します。
Floatから整数への変換は、切り捨てや範囲外キャストを行いません。`-0.0`は整数0へ取得でき、
Float取得なら符号を保持します。Float64はParse時点でdoubleへ丸められた値であり、元表記の精度は復元しません。

```cpp
Raven::Data::DataElement label;
std::string decoded;
if (document.GetRoot().TryFind("label", label) && label.TryGetDecodedString(decoded))
{
    // decodedを所有する側で利用します。
}
```

連続走査は`GetFirstChild` / `GetNextSibling`を使います。Arrayは子の部分木をSkipして次の値へ進み、
Objectはキー→値→次のキーです。Objectのキーから次の組へ進むには兄弟を2回取得します。
index APIを0から繰り返すと全体ではO(n²)となるため、大量の要素の連続走査には使いません。

Source ViewはDocumentの破棄・置換・Moveで無効になります。Decoded Stringは呼び出し側が所有し、
そこへのViewはそのStringの変更・破棄で無効になります。`TryGetDecodedString`の成功も変更に含みます。
無効化済みのViewやElementを読まず、現在の所有先から再取得してください。

Document内に可変Lazy Cacheは追加しません。`TryFind`の展開用Bufferは呼び出し内だけで所有します。
同じDocumentを並列に読む場合は、Elementの出力変数とDecoded StringをThreadごとに用意し、
DocumentのMove・置換・破棄とは競合させないでください。Borrowed Bufferは引き続き未導入です。

Phase 3はGCC環境で2,253チェック（独立したlong double精度判定との照合を含む）と、
4 Worker × 500回の共有Document読み取りを実行します。long doubleがdoubleと同じ精度の環境では、
long doubleを使った追加照合をスキップし、固定の数値境界Fixtureを実行します。
サニタイザは`-fsanitize=address,undefined,float-cast-overflow`を使い、整数境界のキャストも検査しました。
一時Harnessで確保失敗6箇所の出力維持と、Escapeキーを調べない検索・String View取得の無確保経路も確認しました。
ThreadSanitizerとVisual Studio全体ビルドは未実施です。性能計測は後続Phaseの対象です。

## Phase 4 DataReader / DataWriter / JSON出力

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -DRAVEN_DATA_IO_TEST_MAIN -IRoot/Root \
  Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Core/JsonParser.cpp Root/Root/Raven/Core/JsonWriter.cpp \
  Root/Root/Raven/Data/DataUtf8.cpp Root/Root/Raven/Data/DataDocument.cpp Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/DataReader.cpp Root/Root/Raven/Data/DataWriter.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonWriter.cpp \
  Root/Root/Raven/Data/Tests/DataIOTests.cpp -o /tmp/raven-data-io-tests
/tmp/raven-data-io-tests
```

`-DRAVEN_DATA_IO_ALLOCATION_TESTS`を追加すると、専用実行ファイル内だけで`operator new`を
差し替え、Document出力と直接イベント出力の確保失敗箇所を順に注入します。
通常アプリへこのmacroを指定しないでください。GCCで48箇所の確保失敗を含む4,321チェックが成功しました。
macroなしでは4,268チェックです。固定Seedの2,000組のUInt64 / double、整数境界、subnormal、
Escape、Decoded Key順、Binaryと非有限値の拒否、すべてのイベントでのSink失敗、
出力Byte数の境界、深さ512、10,000要素配列、既存Core Writerとの互換Fixtureを含みます。

サニタイザは`-g -O1 -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer`を
追加し、確保失敗macroなしで実行します。この環境では`ASAN_OPTIONS=detect_leaks=0`を指定します。
Visual Studio全体ビルドとThreadSanitizer、性能・Peak Memory計測は未実施です。

### APIと責務

```cpp
Raven::Data::DataDocument document;
Raven::Data::DataError error;
std::string output;
if (Raven::Data::JsonParser::Parse(input, document, &error))
{
    // 全成功時だけoutputを置換します。既存Core::JsonWriterとは別のAPIです。
    Raven::Data::JsonWriter::Write(document, output, &error);
}
```

`JsonWriter::Write(DataElement, ...)`は部分木にも対応します。Documentは変更しません。
`DataReader(root).Read(writer, &error)`はフォーマット非依存のイベント変換経路です。
JSON入力だけでなく、手動Tapeや既存DOM Adapterで作ったDocumentも利用できます。
キー検索と型別取得は`DataReader::GetRoot()`が返す既存`DataElement`で行います。
Deserializeの必須値・既定値・Version・フィールド経路はPhase 5の責務です。

型付きデータは`DataWriter&`へ直接イベントを送れます。フォーマット固有のCommitは具象Writerで行います。

```cpp
Raven::Data::JsonWriter writer;
if (writer.BeginObject()
    && writer.WriteKey("version") && writer.WriteUInt64(1)
    && writer.WriteKey("enabled") && writer.WriteBool(true)
    && writer.EndObject())
{
    writer.Finish(output);
}
```

イベントはRoot 1値、Objectでは`WriteKey`→値、Arrayでは値の順とし、Begin / Endを対応させます。
StringとKeyは展開済みUTF-8、Binaryは任意Byte列です。WriterはViewを呼び出し後に保持しません。
Readerは非所有で、Documentの破棄・置換・Move前に読み終えてください。
未Escape文字列はSource Viewを利用し、Escape展開用Bufferは呼び出し内で所有します。
Readerは再帰を使わず、子・兄弟による線形走査を行います。Writerは単一Threadで利用してください。
同一Documentを別々のReader / Writerで読むことは可能ですが、Documentの変更・破棄とは競合させません。

### JSON契約と制限

- Compact JSONと末尾改行1 Byteを出力します。ObjectはDecodedキーのByte列を
  `std::string`比較でソートし、既存Core Writerと同じ決定性を持たせます。重複キーは`EndObject`で拒否します。
- Int64 / UInt64をdoubleに変換しません。正のInt64は再読込時にUInt64へ分類され得ますが、値・精度は維持します。
  Float64はlocale非依存の`to_chars` / `max_digits10`で出力し、整数表記なら`.0`を付けて
  Float分類と負のZeroを保持します。そのため既存Core Writerとすべての数値表記が同一にはなりません。
- 不正UTF-8、Binary、NaN / Infinityを拒否します。BinaryのBase64化や文字列への暗黙変換は行いません。
- `DataWriteLimits`の出力上限は改行、括弧、Key、区切り、Escape後のByte数をすべて含みます。
  深さは開いたコンテナ数、文字列上限はDecoded UTF-8のByte数、要素数はArrayの値数 / Objectの組数です。
  上限0も有効で、深さ0はScalarだけ、要素数0は空コンテナだけを許可します。
- ソートのためObject内の符号化済みメンバーを保持し、閉じたコンテナを親へ組み込みます。
  JsonValue / Token DOMを構築しませんが、Streamへの逐次出力や定数Memory出力ではありません。
  入れ子の再構成にコピーを伴います。Allocator統合・性能測定は後続の課題です。
- 失敗はstickyです。最初のエラーを`GetError()`で取得し、失敗したWriterは破棄します。
  `Finish`は全コンテナが閉じたRoot 1値を1回だけ公開でき、失敗時は呼び出し側の出力を維持します。
  空入力、未完コンテナ、2つ目のRoot、Finish後のイベントも拒否します。
- Readerのエラー位置は失敗した値・キーのSource Byte位置です。無効Root / 走査用確保失敗は0です。
  Sinkがfalseを返して詳細コードを持たない場合は`OutputFailure`とします。失敗したSinkは再利用しません。
  Writer単体の位置は受理済み符号化Byte数（改行を除く）であり、ソート後の物理位置ではありません。

File I/O・Streamエラーの取扱い、整形出力、型Adapter、Reflection、MessagePack、既存保存経路の移行は追加していません。

## Phase 5 明示的型Adapter

`Data/Serialization/Serialization.h`はフォーマット非依存の型変換層です。
`DataAdapter<T>`を明示的に特殊化し、`Read(DataElement, T&, SerializationContext&)`と
`Write(DataWriter&, const T&, SerializationContext&)`を定義します。通常関数の実装はcpp、
テンプレートの実装は同名inlに置きます。設定型とAdapterはPhase 7で共用化した
`StructuredDataTestAsset.h/.cpp`、型変換の検証例は`SerializationTests.cpp`を参照してください。

- `Deserialize`は既定構築した一時値へ読み込み、Adapterの検証成功後にだけnoexcept swapします。
  入力Documentを変更せず、出力が以前保持していた値を既定値として流用しません。
  Adapterを直接呼ぶ場合は呼び出し側で一時値を用意してください。
- `ObjectReader::Required / Optional`は必須・省略時の既定値を扱います。Nullは省略と区別し、
  既知フィールドの型不一致を拒否します。未知フィールドは無視し、再保存時の保持を保証しません。
  キーはDecoded UTF-8で比較し、検索中の確保失敗を欠落として扱いません。
- `Header`はtypeと正の整数Tokenのversionを検査し、明示した対応範囲だけ受理します。
  旧フィールドの読込・Migration・値域などの業務検証は型Adapterの責務です。
  テストAssetはv1のsamplesをv2のvaluesへ移し、出力はv2に統一します。
- 標準Adapterはbool、通常の整数、float / double、所有string、vectorです。
  数値はDataElementの厳密変換と取得先の範囲を検査します。floatへの縮小も正確に
  表現できる場合だけ許可します（0.1のdouble→floatは拒否）。Boolの数値化は行いません。
  文字列はDocumentから独立して所有し、vectorは子・兄弟APIで線形走査します。
- `SerializationError`は最初の失敗と原因DataError、JSON Pointer経路を保持します。
  Rootは空経路、例は`/values/1`です。`~`と`/`は`~0`と`~1`へEscapeします。
  経路は終端を含む512 Byteで追加確保せず、超過やNULを含むフィールド名は
  LimitExceededとして拒否します。超過時のPathは入る範囲の接頭辞です。
  欠落フィールドは親Object、型変換失敗は対象TokenのSource Byte位置を返します。
- `Serialize`はDataWriterへ直接イベントを送ります。成功後の公開は具象Writerの責務です。
  JsonWriterでは`Serialize`と`Finish`の両方が成功した場合だけ結果を利用してください。
  Adapter失敗時はWriterを破棄します。Adapterの意味検証失敗はWriter自体をstickyに
  しないため、Serialize失敗後にFinishしてはいけません。
- Pointerの既定Adapterはありません。安定IDは利用側の明示的Adapterで保存し、
  読み込み後の参照解決・Engineへの反映は呼び出し側が担当します。
  Reflection、Math / AssetId / EntityId専用Adapterは実用途が必要になる段階まで保留します。

専用CPUテスト:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -DRAVEN_SERIALIZATION_TEST_MAIN -IRoot/Root \
  Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Data/DataUtf8.cpp Root/Root/Raven/Data/DataDocument.cpp Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/DataReader.cpp Root/Root/Raven/Data/DataWriter.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonWriter.cpp \
  Root/Root/Raven/Data/Serialization/Serialization.cpp \
  Root/Root/Raven/Data/Tests/StructuredDataTestAsset.cpp \
  Root/Root/Raven/Data/Tests/SerializationTests.cpp -o /tmp/raven-serialization-tests
/tmp/raven-serialization-tests
```

`-DRAVEN_SERIALIZATION_ALLOCATION_TESTS`で専用実行ファイルだけoperator newを置換し、
Deserializeの確保失敗を順次注入します。通常アプリにはこのmacroを指定しません。
サニタイザはmacroなしで`-g -O1 -fsanitize=address,undefined,float-cast-overflow
-fno-omit-frame-pointer`を追加します。環境制約によりLeakSanitizerは無効化しています。
Windows / MSVCの正式ビルドと実運用Assetへの導入は未検証です。

Phase 5は確保失敗注入あり142チェック、なし118チェックがGCC C++17で成功しました。
ASan / UBSan / float-cast-overflowも118チェック成功し、既存Phase 1〜4の6,866チェックも成功しました。
Project XML、5つの新規ファイルのFilter / structure登録、静的Cloudベースラインと差分検査を確認しました。
ベースラインスクリプト末尾の既存CRは一時コピーだけで除去し、元スクリプトは変更していません。


## Phase 6 MessagePack

`MessagePackParser::Parse(bytes, document, &error)`は長さ付きメモリ入力を所有Documentへ
変換します。`MessagePackWriter`は既存の`Serialize(writer, value)`と接続でき、
`Serialize`と`Finish(bytes)`の両方が成功した場合だけ結果を公開してください。
`MessagePackWriter::Write(document, bytes, &error)`はDocument全体を変換します。
バイト列にはNULが含まれるため、File I/Oはbinary modeと明示長を使ってください。

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DRAVEN_MESSAGEPACK_TEST_MAIN -IRoot/Root \
  Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Data/DataUtf8.cpp \
  Root/Root/Raven/Data/DataDocument.cpp \
  Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/DataReader.cpp \
  Root/Root/Raven/Data/DataWriter.cpp \
  Root/Root/Raven/Data/Serialization/Serialization.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonWriter.cpp \
  Root/Root/Raven/Data/Formats/MessagePack/MessagePackParser.cpp \
  Root/Root/Raven/Data/Formats/MessagePack/MessagePackWriter.cpp \
  Root/Root/Raven/Data/Tests/MessagePackTests.cpp -o /tmp/raven-messagepack-tests
/tmp/raven-messagepack-tests
```

確保失敗注入は`-DRAVEN_MESSAGEPACK_ALLOCATION_TESTS`を追加します。
サニタイザはこのmacroを付けずに`-g -O1 -fsanitize=address,undefined,float-cast-overflow
-fno-omit-frame-pointer`を追加します。環境でLeakSanitizerが実行できない場合は
`ASAN_OPTIONS=detect_leaks=0`を指定し、Leak未検証として扱ってください。

相互互換性テストは上記コマンドのmain macroを`RAVEN_MESSAGEPACK_INTEROP_MAIN`へ、
出力名を`/tmp/raven-messagepack-interop`へ変更してビルドした後に実行します。
Pythonの独立実装`msgpack`（検証時1.2.1）が必要です。

```bash
python Root/Root/Raven/Data/Tests/MessagePackInterop.py /tmp/raven-messagepack-interop
```

検証結果: 通常419チェック、確保失敗注入あり446チェック、独立実装との相互検証254ケースが成功。
既存Phase 1〜4の6,866チェックとPhase 5の142チェック（確保失敗注入あり）も成功しました。
ASan / UBSan / float-cast-overflowは通常419チェック成功、LeakSanitizerは無効です。

### Encoding・所有権・失敗規則

- [MessagePack公式仕様](https://github.com/msgpack/msgpack/blob/master/spec.md)の
  nil / Bool、全整数幅、Float32 / Float64、fixstr / str8 / str16 / str32、
  bin8 / bin16 / bin32、fixarray / array16 / array32、fixmap / map16 / map32を受理します。
- 整数は入力tagの符号を保持します。正のfixintはUInt64です。Writerは最短整数表現を選ぶため、
  非負Int64の再ParseはUInt64になる場合があります。値域を検査する既存Adapterで型を復元します。
- Float32はFloat64へ正確に拡張します。Writerは有限Float64を常にfloat64で出力し、
  負のZero・subnormalを保持します。NaN / InfinityはNumericRangeです。
- Stringは妥当なUTF-8（NUL・制御文字を含む）に限定し、Binaryは任意バイト列です。
  JSONへのBinary出力はUnsupportedTypeです。暗黙Base64化・整数のdouble丸めは行いません。
- MapキーはStringのみ。非文字列キー、全Extension（Timestamp含む）はUnsupportedType、
  予約tag 0xc1はInvalidSyntaxです。Decodedキー重複はDuplicateKeyです。
- Parserは反復Stackを使い、長さPrefix・Countから先行Reserveしません。入力、Token、深さ、
  値長、要素数は既存DataLimitsを適用します。切断入力・巨大Prefixは確保前に拒否します。
- UTF-8と重複検査は既存Documentの検証を共用し、全成功後だけCommitします。
  入力が出力DocumentのSourceを参照する場合も安全です。公開後は既存のView寿命規則に従います。
- DataWriterはBegin時に要素数を受け取らないため、Writerは符号化済み子データを保持し、
  End時にCount Prefixを生成します。DOMは構築しません。Objectはキー順にソートします。
  Payloadと最小Headerは受理時、Header拡大分はEnd時に出力予算へ課金します。
  一時メモリは最終出力サイズそのものには限定されず、ネスト中のコピーやキー索引を含みます。
- Error.OffsetはParserでは入力Byte位置（EOFは入力長）、Writerではその時点の課金Byte数です。
  Documentの検証エラーはPayload位置を指す場合があります。最初のWriter失敗はstickyです。
- File I/O・通信・Reflection・実運用設定の移行は含みません。Windows / MSVCの正式ビルドは未検証です。


## Phase 7 専用設定テストAssetのファイル導入

Phase 0で選定した専用Assetを用い、既存のRuntime設定は変更しません。
`StructuredDataTestAsset.v1.json`を読み、既存のv1 samples→v2 values移行を適用し、
v2の決定的JSONとして保存・再読み込みします。Phase 5テストと同じ型・Adapterを共有します。
未知フィールドは無視し、再保存時に保持しません。既存Core Parser、Dock Snapshot、
Humanoid Profile、glTFの読み書きは引き続き既存経路です。

Windowsでの一括検証（Visual Studio C++ツールセットが必要）:

```powershell
./scripts/test-windows-structured-data.ps1
```

スクリプトはリポジトリルートでCPU専用テストを実行します。Fixtureの相対Pathもルート基準です。
出力は`artifacts/structured-data/`の`msvc-build.log`と`tests.log`です。
Phase 5の単独ビルドにも、共用Adapterを定義する`StructuredDataTestAsset.cpp`のリンクが必要です。
GCCでPhase 7だけを実行する場合、上記Phase 5コマンドの`SerializationTests.cpp`を
`StructuredDataTestAssetTests.cpp`へ、main macroを`RAVEN_TEST_ASSET_MAIN`へ変更します。

データの流れと契約:

- 読込: File I/Oで64 KiB上限を確保前に検査→所有Buffer→JSON Parser→Document→
  Adapterの一時値→全検証後Commit。短いRead・追記を拒否します。
  欠落・破損時は呼び出し側の状態を維持するため、初回起動で既定値を使う場合は
  呼び出し側が既定構築してから読み込みます。型不一致を既定値で隠しません。
- `TestAssetError`はI/O段階、構文のDataError / Byte位置、型変換のフィールド経路を保持します。
  診断の表示先はテスト実行結果です。EngineのUIや汎用Runtimeログへの依存は追加しません。
- 保存: 型検証・64 KiB出力上限を満たしたJSON→同じ親の排他作成ディレクトリ内の一時File→
  Write / Flush / Close成功→WindowsのMoveFileExWによる置換（非Windowsはrename）。
  旧Fileを先に削除せず、置換失敗時は旧Byte列を維持します。一時領域はRAIIで後始末します。
  File I/OとOS APIは専用Asset側だけに置き、DataのParser / Writerへ持ち込みません。
- 保存先の親ディレクトリは事前に用意してください。電源断時の耐久性、敵対的なPath変更、
  複数プロセスの競合制御、保存権限の継承は保証しません。バックアップ復旧は提供しません。

MSVC C++17 / W4: 新規48チェックと既存5,355チェック、合計5,403チェック成功、警告0。
MSVCではlong doubleとdoubleの精度が同じため、既存DataElementテストの追加精度照合をスキップします。
新規テストは旧版移行、新規保存・上書き、canonical v2 Byte列、Escape、未知・省略フィールド、
型・構文・Version・入力上限ちょうど／1超過、出力上限、失敗時の設定維持を確認します。
WindowsではDelete共有なしのHandleで実際に置換を失敗させ、元Fileと一時領域の後始末も確認します。
Write / Flush / CloseのOS障害注入、競合中の短いRead、非Windowsの置換経路、
Phase 7のSanitizerは未検証です。旧PhaseのGCC / Sanitizer実績とは区別してください。

アプリのDebug増分ビルドはコンパイル成功後、通常のPDB生成で既存の環境問題
`LNK1101: MSPDB140.DLL`不一致によりリンク失敗しました。既存の診断オプション
`/p:RavenGenerateLinkDebugInformation=false`を指定した増分ビルドではリンク成功しました。
既存ログにもあるCRT競合警告LNK4098が1件あります。PDBありの正式ビルドとGUI実行は未検証です。


## Phase 8 計測・並列Deserialize

`Data/Serialization/ParallelDeserialize.h`の`DeserializeArray<T>`は独立した配列要素を
既存JobSystemへ連続範囲で投入する明示的APIです。既存`Deserialize`は変更しません。
APIは既定で逐次。試験設定型で実測した条件は4 Workerに対して
`EnableParallel=true / MinElements=128 / MinTokens=1409`です。
他のAdapterや実機へそのまま流用せず、計測して設定してください。

- 入力は検証済みDocumentのArray。Document、Pool、取消Flagを関数終了まで生存させます。
  同じPoolのWorkerから同期呼び出ししないでください（全Workerが待つとdeadlockします）。
- 出力は一時vectorを先に確保し、各Workerは重ならない要素だけを変更します。
  エラーと取消を確認し、投入済みJobを全回収後、全成功時だけ呼び出し側へswapします。
- `ArrayReadResult`で状態、投入Job数、並列利用有無、失敗件数、最小indexとそのフィールド診断を返します。
  要素失敗は途中停止せず全件を集計します。取消時は処理済み範囲だけを集計します。
  取消は要素間の協調方式で、Adapter内の長い処理を強制停止しません。
- Adapterは並列安全で、入力や共有状態を変更せず、出力を所有値として生成してください。
  共有Cacheを追加せず、IDの参照解決・Scene/GPU変更は呼び出し側の成功後Commitに分離します。
  `vector<bool>`は保存領域の共有を避けるため対象外です。
- 計測は専用実行ファイルのCSVだけに出力し、ゲーム中のログやDiagnostics UIは変更しません。
  既存AllocationMeasurementは変更せず、Worker合算Peakの追跡は専用macroのビルドに限定します。

```powershell
./scripts/test-windows-structured-data.ps1
./scripts/test-windows-structured-data.ps1 -Benchmark
./scripts/test-windows-structured-data.ps1 -Benchmark -MeasureMemory
```

通常CPU検証は新規37、既存5,403、合計5,440チェック成功、警告0。
メモリ専用ビルドでは確保失敗注入を含む新規50チェックと、既知Peak / 世代切替 /
Aligned確保 / Worker間の確保解放を確認しました。時間とメモリ追跡は別ビルドで測定しています。
結果、測定定義、生CSV、未検証事項は[Phase 8計測結果](PHASE8_MEASUREMENTS.md)を参照してください。
MSVC以外・Sanitizer・ThreadSanitizer・GUIは未検証。アプリDebugはPDB生成無効でリンク成功し、
既存LNK4098が1件あります。既知の通常PDB生成環境問題は未解消です。


## Phase 9 編集Document / Editor Command

`MutableDataDocument`へ不変Elementをコピーし、挿入・削除・置換した後にFreezeできます。
Slot再利用はGeneration、Document再構築はOwner、Compact / Reset / Undo / Redo復元はEpochで
古いHandleを拒否します。成功した変更はすべての編集String Viewを無効化します。
編集は単一Threadに限定し、Workerへ渡すDocumentはMain Threadで先にFreezeしてください。

Editor側は`DataDocumentEditCommand`を既存Historyへ登録します。Scene / Sessionの寿命、
履歴外の変更、Scene交換による履歴世代を検証し、前後SnapshotでUndo / Redoを行います。
履歴の容量は変更前に確保し、一時的なOutOfMemoryは再試行できるよう履歴に残します。
Data層へSceneやFile I/Oは追加しません。専用Assetで編集・Undo / Redo・保存・再読み込みを検証します。

```powershell
./scripts/test-windows-structured-data.ps1
./scripts/test-windows-structured-data.ps1 -AllocationTests
# アプリのビルド済み実行ファイルを利用する場合
./scripts/test-windows-scene.ps1 -SkipBuild -SkipBootstrap
```

MSVC通常テストは既存5,440＋新規65＝5,505チェック成功、警告0。
確保注入付きの編集テストは610チェック、Editor Commandは50チェック成功。
既存Scene / Editor回帰も成功しました。アプリDebugは既存PDB無効オプションでリンク成功しています。
詳細なAPI契約、対象ファイル、データの流れ、検証制限は[Phase 9編集Document](PHASE9_EDITING.md)を参照してください。
初期版は編集ごとの全体Copy・検証とSnapshot履歴を使います。大規模Assetの編集性能とByte単位の
履歴予算は未対応です。GUI、Sanitizer、通常PDB生成の環境問題も未検証・未解消です。
