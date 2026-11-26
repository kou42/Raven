# Raven SPH Solver 最適化ロードマップ

対象: `Root/Root/Raven/Physics/Fluid/SPHSolver.cpp` / `SPHSolver::Step()`  
目的: 物理挙動と安定性を維持しながら CPU 処理時間を削減する。

## 1. 現状と課題

- Force は最大7 Worker Job + 呼び出し元の8並列。1024粒子未満では直列。
- Density、Pressure、ForceTerms 前計算、Grid Build、積分、境界処理は直列。
- Neighbor Cache は262,144件（通常約2 MiB）で、超過すると Substep 全体が Grid 再探索へフォールバックする。
- デフォルトの CFL 条件では dt 上限は約2.83 ms。16.67 ms の Step では最低6 Substep（他の制約により増加）。
- Kernel 係数の前計算、距離二乗判定、Prepared Neighbor Buckets は実装済み。

## 2. 実装順序

| Phase | 内容 | 優先度 | 完了条件 |
|---|---|---|---|
| 1 | 詳細プロファイリング | 最優先 | Step 内訳と基準値を取得 |
| 2 | Neighbor Cache 容量比較 | 高 | メモリ予算と最適容量を決定 |
| 3 | Density 並列化 | 高 | 直列版と結果が一致し高速化 |
| 4 | Particle Pass 統合 | 高 | 重複走査を削減 |
| 5 | Spatial Hash 最適化 | 高 | 最適 CellSize と探索方式を決定 |
| 6 | Verlet Neighbor List | 中 | 安全な Substep 間再利用 |
| 7 | Force Fast Path / 負荷分散 | 中 | Kernel・Job 待機時間を削減 |
| 8 | SoA / Spatial Sorting / SIMD | 中 | メモリ局所性と演算効率を改善 |
| 9 | GPU SPH | 将来 | CPU 版との比較と GPU 常駐化 |

## Phase 1 — 詳細プロファイリング

- [ ] `Physics.Fluid.SPH.` 名前空間に以下の計測を追加する。

| 項目 | 計測内容 |
|---|---|
| DensityNeighborLoop | Grid 構築を除く密度計算 |
| NeighborCacheCount / Overflow | 保存件数・容量超過回数 |
| NeighborCacheCapacityBytes | 確保済みメモリ |
| NeighborForceTerms | Force 前計算 |
| ForceJobSubmit / Wait | Job 投入・待機時間 |
| ForceJobMaxMilliseconds / MinMilliseconds | Job 間負荷差 |
| StableTimeStep / StepStatistics | 粒子走査時間 |
| PrepareNeighborBuckets | Bucket 準備時間（既存 Trace を活用） |

- [ ] 既存の `ForceNeighborCacheUsed`、Candidate/Accepted Count、SubstepCount、SubstepLimitReached を記録。
- [ ] Hot loop 内で時計取得や共有 Profiler 更新を繰り返さない。Job ローカル集計を同期後に登録する。
- [ ] Step 時間と Substep あたりの時間を分けて記録する。ネストした Trace の時間を二重加算しない。

## Phase 2 — Neighbor Cache 容量比較

- [ ] 256K / 512K / 1M / 2M 件 / キャッシュ無効を比較。
- [ ] キャッシュ上限を Settings で設定可能にする（メモリ予算を明示）。
- [ ] 容量超過時は完全な行のみ利用し、未完成行は Grid 探索へフォールバックする方式を検討。
- [ ] キャッシュ構築コストを含めた Density + Force 合計時間で採否を判断。

| 件数 | 8 byte/件の場合の容量（Offset等を除く） |
|---:|---:|
| 262,144 | 2 MiB |
| 524,288 | 4 MiB |
| 1,048,576 | 8 MiB |
| 2,097,152 | 16 MiB |

## Phase 3 — Density 並列化

対象: `SPHSolver.cpp/.h`。

- [ ] `ComputeDensityRange()` を追加し、粒子範囲ごとに独立して処理する。
- [ ] Grid Build / PrepareNeighborBuckets を先に完了し、探索が読み取り専用であることを Core 実装で確認する。
- [ ] Job ごとに永続的な Neighbor Buffer、Offsets、Counter を持たせる。
- [ ] キャッシュ総メモリ予算を Job 間で配分し、超過時も密度・力の計算を欠落させない。
- [ ] Density 完了 Barrier の後に Force を開始する。Force 完了後に積分する。
- [ ] JobSystem が null の場合、小規模粒子数、Job 投入失敗時の直列経路を維持する。
- [ ] 公開 ComputeDensity / ComputeForces の既存動作を維持する。

```cpp
struct DensityJobBuffer
{
    std::vector<CachedNeighbor> Neighbors;
    std::vector<std::size_t> Offsets;
    uint64_t CandidateCount = 0;
    uint64_t AcceptedCount = 0;
    std::size_t RangeBegin = 0;
    std::size_t RangeEnd = 0;
};
```

共有 vector への並列 push_back は避ける。Force 側は Job 別バッファを直接参照する方式を優先し、不要な全件コピーを避ける。

## Phase 4 — Particle Pass 統合

- [ ] Density 完了時に同じ粒子の Pressure / NeighborForceTerms を計算する。
- [ ] Integrate と ResolveBoundary を統合し、境界処理後の最大速度・加速度・密度・圧力を集計する。
- [ ] 次の Substep の CFL 計算に集計値を再利用する。Step 初回は外部変更を考慮して再集計する。
- [ ] 最大速度・加速度の比較を二乗値で行い、平方根を集計後に計算する（極端な値での overflow に注意）。
- [ ] 公開 API の単独呼び出しと無効 Mass 粒子の境界処理を維持する。

## Phase 5 — Spatial Hash 最適化

対象: `FluidSpatialHashGrid` / `ParticleSpatialHashGrid`。

- [ ] Core のデータ構造、割り当て、Hash lookup、Prepared Bucket の上限を調査。
- [ ] CellSizeScale = 1.0 / 0.8 / 0.6 / 0.5 / 0.4 を比較。
- [ ] SmoothingRadius は固定し、候補数と Grid Build + Prepare + Density + Force 合計時間を比較。
- [ ] 必要ならセル AABB と探索球の交差判定、連続したセル範囲、ソート型 Grid を検討。
- [ ] 共有 Core の変更時は SoftBody 等への影響も確認する。

## Phase 6 — Verlet Neighbor List

- [ ] `searchRadius = h + skin`、初期候補 `skin = 0.2h` とする。
- [ ] 構築時の粒子位置を保存し、最大移動量が skin/2 以上なら再構築する。
- [ ] 毎 Substep で距離と Kernel を再計算し、厳密な h 内判定を行う。
- [ ] 粒子追加・削除・並び替え・設定変更時に無効化する。
- [ ] RebuildCount、ReuseCount、候補増加率、メモリ量を計測し、skin を比較する。

## Phase 7 — Force Fast Path / 負荷分散

- [ ] 自粒子の PressureOverDensitySquared も前計算値から取得。
- [ ] Mass * InverseDensity の前計算、Force のローカル変数集約を比較。
- [ ] Kernel のインライン化 / LTO、範囲判定済み Fast Path、粘性有無の分岐分離を検討。
- [ ] Job ごとの近傍件数を基準に負荷分散する。
- [ ] Job 数・最小粒子数の閾値を実測で調整する。
- [ ] 対称ペア計算は書き込み競合と現行粘性式の非対称性があるため、別途検証する。

## Phase 8 — SoA / Spatial Sorting / SIMD

- [ ] Solver 内部の Position / Mass / Velocity 等を SoA 化する試作。
- [ ] Cell 順または Morton Code 順に並べ、粒子 ID と外部参照を維持する。
- [ ] AVX2 等で距離判定・Poly6・Force 計算を比較する。
- [ ] データ変換コスト、SIMD 非対応 CPU のフォールバック、浮動小数点誤差を検証する。

## Phase 9 — GPU SPH（将来）

- [ ] Raven RHI の Compute / Storage Buffer / Barrier 対応状況を調査する。
- [ ] GPU Grid Build → Density → Force → Integration を段階的に実装する。
- [ ] GPU 上に粒子を常駐させ、CPU readback を最小化する。
- [ ] CPU 版を参照実装・フォールバックとして維持する。

## 3. ベンチマーク計画

Release ビルド、同じ初期状態・設定・固定 dt・Worker 数で比較する。ウォームアップ後、例えば300 Step を計測し、中央値・p95・最大値を記録する。粒子数は実行可能な範囲で 1K / 5K / 10K / 20K / 50K、均一配置と密集状態を比較する。

| Phase / 設定 | 粒子数 | Step median ms | p95 ms | Substep数 | Density ms | Force ms | Cache MiB | 備考 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Baseline | | | | | | | | |
| 改善後 | | | | | | | | |

### 共通の完了条件

- [ ] 同じ入力に対する Density / Pressure / Force / Position / Velocity を参照実装と許容誤差内で比較。
- [ ] 空配列、単一粒子、重複位置、無効 Mass、密集、キャッシュ超過、JobSystem 無効を検証。
- [ ] NaN / Inf、近傍欠落、データ競合、境界逸脱がないことを確認。
- [ ] CFL や Substep 上限を変更して見かけ上の高速化を得ない。
- [ ] 性能改善とメモリ増加を記録し、効果がない変更は採用しない。

## 4. 次の作業

**Phase 1 → Phase 2 → Phase 3** を最初の実装単位とする。実測で別の支配的ボトルネックが判明した場合は順序を見直す。

## 5. 調査範囲と前提

本ロードマップは GitHub master の SPHSolver、SPHKernel、SPHSettings、FluidParticle、FluidSpatialHashGrid の静的調査に基づく。ParticleSpatialHashGrid の内部、実機計測、ビルド・テストは未確認。記載した容量、粒子数、skin、計測回数は比較実験の提案値であり、採用値ではない。Solver の実装変更は行っていない。

参照: [SPHSolver.cpp](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Fluid/SPHSolver.cpp)
