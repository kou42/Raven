# Structured Data Phase 1〜3 単体テスト

GPU・Window・外部ライブラリに依存しないC++17のテストです。通常アプリケーションでは
`RunDataDocumentTests()`を提供し、`RAVEN_DATA_TEST_MAIN`指定時だけ専用mainを定義します。
Phase 2の直接JSON ParserとPhase 3のDecoded取得・数値変換・キー検索APIを含みます。

リポジトリルートで実行します。

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DRAVEN_DATA_TEST_MAIN \
  -IRoot/Root Root/Root/Raven/Core/JsonLexicalReader.cpp \
  Root/Root/Raven/Data/DataDocument.cpp \
  Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/Tests/DataDocumentTests.cpp -o /tmp/raven-data-tests
/tmp/raven-data-tests
```

Visual Studio Developer Command Promptでの単体ビルド例です（実行未検証）。
出力用ディレクトリを作成してから実行してください。

```bat
cl /nologo /std:c++17 /EHsc /W4 /DRAVEN_DATA_TEST_MAIN /IRoot\Root /Fo:build\data-tests\ /Fe:build\data-tests\DataDocumentTests.exe Root\Root\Raven\Core\JsonLexicalReader.cpp Root\Root\Raven\Data\DataDocument.cpp Root\Root\Raven\Data\DataElement.cpp Root\Root\Raven\Data\Tests\DataDocumentTests.cpp
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
  Root/Root/Raven/Data/DataDocument.cpp Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Tests/JsonParserTests.cpp -o /tmp/raven-json-tests
/tmp/raven-json-tests
```

Visual Studioの単体ビルド例（実行未検証）です。

```bat
cl /nologo /std:c++17 /EHsc /W4 /DRAVEN_JSON_TEST_MAIN /IRoot\Root /Fo:build\data-tests\ /Fe:build\data-tests\JsonParserTests.exe Root\Root\Raven\Core\JsonLexicalReader.cpp Root\Root\Raven\Core\JsonParser.cpp Root\Root\Raven\Data\DataDocument.cpp Root\Root\Raven\Data\DataElement.cpp Root\Root\Raven\Data\Formats\Json\JsonParser.cpp Root\Root\Raven\Data\Tests\JsonParserTests.cpp
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
  Root/Root/Raven/Data/DataDocument.cpp Root/Root/Raven/Data/DataElement.cpp \
  Root/Root/Raven/Data/Formats/Json/JsonParser.cpp \
  Root/Root/Raven/Data/Tests/DataElementTests.cpp -o /tmp/raven-element-tests
/tmp/raven-element-tests
```

Visual Studio Developer Command Promptの例（実行未検証）です。

```bat
cl /nologo /std:c++17 /EHsc /W4 /DRAVEN_DATA_ELEMENT_TEST_MAIN /IRoot\Root /Fo:build\data-tests\ /Fe:build\data-tests\DataElementTests.exe Root\Root\Raven\Core\JsonLexicalReader.cpp Root\Root\Raven\Data\DataDocument.cpp Root\Root\Raven\Data\DataElement.cpp Root\Root\Raven\Data\Formats\Json\JsonParser.cpp Root\Root\Raven\Data\Tests\DataElementTests.cpp
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
