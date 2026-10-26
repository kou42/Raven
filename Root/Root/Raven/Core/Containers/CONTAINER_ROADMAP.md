# Raven コンテナ整備ロードマップ（Array / FixedVector / SmallVector）

更新日: 2026-10-27

## 目的と責務

| 型 | 要素数 | 容量 | ストレージ | 用途 |
| --- | --- | --- | --- | --- |
| `Array<T, N>` | 常に N | N | オブジェクト内 | 色、固定成分、固定スロット |
| `FixedVector<T, N>` | 0〜N | N | オブジェクト内 | 接触点など上限が明確な可変要素 |
| `SmallVector<T, N>` | 可変 | N から拡張 | 最初はインライン、超過時は Raven Allocator | 一時候補、通常は少数の可変要素 |

原則: Array は N 個の要素を常時構築済みとする。未構築領域を管理する役割は FixedVector / SmallVector に任せる。実行時ヒープ確保を避けたい箇所には FixedVector を使う。SmallVector の拡張は Allocator の寿命と確保・解放契約に従う。

## 現状（2026-10-27 検証時点）

- `Core/Containers/Array.h/.inl`: 独自のインライン固定長ストレージを保持。`N == 0` は専用特殊化で扱い、initializer_list は要素数が N と一致しなければ `std::length_error`。
- `Core/Containers/FixedVector.h`: `std::aligned_storage_t` に最大 N 個を格納し、動的確保なし。N > 0。コピー・代入は削除、ムーブ構築あり。容量超過は `try_emplace_back` が nullptr、`emplace_back` が length_error。
- `Core/Containers/SmallVector.h`: N > 0、Raven `Allocator&` 必須。インラインからヒープへ拡張。コピー・代入は削除、ムーブ構築あり。安全な拡張のため T に noexcept move または copy を要求。
- `Core/Memory/Tests/MemoryAllocatorSelfTests.cpp` に各コンテナのテスト導線が存在する。

## 実装順序

### Phase 0 — 仕様・互換性の固定

- [x] 既存呼び出し箇所と公開 API の利用状況を調査する（`Array`, `FixedVector`, `SmallVector`, `Span`）。
- [x] C++言語バージョン、例外方針、assert 方針、既存テストの実行方法を確認する。
- [x] `N == 0` は Array で許可、FixedVector / SmallVector は現行どおり N > 0 とする方針を確定する。
- [x] API の追加は既存呼び出し元を壊さず段階的に行う。

完了条件: 互換性・境界条件のテスト項目が確定している。

### Phase 1 — Array の独立ストレージ化（最優先）

- [x] `std::array` メンバーを独自のインライン固定長ストレージへ変更する。標準C++で不正な `T[0]` を避けるため、ゼロ要素専用のストレージ特殊化などを採用する。
- [x] `size/empty/data/begin/end/operator[]/at/fill` と既存 initializer_list の「ちょうど N 個」仕様を維持する。
- [x] `end()` のポインタ型を保証し、空配列で null ポインタ演算をしない。
- [x] デフォルト初期化、コピー・ムーブ、非 trivial な T、アラインメント、constexpr の意味を維持・検証する。
- [x] `Array<T, 0>` の `size/empty/begin/end` と、空配列への `front/back` を禁止する契約をテストする。

完了条件: Array が `std::array` をストレージとして使わず、既存 API テストが通る。

### Phase 2 — 共通 API の整合

- [x] 各型の `size/empty/data/begin/end/cbegin/cend/front/back` を比較し、欠けた安全な API を追加する。
- [x] `Array` に `max_size` と `fill` の検証を追加する。`rbegin/rend` は現利用箇所に要求がないため追加しない。
- [x] `FixedVector` の `full/try_emplace_back`、`SmallVector` の `reserve/UsingInlineStorage/GetAllocator` は各型固有 API として維持する。
- [x] `Span<T>` との相互運用を検討する（所有権は移さず、寿命に注意）。

完了条件: 共通操作の命名・const 性・戻り値が一貫している。

### Phase 3 — 要素寿命とエラー契約の堅牢化

- [x] FixedVector の placement new / 破棄、move、容量超過時の状態不変性をテストする。
- [x] SmallVector のインライン→ヒープ遷移、再配置失敗時の例外安全性、Allocator 寿命、アラインメント、破棄をテストする。
- [x] コピー・代入を追加するかはユースケースと例外保証を評価してから決定する（現行の削除仕様を維持する）。
- [x] `std::aligned_storage_t` の将来の置換は C++17 から標準を更新する際の別変更とする。

完了条件: 非 trivial / move-only / throwing-copy 型での保証が文書化され、テストされている。

### Phase 4 — エンジン統合・性能検証

- [x] MemoryAllocatorSelfTests に N=0、境界、constexpr、寿命、拡張、Allocator 統計のテストを追加する。
- [x] Debug / Release のビルドと既存テストを実行する。
- [x] 代表的な用途で `sizeof`、アラインメント、アロケーション回数、追加・走査コストを計測する。
- [x] 既存の `std::array` 利用箇所は必要性を確認して個別に移行する（全置換しない）。

完了条件: 回帰テストが通り、不要なヒープ確保・性能退行がない。

## 実装・検証状況

実装元: `feature/container-array-inline-storage` / PR #335（`master` へマージ済み）。

- Phase 1: `Array` の `std::array` 依存を独立インラインストレージへ変更。ゼロ容量特殊化、境界・constexpr・寿命・アラインメントのテストを追加。
- Phase 2: `Array` に `max_size/front/back/cbegin/cend`、`SmallVector` に `cbegin/cend` を追加。`FixedVector` の既存 API と整合。
- Phase 3: `FixedVector` の構築・ムーブ失敗、`SmallVector` の再配置時のコピー失敗、追加要素の構築失敗をテスト。拡張時の追加要素をコミット前に構築するよう修正。
- Phase 4: move-only 型の拡張・確保失敗・ムーブ所有権、128 バイトアラインメント、Allocator の確保数を検証する回帰テストを追加。
- 2026-10-27: Visual Studio 2022 の x64 MSBuild で Debug / Release ビルド成功。
- 2026-10-27: Debug の `--test-memory-containers` 成功（suites=1、failures=0）。
- 2026-10-27: `FixedVector` の noexcept move 型で不要な再送出経路が生成される Warning を解消。throwing-move 型の cleanup 経路は維持。
- 2026-10-27: Release の `--benchmark-containers` を追加。`Array<uint64_t, 16>` は `std::array` と同じ sizeof=128 / alignof=8 で、構築書き込み・走査時間も同程度であることを確認。
- 2026-10-27: `SmallVector<uint64_t, 16>` は16要素まで確保0回、64要素では2回確保、破棄後の生存確保0件であることを確認。
- 2026-10-27: `SkinWeight` の4固定成分を `Array` へ個別移行。描画API境界の `std::array` は外部型との受け渡しを優先して維持。`ContactManifold` は `PointCount` と一体のデータフロー変更になるため、将来の `FixedVector` 移行候補として分離。
- 契約: `Array<T, 0>` の `front/back` はコンパイル時に禁止。`FixedVector` と `SmallVector` は `N > 0`。Allocator は `SmallVector` より長寿命で、個別解放をサポートする必要がある。

### 性能計測の実行方法

最適化による実運用相当の差を確認するため、Release ビルドで次を実行します。

```text
Root.exe --benchmark-containers
```

計測値はCPU負荷や電源状態で変動するため、絶対値ではなく同一実行内の `std::array` / `Array` 比較と、Allocator診断値を判断基準にします。

## 変更対象（予定）

- `Root/Root/Raven/Core/Containers/Array.h`
- `Root/Root/Raven/Core/Containers/Array.inl`
- `Root/Root/Raven/Core/Containers/FixedVector.h` / `.inl`（必要時）
- `Root/Root/Raven/Core/Containers/SmallVector.h` / `.inl`（必要時）
- `Root/Root/Raven/Core/Containers/Span.h`（必要時）
- `Root/Root/Raven/Core/Memory/Tests/MemoryAllocatorSelfTests.cpp`

## 実装・レビュー方針

1. Phase ごとに小さな差分で変更し、テストを追加する。
2. 既存の API / 容量超過時の動作 / Allocator 契約を破壊する変更は別途判断する。
3. Array の独自実装は STL 排除そのものではなく、エンジン内の型・契約・デバッグの統一を目的とする。性能向上は測定して判断する。
4. 本ドキュメントのチェックボックスは実装と検証が完了した時点で更新する。
