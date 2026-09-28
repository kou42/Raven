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

選択肢:

- STL-backed FrameVector を継続。
- Raven::Vector + FrameAllocator に統合。
- alias として compatibility layer を維持。

既存 Physics / SoftBody を壊さず段階移行します。

---

# Phase 5: Hash Containers

## 5-1. FlatHashSet 強化

既存実装を Raven STL の正式 Container として強化します。

- [ ] Iterator。
- [ ] const Iterator。
- [ ] erase Iterator。
- [ ] Tombstone cleanup policy。
- [ ] Probe length statistics。
- [ ] reserve / rehash Benchmark。
- [ ] adversarial hash test。
- [ ] over-aligned Key test。
- [ ] exception safety 確認。

## 5-2. FlatHashMap

`FlatHashSet` の知見を利用して Map を追加します。

- [ ] Key / Value storage。
- [ ] find。
- [ ] contains。
- [ ] insert / emplace。
- [ ] erase。
- [ ] reserve。
- [ ] Iterator。
- [ ] heterogeneous lookup は必要性を確認後に追加。

用途候補:

- Resource lookup。
- Entity / Component lookup。
- Renderer cache。
- Physics lookup table。

---

# Phase 6: Utility Containers / Views

優先度は利用箇所が発生した順に決定します。

候補:

- [ ] `Raven::Array<T, N>`。
- [ ] `Raven::Span<T>`。
- [ ] `Raven::RingBuffer<T>`。
- [ ] `Raven::BitSet` / DynamicBitSet。
- [ ] `Raven::HandlePool<T>`。
- [ ] `Raven::SparseSet`。

特に ECS / Physics / Rendering の要求が明確になった場合は `HandlePool` / `SparseSet` を優先します。

---

# Phase 7: String / Name System

String は Container 群が安定してから着手します。

## 7-1. Raven::String

目的を明確にしてから実装します。

検討事項:

- UTF-8 を標準内部表現にするか。
- SSO。
- Allocator 対応。
- StringView。
- formatting。
- path / asset name との責務分離。

## 7-2. Raven::StringView

所有しない文字列 View。

標準 Library との相互運用性を重視します。

## 7-3. Raven::Name

大量比較される識別子向けの Interned / Hashed Name System を必要性に応じて追加します。

候補用途:

- Asset identifier。
- Animation state / bone name。
- Material parameter。
- Component / property identifier。

String と Name を同一概念にしないことを基本とします。

---

# Phase 8: Ownership Utilities

`std::unique_ptr` / `std::shared_ptr` の単純コピーは優先しません。

独自化する場合は Raven 固有の ownership requirement がある場合に限定します。

候補:

- [ ] Allocator-aware UniquePtr。
- [ ] intrusive reference counting。
- [ ] Handle / WeakHandle。
- [ ] Object Pool ownership helper。

標準 smart pointer で十分な場所は継続利用します。

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
