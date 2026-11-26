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

- [x] `Physics.Fluid.SPH.` 名前空間に以下の計測を追加する。

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

- [x] 既存の `ForceNeighborCacheUsed`、Candidate/Accepted Count、SubstepCount、SubstepLimitReached を記録。
- [x] Hot loop 内で時計取得や共有 Profiler 更新を繰り返さない。Job ローカル集計を同期後に登録する。
- [x] Step 時間と Substep あたりの時間を分けて記録する。ネストした Trace の時間を二重加算しない。

### Phase 1 の再計測方法

x64 Release でビルドし、リポジトリルートから実行します。

```powershell
& .\Root\x64\Release\Root.exe --benchmark-fluid-sph
```

- 固定 dt は 1/60 秒、Worker は4本です。3次元格子の粒子間隔は均一が 0.065、密集が 0.025 です。
- Worker Pool は全ケースで共用し、Solver はケース内で再利用します。各 Step の前に粒子状態を初期配置へ復元し、20回のウォームアップ後に300回計測します。復元と集計は Step 時間の外です。
- `Step` は Solver 呼び出し全体、`Substep` は Density から境界処理までです。CFL の `StableTimeStep` は `Substep` の外で、`StepStatistics` は Step 末尾の走査です。
- `Density` は Grid 構築・Bucket 準備・近傍ループを含みます。`Force` は前計算・Job 投入・呼び出し元の計算・同期を含みます。親区間と子区間は重複するため加算しません。
- Job の最短・最長は Worker の実行時間です。`ForceJobWait` は呼び出し元の担当範囲の計算後、future の回収と集計にかかった時間です。
- キャッシュ容量は近傍配列と行オフセット配列の予約済みバイト数です。容量は `max` 列、保存件数・負荷比較は `mean_per_call` 列、超過頻度は `sum_per_step` 列で確認します。上限でキャッシュを断念した場合、保存件数には破棄された部分リストも含まれます。

### Phase 1 の基準値

Windows x64 Release、Intel Core i9-10900K、CPUProfiler 有効で計測しました。Density と Force は Step あたりの平均です。キャッシュ超過回数は Step あたり、容量は最大確保量です。

| 粒子数 | 配置 | Step 中央値 ms | p95 ms | 最大 ms | Substep | Density ms | Force ms | キャッシュ MiB | 超過 |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,024 | 均一 | 4.91 | 5.06 | 5.44 | 7 | 3.95 | 0.89 | 0.13 | 0 |
| 1,024 | 密集 | 82.13 | 83.25 | 103.22 | 16 | 67.06 | 14.94 | 2.01 | 0 |
| 5,120 | 均一 | 22.86 | 23.02 | 23.42 | 7 | 20.88 | 1.66 | 1.04 | 0 |
| 5,120 | 密集 | 531.89 | 549.97 | 619.82 | 15 | 414.93 | 120.55 | 2.04 | 15 |
| 10,240 | 均一 | 43.92 | 44.62 | 59.64 | 7 | 40.26 | 3.12 | 2.08 | 0 |
| 10,240 | 密集 | 1,229.54 | 1,259.81 | 1,300.17 | 16 | 965.62 | 266.48 | 2.08 | 16 |

- `DensityNeighborLoop` は全6条件で最も長く、計測した Step 平均時間の約74〜85%です。10,240粒子の密集条件では 963.74 ms/Step でした。
- 5,120・10,240粒子の密集条件では毎 Substep でキャッシュが超過し、Force は Grid 探索へフォールバックしました。Phase 2 では同じ初期状態でキャッシュ上限を振り、Density と Force の合計時間と確保量を比較します。
- `SubstepLimitReached` は1,024粒子の密集条件だけで記録されました。10,240粒子の密集条件も16 Substepですが、最後の Substep で安全 dt を超えて残時間を消化する処理は発生していません。
- Debug の `--test-fluid-sph` と `--test-physics-collision` は成功しました。20K・50K粒子と計測無効時のオーバーヘッドは未計測です。

## Phase 2 — Neighbor Cache 容量比較

- [x] 256K / 512K / 1M / 2M 件 / キャッシュ無効を比較。
- [x] キャッシュ上限を Settings で設定可能にする（メモリ予算を明示）。
- [x] 容量超過時は完全な行のみ利用し、未完成行は Grid 探索へフォールバックする方式を検討。
- [x] キャッシュ構築コストを含めた Density + Force 合計時間で採否を判断。

| 件数 | 8 byte/件の場合の容量（Offset等を除く） |
|---:|---:|
| 262,144 | 2 MiB |
| 524,288 | 4 MiB |
| 1,048,576 | 8 MiB |
| 2,097,152 | 16 MiB |

### Phase 2 の比較結果

`Root.exe --benchmark-fluid-sph-cache` を Windows x64 Release で実行しました。CPU、粒子配置、設定、固定 dt、Worker 数は Phase 1 と同条件です。3配置それぞれで9設定を固定順に実行し、各設定を5 Step ウォームアップして20 Step 計測しました。

`SPHSettings::MaximumCachedNeighbors` は近傍エントリ数の上限で、0にすると近傍配列と行オフセットを確保しません。上限を下げると以前の確保を解放します。`ReuseCompletedNeighborRowsOnOverflow` を有効にすると、上限で中断した行のエントリを破棄し、Force は完成済みの行だけを読み取ります。残りの行は同じ Substep の Prepared Bucket から再探索します。

Density + Force は1 Stepあたりの平均で、キャッシュ構築時間を含みます。確保量は近傍配列と行オフセットの合計の最大値です。超過は1 Stepあたりの平均回数です。

| 粒子・配置 | 上限 | 超過時の再利用 | Density + Force ms | Step p95 ms | 確保 MiB | 超過 |
|---|---:|---|---:|---:|---:|---:|
| 1,024 均一 | 無効 | なし | 7.70 | 7.88 | 0.00 | 0 |
| 1,024 均一 | 256K | なし | 6.42 | 6.56 | 0.13 | 0 |
| 1,024 均一 | 256K | 完成行 | 4.89 | 5.25 | 0.13 | 0 |
| 1,024 均一 | 512K | なし | 4.78 | 4.98 | 0.13 | 0 |
| 1,024 均一 | 512K | 完成行 | 4.79 | 5.05 | 0.13 | 0 |
| 1,024 均一 | 1M | なし | 4.83 | 5.03 | 0.13 | 0 |
| 1,024 均一 | 1M | 完成行 | 4.82 | 5.05 | 0.13 | 0 |
| 1,024 均一 | 2M | なし | 4.84 | 5.05 | 0.13 | 0 |
| 1,024 均一 | 2M | 完成行 | 4.82 | 4.96 | 0.13 | 0 |
| 5,120 密集 | 無効 | なし | 522.62 | 541.57 | 0.00 | 0 |
| 5,120 密集 | 256K | なし | 537.43 | 542.39 | 2.04 | 15 |
| 5,120 密集 | 256K | 完成行 | 534.38 | 540.77 | 2.04 | 15 |
| 5,120 密集 | 512K | なし | 548.72 | 550.41 | 4.04 | 15 |
| 5,120 密集 | 512K | 完成行 | 539.12 | 540.98 | 4.04 | 15 |
| 5,120 密集 | 1M | なし | 574.89 | 576.45 | 8.04 | 14 |
| 5,120 密集 | 1M | 完成行 | 508.78 | 511.89 | 8.04 | 14 |
| 5,120 密集 | 2M | なし | 499.89 | 503.19 | 16.04 | 0 |
| 5,120 密集 | 2M | 完成行 | 500.57 | 504.12 | 16.04 | 0 |
| 10,240 密集 | 無効 | なし | 1,233.03 | 1,279.04 | 0.00 | 0 |
| 10,240 密集 | 256K | なし | 1,256.99 | 1,293.21 | 2.08 | 16 |
| 10,240 密集 | 256K | 完成行 | 1,226.65 | 1,234.02 | 2.08 | 16 |
| 10,240 密集 | 512K | なし | 1,279.02 | 1,321.05 | 4.08 | 16 |
| 10,240 密集 | 512K | 完成行 | 1,272.10 | 1,299.50 | 4.08 | 16 |
| 10,240 密集 | 1M | なし | 1,281.61 | 1,296.07 | 8.08 | 16 |
| 10,240 密集 | 1M | 完成行 | 1,272.09 | 1,279.30 | 8.08 | 16 |
| 10,240 密集 | 2M | なし | 1,348.48 | 1,354.15 | 16.08 | 16 |
| 10,240 密集 | 2M | 完成行 | 1,211.39 | 1,214.25 | 16.08 | 16 |

既定値は **256K件・超過時は全体フォールバック**を維持します。5,120粒子の密集では2M件で超過が解消し、Density + Force が537.43 msから499.89 msへ約7.0%短縮しました。一方、10,240粒子の密集では2M件でも全 Substep で超過し、完成行を再利用しても16.08 MiBを確保して短縮は約3.6%でした。256K件の完成行再利用は確保量が同じで両密集ケースの平均時間を短縮しましたが、改善幅は約0.6〜2.4%でした。固定順の単発計測で、超過のない均一配置にも同じ処理経路間で時間差が出たため、完成行再利用は設定で選択できる状態とします。

Debug／Release ビルドと Debug の `--test-fluid-sph` に成功しました。設定変更、無効、上限ちょうど、行途中での超過、並列での部分再利用を参照経路と比較しました。

## Phase 3 — Density 並列化

対象: `SPHSolver.cpp/.h`。

- [x] `ComputeDensityRange()` を追加し、粒子範囲ごとに独立して処理する。
- [x] Grid Build / PrepareNeighborBuckets を先に完了し、探索が読み取り専用であることを Core 実装で確認する。
- [x] Job ごとに永続的な Neighbor Buffer、Offsets、Counter を持たせる。
- [x] キャッシュ総メモリ予算を Job 間で配分し、超過時も密度・力の計算を欠落させない。
- [x] Density 完了 Barrier の後に Force を開始する。Force 完了後に積分する。
- [x] JobSystem が null の場合、小規模粒子数、Job 投入失敗時の直列経路を維持する。
- [x] 公開 ComputeDensity / ComputeForces の既存動作を維持する。

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


### Phase 3 の実測と動作

Grid Build と Bucket 準備を終えてから粒子の連続範囲を配分します。Grid の候補探索は読み取り専用で、各範囲は担当粒子の Density と専用バッファを書き込みます。範囲ごとの集計とバッファの公開は全 Job の完了後に行い、Pressure、Force、積分の順に進みます。Force はバッファを直接参照します。

呼び出し元も1範囲を担当し、JobSystem が未指定か1,024粒子未満なら直列です。投入に失敗した場合、未投入の範囲を呼び出し元が処理します。処理中に例外が発生した場合は、投入済みの全 Job を待ってから例外を伝播します。公開 `ComputeDensity` と `ComputeForces` の単独呼び出しは直列です。

キャッシュ上限は各範囲へ等分し、余りは先頭から1件ずつ配分します。全体フォールバックと完成行再利用は Phase 2 の設定を引き継ぎます。範囲ごとの上限があるため、全体に空きがあっても密度の高い範囲は超過することがあります。`NeighborCacheOverflow` は超過のあった Substep を、`NeighborCacheOverflowedRangeCount` はその中で超過した範囲数を記録します。

`Root.exe --benchmark-fluid-sph-density` を Windows x64 Release、Intel Core i9-10900K で実行しました。各条件で粒子の初期状態を毎 Step 復元し、固定 dt は1/60秒、5回ウォームアップ後に20 Step計測しました。キャッシュ上限は256K件です。並列測定では Density と Force の両方に同じ Pool を使用します。

| 粒子・配置 | Worker 上限 | Density Job数 / Substep | Substep数 | Density 平均 ms | Force 平均 ms | Step 中央値 ms | p95 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1,024 均一 | 0 | 0 | 7 | 4.16 | 1.12 | 5.36 | 5.54 |
| 1,024 均一 | 4 | 1 | 7 | 3.59 | 0.89 | 4.58 | 4.62 |
| 5,120 密集 | 0 | 0 | 15 | 407.87 | 481.65 | 889.74 | 895.28 |
| 5,120 密集 | 4 | 4 | 15 | 104.80 | 117.10 | 222.41 | 223.87 |
| 10,240 密集 | 0 | 0 | 16 | 951.74 | 1,127.47 | 2,079.39 | 2,095.45 |
| 10,240 密集 | 4 | 4 | 16 | 231.31 | 262.03 | 494.81 | 498.70 |

同じ4 Workerの Phase 2 記録と比較すると、Step中央値は5,120粒子の密集で535.26 msから222.41 ms、10,240粒子の密集で1,260.88 msから494.81 msでした。Phase 2 の値は別セッションで、Force は当時も並列でした。密集の2条件は今回も全 Substep でキャッシュが超過しました。

Debug／Release ビルドと Debug の `--test-fluid-sph` に成功しました。通常、密集、キャッシュ超過時の全体／部分フォールバック、キャッシュ無効、粒子数の増減で、直列と並列の結果を照合しました。Job投入失敗は静的に確認し、故障注入とデータ競合検出ツールによる検証は未実施です。

## Phase 4 — Particle Pass 統合

- [x] Density 完了時に同じ粒子の Pressure / NeighborForceTerms を計算する。
- [x] Integrate と ResolveBoundary を統合し、境界処理後の最大速度・加速度・密度・圧力を集計する。
- [x] 次の Substep の CFL 計算に集計値を再利用する。Step 初回は外部変更を考慮して再集計する。
- [x] 最大速度・加速度の比較を二乗値で行い、平方根を集計後に計算する（極端な値での overflow に注意）。
- [x] 公開 API の単独呼び出しと無効 Mass 粒子の境界処理を維持する。


### Phase 4 の実測と動作

Density が確定した粒子から Pressure と Force 前計算項を作り、すべての Density Job が終了してから Force が読み取ります。公開 `ComputeDensity` と `ComputePressure` はそれぞれの値だけを更新します。公開 `ComputeForces` は呼び出し側が渡した Density と Pressure を使って前計算項を作ります。

Force 完了後は粒子ごとに積分、Box 境界処理、統計集計を行います。無効 Mass の粒子は積分を省き、境界処理と速度集計は行います。初回 CFL は呼び出し元が変更した粒子状態を再走査し、2回目以降は境界処理後に集計済みの最大速度・加速度を使います。最後の Substep の集計結果を Step の診断にも使います。

通常値は従来の float の二乗和を使い、float の二乗があふれる場合などは double で再計算します。加速度は Force と Mass の二乗の交差積で比較し、選んだ値の平方根のみを取得します。

Phase 3 と同じ `Root.exe --benchmark-fluid-sph-density` を Windows x64 Release で実行しました。5 Step のウォームアップ後に20 Stepを計測した別セッション同士の比較です。

| 粒子・配置 | Worker 上限 | Substep数 | Phase 3 中央値 ms | Phase 4 中央値 ms | Phase 3 p95 ms | Phase 4 p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| 1,024 均一 | 0 | 7 | 5.36 | 5.12 | 5.54 | 5.22 |
| 1,024 均一 | 4 | 7 | 4.58 | 3.77 | 4.62 | 4.00 |
| 5,120 密集 | 0 | 15 | 889.74 | 890.48 | 895.28 | 906.49 |
| 5,120 密集 | 4 | 15 | 222.41 | 223.76 | 223.87 | 224.83 |
| 10,240 密集 | 0 | 16 | 2,079.39 | 2,073.96 | 2,095.45 | 2,085.76 |
| 10,240 密集 | 4 | 16 | 494.81 | 492.17 | 498.70 | 505.36 |

統合後のトレースでは、初回の再走査 `InitialParticleStatistics` は Step あたり1回、`IntegrateBoundaryStatistics` は Substep あたり1回です。独立していた Pressure／Force 前計算の時間は Density に含まれます。中央値は均一条件で短縮し、密集条件ではほぼ横ばいでした。10,240粒子の4 Workerでは p95 が増加しました。この計測だけで統合の効果とセッション間の変動を分離することはできません。

Debug／Release ビルドと Debug の `--test-fluid-sph`、`--test-physics-collision` に成功しました。公開 API を順に呼ぶ参照経路と粒子状態、Substep 回数、最小 dt、上限フラグが一致しました。境界補正される無効 Mass 粒子、Step 間の外部速度変更、float の二乗範囲を超える速度と Force も検証しました。

## Phase 5 — Spatial Hash 最適化

対象: `FluidSpatialHashGrid` / `ParticleSpatialHashGrid`。

- [x] Core のデータ構造、割り当て、Hash lookup、Prepared Bucket の上限を調査。
- [x] CellSizeScale = 1.0 / 0.8 / 0.6 / 0.5 / 0.4 を比較。
- [x] SmoothingRadius は固定し、候補数と Grid Build + Prepare + Density + Force 合計時間を比較。
- [x] 必要ならセル AABB と探索球の交差判定、連続したセル範囲、ソート型 Grid を検討。
- [x] 共有 Core の変更時は SoftBody 等への影響も確認する。

### Phase 5 の比較結果

共有 Core は線形プローブの Hash Table を使い、粒子数の2倍以上のスロットを2のべき乗で確保します。セル内の粒子配列は必要に応じて倍増し、Build 間で再利用します。Prepared Bucket は占有セルごとの近傍スロット一覧を一時表に保存します。検索範囲が2セル超、Table が256Kスロット超、または占有セル数×検索セル数が1M超の場合は、各粒子の検索時に Hash を引きます。共有 Core は SoftBody からも使われます。

Windows x64 Release の `Root.exe --benchmark-fluid-sph-grid` で計測しました。SmoothingRadius は0.1、固定 dt は1/60秒、Worker 上限は4、近傍キャッシュ上限は256K件です。各 Step 前に同じ初期配置を復元し、各条件で5 Stepウォームアップ後に20 Step計測しました。表の時間は最終ビルドの値です。候補数と区間時間は Step あたりの平均で、Density＋Force は Build と Prepare を内包します。Build と Prepare は内訳として別列に示します。

| 粒子・配置 | Scale | Density候補 | Force候補 | Build ms | Prepare ms | Density＋Force ms | Step中央値 ms | p95 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,024 均一 | 1.0 | 548,088 | 109,488 | 0.08 | 0.21 | 3.82 | 3.92 | 3.98 |
| 1,024 均一 | 0.8 | 1,005,984 | 109,488 | 0.10 | 2.09 | 7.26 | 7.35 | 7.41 |
| 1,024 均一 | 0.6 | 606,406 | 109,488 | 0.12 | 6.97 | 10.90 | 10.99 | 11.13 |
| 1,024 均一 | 0.5 | 348,826 | 109,488 | 0.13 | 9.41 | 12.80 | 12.88 | 13.10 |
| 1,024 均一 | 0.4 | 500,154 | 109,488 | 0.13 | 0.00 | 16.58 | 16.68 | 16.74 |
| 5,120 密集 | 1.0 | 83,879,980 | 83,879,980 | 0.63 | 0.19 | 222.45 | 223.30 | 225.07 |
| 5,120 密集 | 0.8 | 145,415,494 | 145,415,494 | 0.68 | 1.14 | 357.60 | 358.59 | 359.74 |
| 5,120 密集 | 0.6 | 86,070,730 | 86,070,730 | 0.75 | 2.10 | 259.62 | 260.62 | 260.98 |
| 5,120 密集 | 0.5 | 50,398,912 | 50,398,912 | 0.89 | 4.26 | 205.95 | 206.77 | 208.20 |
| 5,120 密集 | 0.4 | 68,689,416 | 68,689,416 | 1.07 | 0.00 | 308.15 | 308.84 | 311.97 |
| 10,240 均一 | 1.0 | 5,837,962 | 1,194,986 | 0.95 | 2.08 | 24.72 | 25.65 | 25.90 |
| 10,240 均一 | 0.8 | 13,136,236 | 1,194,986 | 1.21 | 23.63 | 57.14 | 58.05 | 58.96 |
| 10,240 均一 | 0.6 | 6,684,620 | 1,194,986 | 1.63 | 0.00 | 36.97 | 37.89 | 38.11 |
| 10,240 均一 | 0.5 | 3,584,366 | 1,194,986 | 1.60 | 0.00 | 36.52 | 37.43 | 37.84 |
| 10,240 均一 | 0.4 | 5,804,804 | 1,194,986 | 1.63 | 0.00 | 64.01 | 64.87 | 65.69 |

3回の測定で、均一配置2条件では1.0が最速、5,120粒子の密集配置では0.5が最速でした。密集配置の0.5ではDensity候補を約40%減らし、Step中央値を各回で約7〜8%短縮しました。一方、10,240粒子の均一配置で0.5にすると候補は減るものの、Prepared Bucket の探索回数上限を超え、最終計測のStep中央値は25.65 msから37.43 msに増えました。0.4は半径が3セルにまたがるため、どの配置でも準備済み一覧を作らず、粒子ごとの Hash 探索が増えました。

**既定は Scale 1.0 と、予算内で Prepared Bucket を使う現行方式です。** 0.5は今回の密集配置で有効な個別設定と判断しました。セル間AABB判定、連続範囲、ソート型 Grid は候補として検討しました。今回の比較で既定を変える根拠が得られた範囲はなく、共有 Core の変更と代替方式の実装比較は行っていません。

Debug／Release ビルド、Debug の `--test-fluid-sph` と `--test-softbody-integrated` に成功しました。SPH は負座標、セル境界、再Build後のVisit順を確認し、各セル幅でDensity、Pressure、Force、位置、速度を公開APIの参照経路と比較しました。

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
