# M4: Shared Definition / Instance State

## 採用範囲と責務

UIButtonだけを対象に、標準色のInstance内複製をなくす。Context接続中は既存UITheme.Buttonを参照し、未接続時はcpp内の不変な標準Styleを共有する。Themeそのものを新しいRegistryへ移す変更は行わない。

4色と4つのOverride flagを常時保持する方式から、初回SetColorでStyleOverrides（4色とbit mask）を一度だけ確保する方式へ変更した。UIButtonがScopeで所有し、破棄も担当する。宣言のみの不完全型を扱うためDestructorをcppへ定義した。未指定の色は他の色をOverrideした後もContextのThemeに追従する。Themeと同値の明示指定もOverrideとして記録し、将来のTheme変更で失われない。ContextへのポインタをStyleに保存しないため、Detach/移動後も参照先を現在のContextから決める。

Hover/Pressed/FocusはUIElementとUIContext、Scroll位置はUIScrollView、Text/IME編集状態はUIInputTextに保持する既存境界を維持する。Click HandlerもButtonごとの所有である。UIElementのunique_ptr Tree、Detach時のContext入力状態解除、Widgetの生成/破棄順は変更しない。他WidgetのStyleや入力状態を共通Stateへ移していない。

## 測定条件

2026-11-18、Windows x64、MSVC 14.44.35207。CPU単体、Font/Window/GPUなし。100 / 1,000 / 10,000個、Warmup 2回 + 測定5回の平均。生成時間は計測Wrapperと初期状態検査も含む。各回で全個生成、先頭0 / 10 / 100%のButtonに4色指定、全個の色を16回読取り、入力状態を変更、全個破棄する。これは制御された生成/破棄頻度であり、Editor実操作中の発生頻度やフレーム時間ではない。

対象は実UIButton、旧保持方式と遅延方式の比較モデル、UIPanel、UILabel、UIInputNumber、UIScrollView。各々heapと既存PoolAllocatorのplacement構築を比較する。比較モデルはsizeofと確保の構造確認用で、Runtime描画やGetContextコストを再現するものではない。改善の判断では変更前後の実UIButtonを優先する。

Allocationは既存AllocationMeasurementのglobal C++ new置換で計測する。今回の測定時は一時propsでフラグをAllocationMeasurement.cppだけへ定義した。他の翻訳単位はこのフラグを使わないことを検索確認しており、既存RavenUIAllocationProfiling=trueと同じnew置換を行う。通常構成への切替時にMSVC増分LTCGキャッシュが残りLNK2005/LNK1000となったため、該当iobj/ipdbを退避し再リンクした。共有中間出力を使う一時propsは削除し、再実行では既存スイッチでIntDir/OutDirを分離する。Available=1の行だけを有効な計測として扱う。malloc、Driver、別Thread、heap管理領域は含まない。通常ビルドは置換なしで別途確認する。

CSVのobject_bytesはsizeof(T)×個数で、子Widgetや文字列等の別確保を含まない。style_bytesは個別Style確保要求量、shared_bytesは共有標準Style 1個分。create/override_allocated_bytesはその区間の累積new要求量であり、現在使用中メモリではない。owner_bytesは計測用ポインタ配列のcapacity分。pool_reserved_bytesはAllocator本体、alignment込みスロット容量、size_t/bool管理配列を含むが、heap管理領域と計測用Batch本体は含まない。setup列はPoolとBatch/ポインタ配列の初期確保を別集計する。Poolを使用しても複合Widget内部の子確保は残る。

各スロットはtyped destructor実行後にPoolへ返す。次の構築時にHover/Pressed/Focus/Nameが初期状態であること、破棄後の使用スロット数0を確認する。PoolはBatchより長く生存する計測専用所有であり、Runtimeのunique_ptr deleterを変更しない。

代表Panel fixtureはInspector / Hierarchy / Statisticsを同一ContextにAttachする初期構築（Font=null、Entity未選択）。Buttonは10個、個別色Overrideは0個だった。値の一致ではなくThemeメンバとGetterの参照先を比較するため、Themeと同値の明示指定も区別する。全Editor、選択Entityあり、実操作後の割合を示す結果ではない。

## 結果と判断

生データは[測定CSV](UI_MODERNIZATION_M4_MEASUREMENTS.csv)（234行、各構成78ケース）に保存した。変更前実ButtonはReleaseで取得し、変更後はDebug/Releaseで取得した。Debugの変更前実Buttonは未測定で、Legacy比較モデルのみを含む。計測exeと全回帰実行は終了コード0。最後の記録用測定はビルドや他テストと同時実行せず、Baseline Release → 変更後Release → 変更後Debugの順に実行した。独立した長期統計ではなく、時間差はホスト負荷・allocator・コンパイラ最適化の影響を受ける。

Releaseのsizeof(UIButton)は480 → 416 bytes（64 bytes、13.3%減）。Debugは変更後432 bytes（Legacyモデル496 bytes）。別確保Styleは両構成で68 bytes。未接続用共有Styleはプロセス内64 bytesである。Release 10,000個の比較は以下（pointer配列、heap管理領域を除く）。

| 個別指定率 | 変更前のObject | 変更後のObject + Style + 共有標準色 | 差 | 追加確保回数/生成周期 |
| --- | ---: | ---: | ---: | ---: |
| 0% | 4,800,000 B | 4,160,064 B | -639,936 B | 0 |
| 10% | 4,800,000 B | 4,228,064 B | -571,936 B | 1,000 |
| 100% | 4,800,000 B | 4,840,064 B | +40,064 B | 10,000 |

要求サイズだけの損益分岐は約94.1%だが、個別確保のheap管理領域を含めれば下がる。代表Panel初期構築が標準10/10であることと、0/10%での保持量削減を採用理由とする。全個個別指定が多い用途への一般化はしない。

Release、heap、10,000個の平均時間（ms、5回）:

| 個別指定率 | 生成 前→後 | 4色指定 前→後 | 16回色読取 前→後 | 破棄 前→後 |
| --- | ---: | ---: | ---: | ---: |
| 0% | 1.242 → 1.171 | 約0 → 約0 | 0.974 → 0.810 | 0.536 → 0.484 |
| 10% | 1.196 → 1.109 | 0.027 → 0.079 | 0.912 → 0.786 | 0.489 → 0.500 |
| 100% | 1.206 → 1.132 | 0.211 → 0.793 | 0.841 → 0.849 | 0.507 → 0.717 |

全個Override時の設定・破棄コスト増を確認した。読取は描画フレームやGPU Submitの代用ではない。採用は確定的な保持量削減に限定し、一律の高速化を主張しない。

Pool比較（変更後Release、10,000個、標準設定）:

| Widget | sizeof | heap生成時new件数 | Pool生成時new件数 | heap生成→Pool生成 ms | Pool予約量 B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Button | 416 | 10,000 | 0 | 1.171 → 0.239 | 4,250,104 |
| Panel | 368 | 10,000 | 0 | 1.081 → 0.284 | 3,770,104 |
| Label | 552 | 10,000 | 0 | 1.427 → 0.394 | 5,610,104 |
| InputNumber | 520 | 30,000 | 20,000 | 4.839 → 3.614 | 5,290,104 |
| ScrollView | 440 | 50,000 | 40,000 | 4.585 → 3.721 | 4,490,104 |

Pool予約量には子Widgetは含まれない。setupの確保件数・量はCSVに別記し、時間は測っていないため、初回Pool構築込みの総時間改善とは解釈しない。定常再使用での短縮はあるが、固定容量と所有権変更の費用を正当化する実Editor生成頻度までは未計測のため、Runtimeには導入しない。

## 検証

| 項目 | 結果 |
| --- | --- |
| Allocation計測付きDebug/Releaseビルド・全UI回帰 | 成功、各終了コード0 |
| 通常Debug/Releaseビルド・全UI回帰 | 成功、各終了コード0。通常はAllocation Available=false |
| 専用Lifecycle計測 | Baseline Release、変更後Debug/Release、各78ケース成功。全行Available=1 |
| 新規回帰 | 標準色参照共有、同値明示Override、全4色優先順位、未指定色Theme追従、Context移動、Detach時入力解除、2 Buttonの入力/Click独立性、初回だけStyle確保 |
| 既存OpenGL GPU統合Debug | 成功。Font/Texture upload、Cache off/on、共有Context描画・切り離し/復帰各3周期 |
| 差分/構成確認 | git diff --check成功。Runtimeファイル追加/移動なし |
| 実Editor手操作・異DPI・DX12/Vulkan UI | 未実施 |

通常Releaseへの切替で発生したLNK2005/LNK1000は増分LTCGのiobj/ipdbを退避した再リンクで解消し、その後のビルドと回帰は成功した。Clean Compileは行っていない。既存C4244（UI回帰のfloat変換）とDebug LNK4098は継続し、今回追加箇所のWarningは確認されなかった。ホスト固有のGLFW Native Header補完とRavenGenerateLinkDebugInformation=falseを継続使用した。

ログはui-m4-*-build.log / ui-m4-*-tests.log / ui-m4-*-measurements.log。生成・読取ベンチマークでは描画せず、GPU成功も全Panel操作確認とは扱わない。

## 再実行

```powershell
# リポジトリRootで実行。ホスト固有のGLFW 3.4 native header補完を使用。
$env:CL = '/ID:\Engine\artifacts\sg3-deps'
msbuild Root/Root.sln /m /p:Configuration=Release /p:Platform=x64 /p:RavenUITest=true /p:RavenGenerateLinkDebugInformation=false /p:RavenUIAllocationProfiling=true
Set-Location Root/Root
& D:/Engine/Root/x64/Release/AllocationProfile/RavenUITests.exe --measure-ui-widgets
# 引数なしは通常の全UI回帰。Debugも同じ手順でConfigurationと出力先を変更する。
```

変更前BaselineはM3のmaster上のUIButtonに同じ測定コードを追加して取得した。Baseline exeを別OutDirへ保存し、変更後との比較を可能にした。通常ビルドはRavenUIAllocationProfiling=trueを省略する。OpenGL統合はRavenUITestの代わりにRavenUIGPUTest=true、RAVEN_UI_TEST_FONT=C:/Windows/Fonts/arial.ttfで既存GPUテストを実行する。

## 注意点と次の候補

全Buttonへ個別色を設定すると、Style確保が1個につき1回増える。小さい色だけをOverrideしても4色分の68 bytesを確保する設計なので、個別指定が非常に多い用途には利益がない。共有Themeへまとめられる色はContextのThemeで設定する。heapの実常駐量削減やフレーム高速化は保証しない。

Poolは繰返し生成コストを下げる一方、固定最大容量の予約、型別管理、既存Treeのdeleter/Detach/Lifetime設計が必要になる。今回の定常PanelではまずStyle複製削減だけを採用し、RuntimeへのPool導入は見送る。入力状態の共有や全WidgetのDefinition/Instance分離を一括導入する根拠にはしない。

新しいRuntimeソースファイルや配置変更はないため、structure.txtとvcxproj.filtersの更新は不要。新しい測定用propsは残さず、既存の計測専用スイッチを再利用する。次はM5 Geometry Builder / RHI UI Backendだが、別途範囲提示と承認が必要であり自動開始しない。
