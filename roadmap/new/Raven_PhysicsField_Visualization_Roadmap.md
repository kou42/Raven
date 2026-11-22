# Raven PhysicsField 可視化ロードマップ

## 目的

`Root/Root/Raven/Physics/Field` を中心に、Raven の PhysicsField 派生型を共通の Debug Visualization 基盤で可視化する。既存の Physics Debug Renderer と Immediate UI を再利用し、物理計算側に描画依存を導入しない。

対象リポジトリ: https://github.com/kou42/Raven （調査対象: `master`）

## 現状の確認事項

- `PhysicsField` は共通基底。`VectorField::Evaluate(worldPosition)` は `math::Vec3`、`ScalarField::Evaluate(worldPosition)` は `float` を返す。
- `GravityField`、`ElectricField`、`MagneticField` は `VectorField` 派生。
- `TemperatureField`、`DenseSDFSource` は `ScalarField` 派生。
- `SparseSDFSnapshot` は `ScalarField` 非派生で、`TrySample()` を持つ。
- `PhysicsWorld::GetGravityField()` から現在の `UniformGravityField` を取得できる。
- `PhysicsDebugRenderer` は `AddLine`、`AddAABB`、`AddPointMarker`、`SubmitLines` を持ち、Explicit Scene Mode と従来描画経路をサポートする。
- `PhysicsDebugSettings` と `PhysicsDebugImmediatePanel` は表示設定と UI の拡張点となる。
- 既存 Debug Pipeline は Depth Test 無効。視認性と遮蔽表現は別途検討する。

## 対象 Field と表示方式

| 分類 | 具体型 | 基本表示 | 補助表示・注意点 |
|---|---|---|---|
| Vector | `UniformGravityField` | 格子矢印 | 加速度 `[m/s²]` |
| Vector | `PointGravityField` | 中心へ向かう矢印 | 中心マーカー、近傍の強度クランプ |
| Vector | `UniformElectricField` | 格子矢印 | 電場 `[N/C]` |
| Vector | `PointChargeElectricField` | 放射状矢印 | 電荷の符号、ソース位置 |
| Vector | `UniformMagneticField` | 格子矢印 | 磁束密度 `[T]` |
| Vector | `DipoleMagneticField` | 双極子周辺の矢印 | 中心・双極子方向、近傍の強度クランプ |
| Scalar | `UniformTemperatureField` | 色付きサンプル / スライス | 温度 `[K]` |
| Scalar | `GradientTemperatureField` | 温度グラデーション | 参照位置・温度勾配 |
| Scalar | `SphericalTemperatureRegionField` | 温度分布 | 球領域、`EvaluateInfluence()` |
| Scalar | `BoxTemperatureRegionField` | 温度分布 | Box 領域、`EvaluateInfluence()` |
| Scalar | `DenseSDFSource` | 距離の色分布 / スライス | 0 等値線・等値面は後段 |
| Adapter | `SparseSDFSnapshot` | 有効セルの距離分布 | `TrySample()` 失敗は欠損として扱う |

> 基底型の共通描画は将来の派生型にも適用する。中心、領域、物理単位などのメタデータ表示は必要に応じて型別アダプターを設ける。

## 設計原則

1. **物理と描画の分離**: `PhysicsField` / 各 `Evaluate()` に描画依存を追加しない。
2. **サンプリングと描画の分離**: world-space のサンプル生成・値取得・線分化を別責務にする。
3. **既存経路の再利用**: `PhysicsDebugRenderer::SubmitLines()` と既存カメラコンテキストを利用する。
4. **Field インスタンスを明示**: `PhysicsWorld` にある重力場は既存アクセサーを使い、他 System 所有の Field は所有者・寿命を確認して参照登録する。
5. **数値安全性**: NaN / Inf、ゼロベクトル、特異点、欠損サンプルを安全に扱う。
6. **単位別設定**: Gravity、Electric、Magnetic、Temperature、SDF の値域・色範囲・矢印倍率を混同しない。
7. **負荷制御**: サンプル数上限、間引き、表示時のみ評価、必要ならキャッシュを用意する。

## フェーズ別実装計画

### Phase 0 — 既存コードと所有関係の追加調査

- [ ] `PhysicsDebugRenderer` の呼び出しタイミング、カメラコンテキスト、Debug UI 接続を確認。
- [ ] Electric / Magnetic / Temperature Field の保持元、Registry、Scene / World からの取得経路を確認。
- [ ] Dense / Sparse SDF の評価可能範囲、GPU リソースと CPU Snapshot の同期条件を確認。
- [ ] ビルド設定、既存テスト、Sandbox / Scene のデモ導線を確認。

**完了条件**: Field ごとの実インスタンス取得方法と、変更対象ファイルが確定する。

### Phase 1 — 共通サンプリングと Vector Field 描画基盤

- [ ] サンプリング領域（中心・半径または AABB）、格子解像度、間隔を定義。
- [ ] `VectorField` を任意位置でサンプリングする共通処理を実装。
- [ ] 線分で構成した矢印（軸＋矢尻）を生成。
- [ ] 方向正規化、強度別色、長さクランプ、ゼロ・非有限値の除外を実装。
- [ ] サンプリング領域の Wireframe 表示を実装。
- [ ] `PhysicsDebugRenderer` の既存バッチへ統合。
- [ ] `PhysicsDebugSettings` に表示切替・格子数・間隔・倍率を追加。
- [ ] Immediate UI と Overlay に設定を追加。

**完了条件**: `PhysicsWorld::GetGravityField()` の `UniformGravityField` を Scene 上で切替表示でき、既存 Debug 表示を壊さない。

### Phase 2 — Gravity / Electric / Magnetic の共通対応

- [ ] `PointGravityField` の中心表示と逆二乗則の強度表示を検証。
- [ ] `UniformElectricField` / `PointChargeElectricField` を登録・表示。
- [ ] `UniformMagneticField` / `DipoleMagneticField` を登録・表示。
- [ ] Field ごとの単位・値域・色スケールを分離。
- [ ] 複数 Field の選択表示と重ね合わせ表示を用意。
- [ ] 将来の `VectorField` 派生型が共通描画を再利用できることを確認。

**完了条件**: Gravity / Electric / Magnetic の既存具体型を、専用 Renderer の重複実装なしで表示できる。

### Phase 3 — Scalar Field と Temperature 可視化

- [ ] `ScalarField` 共通サンプラーを実装。
- [ ] 2D スライス（XY / XZ / YZ）、色付きサンプル点、カラーレンジを実装。
- [ ] `UniformTemperatureField` / `GradientTemperatureField` を表示。
- [ ] `SphericalTemperatureRegionField` / `BoxTemperatureRegionField` を表示。
- [ ] `TemperatureField::EvaluateInfluence()` を温度値と区別して表示。
- [ ] 温度単位 Kelvin を明示し、必要なら Celsius 表示を UI 側で変換。

**完了条件**: 温度値・温度勾配・局所 Field の影響範囲を視覚的に判別できる。

### Phase 4 — Dense / Sparse SDF 可視化

- [ ] `DenseSDFSource` の距離スライスを表示。
- [ ] 符号付き / 符号なし距離を区別した色分布を実装。
- [ ] 0 等値線を追加し、必要に応じて 3D 等値面を後続実装。
- [ ] `SparseSDFSnapshot` 用の読み取りアダプターを追加。
- [ ] Sparse の欠損セル・帯域外・未公開 Snapshot を明確に区別。
- [ ] CPU Readback / GPU 生成との同期と描画コストを検証。

**完了条件**: Dense / Sparse SDF の形状・距離・データ欠損を Debug 表示できる。

### Phase 5 — UI、最適化、テスト

- [ ] Field 一覧、表示選択、色範囲、矢印倍率、スライス位置の UI を整備。
- [ ] サンプル上限と描画頂点数の診断表示を追加。
- [ ] 静的 Field のキャッシュ・カメラ距離による間引きを必要に応じて追加。
- [ ] Field の登録解除・所有者破棄時の参照安全性を確認。
- [ ] Explicit Scene Mode / 従来描画経路の両方で動作確認。
- [ ] 数値境界、NaN / Inf、欠損サンプル、色範囲、単位変換のテストを追加。
- [ ] サンプル Scene と操作説明を整備。

**完了条件**: 複数 Field を安全かつ再現性を持って可視化でき、Debug UI から制御できる。

## 推奨ファイル構成（案）

```text
Root/Root/Raven/Physics/
├── Field/
│   ├── PhysicsField.h                 # 既存・原則変更なし
│   ├── GravityField.h                 # 既存・原則変更なし
│   ├── DenseSDF.h / .cpp              # 既存
│   └── SparseSDF.h / .cpp             # 既存
└── Debug/
    ├── PhysicsDebugRenderer.h / .cpp  # 既存・統合箇所
    ├── PhysicsDebugSettings.h         # 既存・設定追加
    ├── PhysicsDebugImmediatePanel.h   # 既存・UI追加
    ├── PhysicsFieldDebugSampler.h/.cpp # 新規候補
    ├── PhysicsFieldDebugDraw.h/.cpp    # 新規候補
    └── PhysicsFieldDebugAdapters.h/.cpp # 必要時に追加
```

ファイル名・分割は Phase 0 の調査結果で確定する。`PhysicsDebugRenderer` の private な `DebugVertex` / `AddLine` を外部 Helper から直接呼べないため、共通線分データ型または Renderer 内での変換境界を設ける。

## 初期 Debug 設定（提案）

| 設定 | 初期値 | 備考 |
|---|---|---|
| `ShowVectorFields` | `false` | 既存表示への影響を避ける |
| `ShowScalarFields` | `false` | 初期は無効 |
| `ShowFieldBounds` | `true` | 表示対象のサンプリング領域 |
| Grid resolution | `7 × 7 × 7` | 343サンプル |
| Grid spacing | `1.0 m` | world-space |
| Arrow base length | `0.5 m` | 強度に応じて調整・上限設定 |
| Scalar slice | `XZ` | 選択可能にする |
| Color range | Field別 | 単位と値域を保持 |

## テスト・受け入れ基準

- 一様ベクトル場では全矢印が同方向・同強度になる。
- 点重力・点電荷・双極子では向きと中心付近の安定性を確認できる。
- 異なる単位の Field が同一スケールに誤って正規化されない。
- Temperature の温度と Influence を区別できる。
- SDF の内外・境界・欠損サンプルを誤認しない。
- Debug 表示 OFF 時に不要な Field サンプリングを実行しない。
- カメラ未設定時の既存描画ガードを維持する。
- 既存 AABB / OBB / Contact / Solver Overlay が従来どおり動作する。

## 参照ソース

- [PhysicsField.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Field/PhysicsField.h)
- [GravityField.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Field/GravityField.h)
- [ElectricField.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Electromagnetism/ElectricField.h)
- [MagneticField.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Electromagnetism/MagneticField.h)
- [TemperatureField.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Thermal/TemperatureField.h)
- [DenseSDF.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Field/DenseSDF.h)
- [SparseSDF.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Field/SparseSDF.h)
- [PhysicsWorld.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/PhysicsWorld.h)
- [PhysicsDebugRenderer.cpp](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Debug/PhysicsDebugRenderer.cpp)
- [PhysicsDebugSettings.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Debug/PhysicsDebugSettings.h)
- [PhysicsDebugImmediatePanel.h](https://github.com/kou42/Raven/blob/master/Root/Root/Raven/Physics/Debug/PhysicsDebugImmediatePanel.h)

---

更新日: 2026-11-22  
状態: 設計ロードマップ。実装・ビルド検証は未着手。