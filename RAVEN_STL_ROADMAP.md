# Raven STL / Core Containers Roadmap

## 目的

Raven の Memory System と統合された Core Container Library を段階的に構築するためのロードマップです。

この文書では便宜上「Raven STL」と呼びますが、標準 C++ Library を全面的に再実装すること自体は目的にしません。Raven 固有のメモリ寿命、Profiler、Hot Path、ABI 境界、固定容量・一時領域といった要求に価値がある箇所から独自化します。

基本方針は次の通りです。

- 既存の `std::vector` / `std::unordered_map` / `std::string` を一括置換しない。
- Raven Memory System を先に安定させ、その上に Container 層を構築する。
- 標準 Library で十分な箇所は標準 Library を継続利用する。
- 独自 Container は計測可能な目的を持って追加する。
- Public API を Raven 型へ寄せる場合も、内部実装を後から交換可能にする。
- Hot Path では allocation 回数、Peak memory、cache locality、Frame lifetime を重視する。
- Debug / Test / Benchmark を実装と同時に整備する。

この文書は 2026-09-29 時点の `master` を基準とします。

---

## 1. 現在地点

### Memory 基盤

以下は実装済みです。

- [x] `Allocator` 共通インターフェース。
- [x] alignment 指定付き `Allocate()`。
- [x] `Deallocate()` / `Reset()`。
- [x] Capacity / Used Memory / Peak Used Memory / Allocation Count 計測。
- [x] `LinearAllocator`。
- [x] `FrameAllocator`。
- [x] `STLAllocatorAdapter<T>`。
- [x] `FrameVector<T>`。
- [x] Memory Allocator Self Tests。

現在の基本構造:

```text
Allocator
    |
    +-- LinearAllocator
    |
    +-- FrameAllocator
            |
            +-- LinearAllocator

Allocator
    |
STLAllocatorAdapter<T>
    |
std::vector<T, STLAllocatorAdapter<T>>
    |
FrameVector<T>
```

### 実運用

- [x] Physics BroadPhase の一時 Pair 配列で `FrameVector` を利用。
- [x] Physics FrameAllocator の使用量 / Peak / Allocation Count を計測。
- [x] SoftBody / Cloth Solver の Temporary Allocation で FrameAllocator を利用。
- [x] Heap / FrameAllocator の比較計測経路を用意。

### 独自 Container

- [x] `FlatHashSet`。
  - Open Addressing。
  - Linear Probing。
  - Tombstone。
  - Power-of-two Capacity。
  - Load Factor 管理。
  - Raven `Allocator` 対応。
  - Frame / LinearAllocator 利用を考慮した `reserve()`。

したがって Raven STL はゼロから開始するのではなく、Memory System と最初の独自 Container が既に存在する状態から発展させます。

---

## 2. 非目標

初期 Raven STL では以下を目標にしません。

- ISO C++ STL の完全互換実装。
- `std::vector` / `std::string` / `std::unordered_map` の機械的な全面置換。
- 標準 Library と同一 API をすべて再現すること。
- 独自実装であること自体を目的とした最適化。
- Benchmark なしで「独自 Container の方が高速」と仮定すること。
- Raven 外部 API まで独自 Iterator / Algorithm へ一度に移行すること。

Raven 固有の要求がない型については `std::*` の利用を許容します。

---

## 3. 設計原則

### 3.1 Container と Allocation Policy を分離する

Container が直接 OS heap や `new/delete` に依存しない構造を基本とします。

```text
Raven Container
       |
       v
Raven::Allocator
       |
 +-----+------+---------+
 |            |         |
Heap       Frame      Pool
 |            |         |
 +------------+---------+
```

### 3.2 Allocator の寿命を明確にする

外部所有 Allocator を参照する Container は、Allocator が Container より長生きすることを契約として明記します。

FrameAllocator のメモリを永続 Container へ保持しないことを API / assert / test で守ります。

### 3.3 Frame / Persistent を区別する

同じ「可変長配列」でも用途を分けます。

```text
Persistent
    Raven::Vector<T>

Frame / Step temporary
    Raven::FrameVector<T>

Small local collection
    Raven::SmallVector<T, N>

No heap allocation
    Raven::FixedVector<T, N>
```

### 3.4 Profiler で判断する

独自化の判断材料:

- Allocation Count
- Allocated Bytes
- Peak Used Memory
- Capacity
- Reallocation Count
- Container Size
- Load Factor
- Probe Length
- CPU Time
- Cache locality が問題となるアクセスパターン

---

# Phase 1: Allocator Foundation 完成

## 1-1. HeapAllocator

通常の長寿命 allocation 用の Raven Allocator を追加します。

目標:

- [ ] `HeapAllocator : Allocator`。
- [ ] alignment 対応。
- [ ] 個別 `Deallocate()`。
- [ ] Used / Peak / Allocation Count。
- [ ] overflow / invalid alignment test。
- [ ] Debug build で double free / invalid pointer を検出可能な設計を検討。

完了条件:

- Raven Container が標準 heap を直接意識せず Persistent allocation を行える。
- Linear / Frame と同じ `Allocator` API から利用できる。

## 1-2. FreeListAllocator

可変サイズ allocation と個別解放を扱える Arena 系 Allocator を追加します。

- [ ] Free block 管理。
- [ ] split。
- [ ] adjacent free block coalescing。
- [ ] alignment。
- [ ] fragmentation statistics。
- [ ] stress test。

主用途候補:

- Resource metadata。
- Scene lifetime allocation。
- Persistent Container backing memory。

## 1-3. PoolAllocator

固定サイズ Object 向け Pool を追加します。

- [ ] Fixed-size slot。
- [ ] Free list。
- [ ] O(1) allocate / deallocate。
- [ ] alignment。
- [ ] Capacity / Used / Peak。
- [ ] exhaustion test。

主用途候補:

- Physics proxy。
- ECS / internal node。
- Command node。
- 頻繁に生成破棄される同一サイズ Object。

---

# Phase 2: Memory Tracking / Diagnostics

## 2-1. MemoryTag

Subsystem ごとの allocation を分類します。

候補:

```cpp
enum class MemoryTag
{
    Core,
    Renderer,
    RHI,
    Physics,
    Animation,
    Audio,
    Resource,
    Scene,
    UI,
    Temporary
};
```

- [ ] Tag 定義。
- [ ] Allocator / tracking との接続。
- [ ] Tag ごとの Current / Peak Bytes。
- [ ] Tag ごとの Allocation Count。

## 2-2. MemoryTracker

Debug / Profiling 用の中央診断層を追加します。

- [ ] Allocation size。
- [ ] Alignment。
- [ ] Allocator type。
- [ ] MemoryTag。
- [ ] Current / Peak。
- [ ] optional file / line。
- [ ] leak report。
- [ ] Profiler counter 接続。

重要:

Release Hot Path に不要な追跡コストを強制しない構造にします。

---

# Phase 3: Raven::Vector

Raven STL の最初の汎用 Sequence Container とします。

## 3-1. API 契約

初期 API 候補:

- [ ] constructor / destructor。
- [ ] move。
- [ ] copy。
- [ ] `size()` / `capacity()` / `empty()`。
- [ ] `data()`。
- [ ] `operator[]`。
- [ ] `front()` / `back()`。
- [ ] `begin()` / `end()`。
- [ ] `clear()`。
- [ ] `reserve()`。
- [ ] `resize()`。
- [ ] `push_back()`。
- [ ] `emplace_back()`。
- [ ] `pop_back()`。
- [ ] erase / insert は必要性を確認して追加。

初期段階では標準 `std::vector` の全 API 互換を要求しません。

## 3-2. 実装戦略

最初から独自 Storage 実装に固定しません。

候補 A:

```text
Raven::Vector
    |
std::vector + Raven Allocator Adapter
```

候補 B:

```text
Raven::Vector
    |
Raven native contiguous storage
    |
Raven::Allocator
```

Phase 3 開始時に Benchmark / API requirement を確認し、A から開始して B へ移行可能な Public API にします。

## 3-3. 必須 Test

- [ ] trivial type。
- [ ] non-trivial type。
- [ ] move-only type。
- [ ] over-aligned type。
- [ ] constructor / destructor count。
- [ ] reserve / growth。
- [ ] exception / allocation failure。
- [ ] empty container。
- [ ] large allocation overflow。
- [ ] Iterator compatibility。

---

# Phase 4: Specialized Sequence Containers

## 4-1. SmallVector<T, N>

Stack / inline storage を優先し、N を超えた場合のみ Allocator を利用します。

主用途:

- Contact points。
- Small child lists。
- Render state lists。
- 小規模 command collection。

- [ ] Inline storage。
- [ ] overflow storage。
- [ ] move / destruction。
- [ ] inline -> heap transition test。
- [ ] Benchmark vs Vector。

## 4-2. FixedVector<T, N>

Heap allocation を禁止した固定最大容量 Container。

- [ ] Inline fixed storage。
- [ ] Runtime size。
- [ ] overflow assert / explicit failure policy。
- [ ] no allocation guarantee。

主用途:

- Hot Path。
- deterministic temporary data。
- 上限が仕様で決まっている collection。

## 4-3. FrameVector

既存 `FrameVector` を維持しつつ、`Raven::Vector` 完成後に位置付けを再評価します。

**Phase 4-3の現時点の判断:** `FrameVector<T>` は `std::vector<T, STLAllocatorAdapter<T>>` の互換エイリアスとして継続します。`BroadPhase::ComputePairs()` が `get_allocator()` からBacking Allocatorを検出し、一時Setを同じFrameAllocatorへ割り当てるためです。`Raven::Vector` の現行Public APIには `get_allocator()` がなく、置換するとこの連携を失います。

**移行条件:** (1) BroadPhaseがコンテナ非依存にAllocatorを取得できること、(2) Physics/SoftBodyの既存テストとフレームReset境界を維持すること、(3) FrameAllocatorのno-op Deallocateによる成長時のArena消費量をBenchmarkで比較すること。条件を満たすまでは既存型を変更しません。

選択肢:

- STL-backed FrameVector を継続。
- Raven::Vector + FrameAllocator に統合。
- alias として compatibility layer を維持。

既存 Physics / SoftBody を壊さず段階移行します。

---

# Phase 5: Hash Containers

## 5-1. FlatHashSet 強化

既存実装を Raven STL の正式 Container として強化します。

- [x] Iterator。
- [x] const Iterator。
- [x] erase Iterator。
- [x] Tombstone cleanup policy。
- [x] Probe length statistics。
- [x] reserve / rehash Benchmark。
- [x] adversarial hash test。
- [x] over-aligned Key test。
- [x] exception safety 確認（Hasher / Key copy例外時のロールバックとmove-only KeyをSelf Testで確認）。

**Phase 5-1 検証メモ:** Benchmarkは `--benchmark-flat-hash-set` で起動し、reserved / growth / erase-insert の中央値、実行時間比、両Containerのallocation回数・peak bytes・capacity、最大／平均Probe距離、Tombstone数を出力します。診断走査と破棄は計測区間外です。Debug / Releaseの両構成とSelf Testを2026-10-25に実行済みです。

## 5-2. FlatHashMap

`FlatHashSet` の知見を利用して Map を追加します。

- [x] Key / Value storage。
- [x] find。
- [x] contains。
- [x] insert / emplace。
- [x] erase。
- [x] reserve。
- [x] Iterator。
- [ ] heterogeneous lookup は必要性を確認後に追加。

**Phase 5-2 検証メモ:** `--test-flat-hash-map` で基本操作、衝突時のLinear Probing、Tombstone、
Iterator / const Iterator、move-only / over-aligned Value、Move操作、rehashと要素構築の例外安全性、
容量overflowを確認します。Benchmarkは `--benchmark-flat-hash-map` でreserved / growth / erase-insertを
128 / 1,024 / 8,192 / 65,536件、ウォームアップ3回・計測9回の中央値として比較します。
Debug / Releaseの両構成、Self Test、Benchmarkを2026-10-25に検証済みです。

**Phase 5-2 Rehash最適化メモ（2026-10-26）:** Copy可能なKey / Valueは旧テーブルを変更せず
新Slotへ直接構築できるため、再配置先を保持していた補助`std::vector`と二段階走査を除去しました。
move-only ValueでHasherが例外を送出し得る経路は、移動前に全Hashを評価するfallbackを維持します。
初回reserveは後続insertのcache localityを保つ走査を残し、Growth時は既知のsizeを再利用して新Slotの
全再集計を省きます。同一環境の中央値で1,024件Growthは59.7usから43.1us、65,536件Growthは
5,145.0usから3,821.3us、65,536件Rehash Onlyは1,919.3usから912.0usへ改善しました。
Probe / Tombstone診断、Collision Heavy、Tombstone cleanup比較、標準Mapとの決定的ランダム操作テストも
追加し、Debug / Release buildとSelf Testを再検証済みです。

用途候補:

- Resource lookup。
- Entity / Component lookup。
- Renderer cache。
- Physics lookup table。

---

# Phase 6: Utility Containers / Views

優先度は利用箇所が発生した順に決定します。

候補:

- [x] `Raven::Array<T, N>`。
- [x] `Raven::Span<T>`。
- [x] `Raven::RingBuffer<T>`。
- [x] `Raven::BitSet<N>`（固定長。DynamicBitSetは未実装）。
- [x] `Raven::HandlePool<T>`。
- [x] `Raven::SparseSet`。

Phase 6の固定長・非所有ContainerはC++17向けに実装済みです。`SparseSet`の疎配列は
256 ID単位のPageへ分割し、離れた大きなID 1件でID値に比例した巨大な連続確保が
発生しない構成にしています。`HandlePool`のHandleはPool識別子を持たないため、異なる
Pool間でHandleを混用しない所有境界は引き続き呼び出し側が管理します。

特に ECS / Physics / Rendering の要求が明確になった場合は `HandlePool` / `SparseSet` を優先します。

---

# Phase 7: String / Name System

String は Container 群が安定してから着手します。

## 7-1. Raven::String

目的を明確にしてから実装します。

検討事項:

- [x] UTF-8 を標準内部表現にする。API上のsize / indexはbyte単位とし、暗黙の正規化は行わない。
- [x] SSO。初期実装では`std::basic_string`のSSOと例外安全性を利用する。
- [x] Allocator 対応。`STLAllocatorAdapter<char>`を介して外部所有のRaven Allocatorへ接続する。
- [x] StringView。所有Stringと分離した非所有byte viewとして実装する。
- [ ] formatting。用途と依存関係を確認して別段階で追加する。
- [x] path / asset name との責務分離。String自体へpath正規化やAsset固有規則を持ち込まない。

**Phase 7-1 初期実装メモ（2026-10-27）:** `Raven::String`は既存`Vector`と同様に、
Raven Allocatorと標準Libraryの成熟したStorage実装を接続する段階導入としました。
AllocatorはStringより長生きし、個別解放をサポートする必要があります。既存の`std::string`は
一括置換せず、API境界と実測上の価値が明確な箇所から移行します。
Debug / Release build、`--test-string`、既存`--test-memory-containers`を2026-10-27に検証済みです。

## 7-2. Raven::StringView

所有しない文字列 View。

標準 Library との相互運用性を重視します。

- [x] `const char*` / pointer + size / `std::string` / `std::string_view`から構築できる。
- [x] embedded nullを含むbyte列を保持できる。
- [x] `substr` / prefix・suffix除去 / compare / findを提供する。
- [x] 終端nullを保証せず、参照元Lifetimeと再確保による無効化を呼び出し側契約とする。

## 7-3. Raven::Name

大量比較される識別子向けの Interned / Hashed Name System を必要性に応じて追加します。

候補用途:

- Asset identifier。
- Animation state / bone name。
- Material parameter。
- Component / property identifier。

String と Name を同一概念にしないことを基本とします。

- [x] `NameTable`がUTF-8文字列をInternし、`Name`はTable + indexの軽量Handleとして保持する。
- [x] 同一Table内の同一文字列を同じNameへ統合し、異なるTableのNameは明示的に区別する。
- [x] Hash衝突時は元文字列を比較し、Hash一致だけで同一識別子と判定しない。
- [x] Intern文字列を個別確保してaddressを安定させ、取得済みViewをTable破棄まで維持する。
- [x] Table操作を内部mutexで同期し、並行Internを許可する。
- [x] 最初の実利用としてAnimatorStateMachineのTransition元State比較、Condition Parameter検索、
  BlendTree Parameter同期を登録時解決済みName cacheへ移行する。
- [ ] Gameplayから毎Frame呼ばれる公開Set/Get APIなど、文字列境界が残る箇所はHandleを上流から
  保持できるAPI設計と実workload計測後に個別移行する。

**Phase 7-3 初期実装メモ（2026-10-27）:** `NameTable`は外部所有のRaven Allocatorを使用し、
そのAllocatorとTableは発行済みNameより長生きする必要があります。`NameHasher`はProcess内検索用で、
Table addressを含むため永続IDとして保存しません。空文字列は未登録の`Name::None`として扱います。
同一NameTableの操作は内部で同期しますが、同じ非thread-safe Allocatorを別Tableや別Containerから
並行利用する場合は、Allocator側または呼び出し側で追加の同期が必要です。
Debug / Release build、`--test-name`、`--test-string`、`--test-memory-containers`を
2026-10-27に検証済みです。

**Phase 7-3 Benchmarkメモ（2026-10-27）:** `--benchmark-name`で32 / 256 / 2,048件の
決定的なlookup-hitを、ウォームアップ3回・計測9回・各1,048,576 queryの中央値として比較します。
同一環境では事前Intern済みName Handleが2.98～3.73ns、`std::unordered_map<std::string>`が
17.77～40.18nsで、Name経路は標準文字列経路の約9～17%でした。一方、各lookupで文字列から
`Intern()`すると47.41～86.40nsとなり、標準文字列経路の約2.2～2.7倍でした。

この結果から、Animation移行は既存の文字列API内部で毎回Internする方式にせず、設定登録時に
Nameを一度生成してRuntime hot pathまでHandleを伝播できる境界から行います。また現在のNameTableは
View address安定化のためEntryと長い文字列を個別確保し、2,048件時に4,099個の生存allocationを
使用しました。大規模Asset registryへ採用する前に、安定addressを維持できるchunk / pool storageとの
比較計測を行います。

**Animation Runtime初期移行（2026-10-27）:** State / Parameter / Transition設定の`std::string`は
Asset・Debug・公開APIの正規表現として維持し、AnimatorStateMachine登録時にだけNameを解決します。
RuntimeではNameから既存owning table要素への非所有pointerを引き、毎FrameのTransition評価と
BlendTree同期で文字列Hashを繰り返しません。State / Parameter削除時はName lookupを先に除去し、
同名再登録後も古いpointerを参照しないことをAnimation Runtime Self Testへ追加しました。
Debug / Release build、`--test-animation-runtime`、`--test-name`、`--test-string`、
`--test-flat-hash-map`、`--test-character-animation`に成功しています。Name単体lookupの効果は
上記Microbenchmarkで確認済みですが、
AnimatorStateMachine::Update全体の実workload Before / After計測は次の課題です。

---

# Phase 8: Ownership Utilities

`std::unique_ptr` / `std::shared_ptr` の単純コピーは優先しません。

独自化する場合は Raven 固有の ownership requirement がある場合に限定します。

候補:

- [ ] Allocator-aware UniquePtr。
- [ ] intrusive reference counting。
- [x] Handle / WeakHandle（Phase 8-3: HandlePoolのgenerationとPool生存Tokenを照合）。
- [ ] Object Pool ownership helper。

標準 smart pointer で十分な場所は継続利用します。

**Phase 8-3:** `HandlePool<T>::weak(handle)` が `WeakHandle<T>` を返します。Poolの生存Tokenとindex/generationを確認し、erase・clear・Pool破棄後はnullptrを返します。Poolは従来どおり非thread-safeで、WeakHandle参照中の並行破棄はサポートしません。Pool生成時に生存Tokenのcontrol blockを一度確保します。Self Test追加済み（ビルド未実施）。

---

# Phase 9: Migration

Raven STL 完成後も全コードを一括変換しません。

移行優先順位:

```text
1. Physics / SoftBody temporary hot paths
2. Renderer / RHI temporary command data
3. Resource lookup / cache
4. Animation temporary data
5. Scene / ECS internals
6. General engine utility code
```

各移行では Before / After を測定します。

最低限確認する値:

- CPU time。
- allocation count。
- allocated bytes。
- peak bytes。
- reallocation count。
- container capacity。
- binary / compile impact が問題になる場合はその差分。

改善がない場合は標準 Container を維持する判断も許容します。

---

# Phase 10: Benchmark / Validation

Raven STL 専用 Benchmark を追加します。

比較対象:

- `std::vector`
- `std::unordered_set`
- `std::unordered_map`
- Raven Containers

代表 workload:

- Sequential push。
- Reserved push。
- Random lookup。
- Insert / erase churn。
- Frame reset workload。
- Small collection。
- Physics BroadPhase Pair。
- SoftBody candidate generation。
- Name Handle lookup / String-to-Name boundary cost。

Debug / Release を分けて計測します。

Microbenchmark の勝敗だけでは採用を決めず、実際の Raven workload を最終判断材料にします。

---

## 11. 目標ディレクトリ構成

```text
Raven/Core/
|
+-- Memory/
|   +-- Allocator.h
|   +-- HeapAllocator.h/.cpp
|   +-- LinearAllocator.h/.cpp
|   +-- FrameAllocator.h/.cpp
|   +-- FreeListAllocator.h/.cpp
|   +-- PoolAllocator.h/.cpp
|   +-- STLAllocatorAdapter.h
|   +-- MemoryTag.h
|   +-- MemoryTracker.h/.cpp
|   |
|   +-- Tests/
|
+-- Containers/
    +-- Vector.h
    +-- SmallVector.h
    +-- FixedVector.h
    +-- FrameVector.h
    +-- FlatHashSet.h
    +-- FlatHashMap.h
    +-- Array.h
    +-- Span.h
    +-- RingBuffer.h
    +-- BitSet.h
    +-- HandlePool.h
    +-- SparseSet.h
    |
    +-- Tests/
    +-- Benchmarks/
```

既存 `Memory/FrameVector.h` は移行時に互換性を確認した上で `Containers/` へ整理するか判断します。

---

## 12. 推奨実装順

```text
Current
  |
  +-- Allocator                 [Done]
  +-- LinearAllocator           [Done]
  +-- FrameAllocator            [Done]
  +-- STLAllocatorAdapter       [Done]
  +-- FrameVector               [Done]
  +-- FlatHashSet               [Done]
  |
  v
HeapAllocator
  |
  v
FreeListAllocator
  |
  v
PoolAllocator
  |
  v
MemoryTag / MemoryTracker
  |
  v
Raven::Vector
  |
  +--> SmallVector
  +--> FixedVector
  |
  v
FlatHashSet stabilization
  |
  v
FlatHashMap
  |
  v
Span / Array / RingBuffer / HandlePool
  |
  v
String / StringView / Name
  |
  v
Measured subsystem migration
```

---

## 13. Raven STL の完成条件

「標準 STL を使わなくなった」ことを完成条件にはしません。

以下を満たした時点を Raven STL / Core Containers の第一完成地点とします。

- [ ] Persistent / Frame / Pool の主要 memory lifetime を Raven Allocator で表現できる。
- [ ] Memory usage を subsystem 単位で観測できる。
- [ ] 汎用 contiguous container が存在する。
- [ ] Small / Fixed temporary collection を heap allocation なしで扱える。
- [ ] Flat Hash Set / Map が実用可能。
- [ ] Physics / Renderer など複数 subsystem で実利用されている。
- [ ] Self Test と Benchmark が存在する。
- [ ] Raven Container と標準 Container を相互運用できる。
- [ ] 標準 Library を利用すべき箇所と Raven Container を利用すべき箇所の基準が文書化されている。

---

## 14. 次の作業

次の実装対象は **Phase 1-1: HeapAllocator** とします。

HeapAllocator では、既存 `Allocator` 契約を変更せずに Persistent allocation を追加し、Linear / FrameAllocator と同じ統計 API で計測できる状態を最初の完了条件とします。

その後、FreeListAllocator -> PoolAllocator の順に進め、Allocator Foundation が揃った時点で MemoryTag / MemoryTracker と Raven::Vector の設計へ進みます。
