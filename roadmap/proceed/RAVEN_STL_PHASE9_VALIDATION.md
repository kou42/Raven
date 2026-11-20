# Raven STL Phase 9 検証記録

2026-11-21。対象は Windows x64 のローカル `master`、リビジョン `a7facb20`。関連する計画は [RAVEN STL ロードマップ](RAVEN_STL_ROADMAP.md)。

## 判断

- **SoftBody Cloth Demo** は、統合ソルバーがすでに候補ペア用の `std::vector` の容量をステップ間で再利用している。計測中に保持した容量は最大8 Bだったため、現行方式を維持する。
- **Physics BroadPhase** は、候補ペアの急増時に `FrameVector` の再確保が集中した。固定長ブロックはそのステップの Arena 使用量を約41%削減したが、安定時は余分な容量を使った。通常経路は現行方式を維持する。
- **汎用 `Raven::Vector`** は内部で `std::vector` と `STLAllocatorAdapter` を使用する。今回のデータから、汎用ストレージ全体を置き換えるだけの効果は確認できていない。

## 1. SoftBody 候補ペアのコンテナ比較

同じ空間ハッシュに3種類の出力コンテナを渡した。格子状に配置した512粒子と4,096粒子で、それぞれ10,720件と105,952件の候補ペアが生成された。4反復で同じコンテナを再利用し、候補ペアの内容と順序が全方式で一致した。以下は候補ペア生成の1ステップ相当の CPU 時間中央値。空間グリッドの再構築とソルバー全体は計測区間外。

| 構成 | 粒子数 | std::vector | Raven Vector + Heap | Raven Vector + Frame |
|---|---:|---:|---:|---:|
| Debug | 512 | 1.892 ms | 1.984 ms | 1.953 ms |
| Debug | 4,096 | 19.043 ms | 19.653 ms | 19.165 ms |
| Release | 512 | 0.214 ms | 0.214 ms | 0.214 ms |
| Release | 4,096 | 2.319 ms | 2.319 ms | 2.075 ms |

| 粒子数 | 確保回数 | 再確保回数 | 累計確保量 | 最終容量 | std / Heap のピーク | Frame のピーク（Release） |
|---|---:|---:|---:|---:|---:|---:|
| 512 | 24 | 23 | 291,376 B | 12,138要素 | 161,840 B | 291,376 B |
| 4,096 | 30 | 29 | 3,318,208 B | 138,255要素 | 1,843,400 B | 3,318,208 B |

確保回数、再確保回数、累計確保量、最終容量は3方式で同じ。メモリ指標は候補ペア用バッファが対象で、Heap Allocator の管理用マップは計上していない。Frame Allocator は個別解放を行わず、旧バッファがリセットまで残る。Release の4,096粒子では CPU 時間が約11%短くなった一方、ピークは約1.8倍になった。

## 2. 前ステップの候補数を使う reserve

4,096粒子の合成データで、毎ステップ空の `std::vector` を作る場合と、前ステップ末尾の候補数を `reserve()` する場合を比較した。いずれもステップ内で4反復し、候補ペアの内容と順序が一致した。

| 候補数の変化 | 確保回数（現行 → reserve） | バッファのピーク（現行 → reserve） |
|---|---:|---:|
| 0 → 105,952 | 30 → 30 | 1,843,400 → 1,843,400 B |
| 105,952 → 105,952 | 30 → 1 | 1,843,400 → 847,616 B |
| 21,280 → 105,952 | 30 → 5 | 1,843,400 → 1,436,400 B |
| 105,952 → 21,280 | 26 → 1 | 364,136 → 847,616 B |

Release の実行時間は、候補数が安定したケースで約21%、増減を繰り返すケースで約2〜6%短縮した。前回値が大きい状態で候補数が減ると、確保回数は減ってもピークは増える。

## 3. Cloth Demo の統合ソルバー

24×24 Cloth Demo を剛体連成あり、固定刻み 1/60 秒で240ステップ実行した。各ステップの反復数は12。粒子間自己衝突候補が出たのは8ステップで、各反復の候補数は最大1件だった。候補数の時系列は両方式で一致した。

| 粒子間の候補バッファ | 現行の容量再利用 | 毎ステップ前回件数で reserve |
|---|---:|---:|
| 確保回数 | 1 | 8 |
| 累計確保量 | 8 B | 64 B |
| ピーク | 8 B | 8 B |
| 最終ステップの保持容量 | 8 B | 0 B |
| Release のソルバー時間 | 3.852 ms | 3.866 ms |

時間は時系列の記録を切った別実行で測り、各方式3回のステップ中央値を比較した。差は実行ごとのばらつきに収まった。合成データで得た短縮率は、毎ステップ空のコンテナを開始点とする比較の結果。実際の統合ソルバーは `clear()` で候補だけを消し、容量を次ステップへ残す。今回の計測対象は粒子間候補で、粒子・三角形間の候補数や、大規模に折り重なる布での保持容量は未検証。

## 4. BroadPhase の候補急増と固定長ブロック

96個の球を置いた検証用 Scene で、実際の `BroadPhase::ComputePairs()` と結果走査を計測した。安定時は144ペア。急増ケースでは球を集め、4,560ペアに増やした。現行の `FrameVector` は前ステップ件数を `reserve()` し、試作は必要時に256要素の固定長ブロックを追加する。両方式で全ステップのペア内容・順序、診断スナップショット、フレームリセットを確認した。

| Release の測定項目 | 現行 FrameVector | 固定長ブロック |
|---|---:|---:|
| 急増時のペア出力の確保回数 | 10 | 18 |
| うち既存バッファの再確保 | 9 | 0 |
| 急増時の出力容量 | 5,530要素 | 4,608要素 |
| 急増時のペア出力の Arena 使用量 | 521,792 B | 147,744 B |
| 急増時の重複除去 Set の Arena 使用量 | 387,072 B | 387,072 B |
| 急増時の Arena 全体の使用量 | 912,960 B | 538,912 B |
| 144ペアで安定したときの Arena 全体の使用量 | 14,848 B | 18,448 B |

急増時に現行バッファが再確保直前に Arena 末尾にあったのは9回中3回。重複除去 Set と Arena を共有するため、その場での拡張は他の確保が入らない場合に限られる。Release の3回の計測では、試作は急増時に約0.5〜1.6%速く、安定時に約2〜4%遅かった。安定時と急増時の両方で、プロファイル対象は BroadPhase と結果走査。Narrow Phase、フレーム全体の時間、実ゲームで急増が起きる頻度は未検証。

## ビルド・検証と再確認用データ

- 本体の Windows x64 Debug / Release ビルドは成功。Debug で `--test-memory-containers`、`--test-physics-collision`、`--test-softbody-integrated`、`--test-explicit-frame` が成功。Cloth Demo と BroadPhase の診断用ビルドも両構成で成功。
- 本体ビルドでは Debug に型変換と既定ライブラリ競合、Release に PDB 型レコードの警告が出た。診断用 Debug リンクでも既定ライブラリ競合などの警告が出た。
- 計測用コードとログはローカルの Git 管理外に保存している。以下のリンクは計測した作業環境で参照できる。
  - [SoftBody 比較ソース](../../artifacts/phase9/probe.cpp) · [Debug](../../artifacts/phase9/measure-Debug.log) · [Release](../../artifacts/phase9/measure-Release.log)
  - [reserve 比較ソース](../../artifacts/phase9/reserve-probe.cpp) · [Release](../../artifacts/phase9/reserve-measure-Release.log) · [Release 再計測](../../artifacts/phase9/reserve-measure-Release-repeat.log)
  - [Cloth Demo 現行 CSV](../../artifacts/phase9/demo-baseline-trace.csv) · [reserve 版 CSV](../../artifacts/phase9/demo-reserve-trace.csv)
  - [BroadPhase 比較ソース](../../artifacts/phase9/broadphase-probe.cpp) · [Debug CSV](../../artifacts/phase9/broadphase-trace-Debug.csv) · [Release CSV](../../artifacts/phase9/broadphase-trace-Release.csv)

## 残課題

BroadPhase の候補急増が実ゲームで頻発し、Arena の容量やピークが問題になることを確認できた場合に、急増時に限定した対策を再検討する。汎用 Vector の独自ストレージ化や Allocator API の拡張は今回の検証範囲外。
