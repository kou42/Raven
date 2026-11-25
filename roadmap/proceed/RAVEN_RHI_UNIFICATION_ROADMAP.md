# Raven RHI 統一ロードマップ

## 目的

Legacy / Explicit RHI の二重管理を解消し、OpenGL・Vulkan・DirectX12 で共通の公開 API、リソース管理、描画契約を使用する。Backend 固有の実装差は Platform 層に閉じ込める。

対象: https://github.com/kou42/Raven

本書は設計・実装計画であり、各項目は未着手として記載する。既存の `roadmap/proceed/RHI_MAINLINE_ROADMAP.md` と `RHI_MIGRATION_CHECKLIST.md` との整合確認を Phase 0 で行う。

## 基本方針

-   既存 RHIDevice / RHIBuffer / RHITexture を活用し、重複する抽象化を増やさない。
-   Material の色・Alpha・Texture は単一の管理元を持つ。SurfaceType は独立した描画分類とする。
-   CPU データと GPU リソースを分離し、GPU 生成・更新・破棄の所有権と同期を明確にする。
-   互換 API は移行期間のみ残し、共通実装へ委譲する。二重書き込みを恒久化しない。
-   各 Phase で OpenGL / Vulkan / D3D12 の回帰確認を行う。未対応機能は明示的に報告する。

## 現状と統一対象

パスは `Root/Root/Raven/` を基準とする。以下は確認済みの主要な境界と設計候補であり、網羅的な呼び出し元調査は Phase 0 で実施する。

  -----------------------------------------------------------------------------------------------------------------------------------------------------------
  対象                                     現状                                                                        目標
  ---------------------------------------- --------------------------------------------------------------------------- --------------------------------------
  Renderer/Material/Material.h             SetUniform(u_Alpha/u_Tint), SetRHITint                                      SetBaseColor / SetAlpha

  Material                                 SetTexture / SetRHITexture                                                  共通 Texture API

  Material                                 Bind / BindForSurface / GetRHIProperties                                    共通 Snapshot と Renderer 側 Binding

  Renderer/Texture/Texture.h               Texture / RHITexture、Specification・Format・Usage の重複                   GPU 本体を RHITexture に集約

  Assets/TextureAsset.h                    FinalizeRuntimeTexture / CreateRHITexture                                   Device 単位の共通生成・キャッシュ

  Renderer/Mesh/Mesh.h                     BuildLegacyResources / BuildRHIResources、SyncGeometry / SyncRHIResources   共通 Resource 生成・同期

  Mesh                                     VertexArray / RHIBuffer、Revision の二重管理                                共通 Buffer と Revision

  Buffer / VertexArray                     VertexBuffer / IndexBuffer / RHIBuffer                                      RHIBuffer、入力 Layout の分離

  Pipeline / Shader                        Pipeline / RHIGraphicsPipeline、SpecificationPipeline                       共通 Pipeline 生成・Shader 契約

  Renderer/RHI                             RHICommandList / RHISceneCommandList                                        共通描画命令

  Framebuffer / RHIOffscreenRenderTarget   描画先・Attachment の異なる経路                                             共通 RenderTarget 契約

  Renderer/Renderer.h                      SetExplicitSceneMode / IsExplicitSceneMode                                  共通 Scene Queue・Pass

  UI/Core/UITextureView.h                  GetLegacyTexture / GetRHITexture                                            共通 Texture 参照

  UI/Widgets/UIImage.h                     SetRenderTargetTexture / SetRHIRenderTargetTexture                          共通 Texture 設定と原点情報

  UI / Debug                               ExplicitUIRenderer、Physics・Animation の経路分岐                           共通 Geometry / Draw Queue

  Core/Application.h / RHI                 IExplicitSceneRuntime、Legacy Factory                                       共通 Runtime・Frame Lifecycle
  -----------------------------------------------------------------------------------------------------------------------------------------------------------

## Phase 0 --- Legacy 境界監査

-   [ ] 既存ロードマップとチェックリストを確認し、重複・完了済み項目を整理する。
-   [ ] Legacy / Explicit / IsExplicitSceneMode / HasLegacyPipeline の全呼び出し元を調査する。
-   [ ] Backend ごとの機能対応表、Resource 所有権、Frame 同期、既存テストを整理する。
-   [ ] 3 Backend のビルド・起動・描画基準を記録する。

完了条件: 移行対象、依存関係、未対応機能、検証基準が確定している。

## Phase 1 --- Material Color 統一

-   [ ] SetBaseColor / GetBaseColor / SetAlpha / GetAlpha を追加する。
-   [ ] RGBA を単一の管理元にし、Legacy Uniform と RHI Snapshot はそこから生成する。
-   [ ] u_Tint / u_Alpha の互換処理を整理し、任意 Uniform との衝突を防ぐ。
-   [ ] SurfaceType の明示設定を尊重し、既存の Alpha 推論との互換性を検証する。
-   [ ] EditorBackendComparisonScene、ExplicitCubeSceneDemo、テストを移行する。

完了条件: 色・Alpha の二重設定なしで同一の描画結果と透明分類が得られる。

## Phase 2 --- Texture 統一

-   [ ] Texture / RHITexture と Format・Usage・Specification の対応を整理する。
-   [ ] RHIDevice::CreateTexture を GPU 生成の共通入口とする。
-   [ ] TextureAsset の CPU decode と GPU finalize を分離したまま共通化する。
-   [ ] Device 世代・寿命を考慮した GPU Texture キャッシュを設計する。
-   [ ] UITextureView / UIImage の Texture 参照を共通化する。
-   [ ] Bind / Unbind / GetID の上位層依存を段階的に除去する。

完了条件: 同一 Asset を Backend 分岐なしで使用でき、UV・原点・Format・Mip の契約が維持される。

## Phase 3 --- Buffer / Mesh 統一

-   [ ] VertexBuffer / IndexBuffer を RHIBuffer に集約する。
-   [ ] VertexArray の入力 Layout と OpenGL VAO 管理を分離する。
-   [ ] BuildResources / SyncGeometry を共通入口とする。
-   [ ] LegacyMeshResourceCreation を Backend 非依存の生成方針へ移行する。
-   [ ] Resource Revision を単一の同期契約へ整理する。複数 Device を扱う場合は Device ごとに管理する。
-   [ ] Dynamic Geometry、SoftBody、Cloth、Skinning の更新を検証する。

完了条件: CPU Geometry から共通 GPU Buffer を生成・更新でき、更新失敗時に旧 Resource を維持できる。

## Phase 4 --- Material Properties 統一

-   [ ] 共通 MaterialProperties / GetProperties を設計する。
-   [ ] BaseColor、Texture、SurfaceType、必要な Shader Parameter を Snapshot 化する。
-   [ ] Texture を用途別に管理し、既存の名前・slot 指定との互換性を整理する。
-   [ ] Legacy と Explicit の描画側を同じ Snapshot に接続する。

完了条件: 描画経路によらず同一 Material データを参照する。未対応の Masked / 任意 Shader 等を黙って無視しない。

## Phase 5 --- Shader / Pipeline 統一

-   [ ] Shader Asset、Backend Binary、Resource Layout の責務を分離する。
-   [ ] OpenGL を含む RHIDevice の Graphics Pipeline 生成契約を整備する。
-   [ ] PipelineSpecification と RHIGraphicsPipeline の生成経路を共通化する。
-   [ ] Pipeline Cache を Renderer 側へ移管する。
-   [ ] Material の SetPipeline / GetPipeline / ResolveSurfacePipeline 依存を段階的に除去する。

完了条件: SurfaceType、Attachment Format、Vertex Layout、Shader 等から適切な Pipeline を解決できる。

## Phase 6 --- CommandList 統一

-   [ ] RHICommandList / RHISceneCommandList の重複命令と失敗契約を整理する。
-   [ ] Pipeline、Vertex/Index Buffer、Resource Binding、Viewport、Scissor、DrawIndexed を共通化する。
-   [ ] Render Pass の開始・終了契約を整備する。
-   [ ] Uniform / Descriptor / Constant Buffer の Backend 差を吸収する。
-   [ ] OpenGL の State 退避・復元を必要に応じ Backend 内部へ移管する。

完了条件: 共通描画命令で各 Backend が描画でき、Resource の GPU 使用期間が保証される。

## Phase 7 --- RenderTarget 統一

-   [ ] Framebuffer / RHIOffscreenRenderTarget の Attachment・Resize・Clear 契約を整理する。
-   [ ] Color / Depth / MRT / Picking / Readback を共通化する。
-   [ ] Resource transition、GPU 同期、座標原点、MSAA の扱いを明確にする。
-   [ ] Editor Viewport の Texture 参照を共通化する。

完了条件: Scene View、Picking、Resize、UI への描画結果表示が各 Backend で成立する。

## Phase 8 --- Renderer 統一

-   [ ] Scene Queue、PrepareFrame、Draw Item、Render Pass を全 Backend で共有する。
-   [ ] Opaque / Transparent の分類・ソート・Depth / Blend 契約を統一する。
-   [ ] Material::Bind / BindForSurface の責務を Renderer へ移管する。
-   [ ] Scene 側の IsExplicitSceneMode / HasLegacyPipeline 依存を除去する。

完了条件: 同じ Scene / Mesh / Material の公開 API から全 Backend で描画できる。

## Phase 9 --- UI / Debug 描画統一

-   [ ] UIGeometryBuilder を活用して UI の描画 Queue・Batch・Clip 契約を共有する。
-   [ ] ExplicitUIRenderer と既存 UI 描画経路の共通化を進める。
-   [ ] PhysicsDebugRenderer / AnimationDebugOverlayRenderer を共通 Debug Queue へ接続する。
-   [ ] SoftBody / Fluid / Editor の Backend 分岐を除去する。

完了条件: UI、Debug Line、Overlay、選択表示が共通 API で動作する。

## Phase 10 --- Frame Lifecycle 統一

-   [ ] IExplicitSceneRuntime / RHISceneFrameLifecycle を全 Backend の共通契約へ拡張する。
-   [ ] Application の初期化、Prepare、BeginFrame、Pass、EndFrame、Present を共通化する。
-   [ ] Device / Context / Swapchain の所有権を整理し、二重生成を防ぐ。
-   [ ] Resize、最小化、失敗時の復旧、終了時の Resource 解放順序を検証する。
-   [ ] Runtime Factory と Legacy Factory を整理する。

完了条件: Application が Backend 固有の Frame 処理を直接分岐せずに動作する。

## Phase 11 --- Legacy 互換層削除

-   [ ] 全呼び出し元の移行を確認する。
-   [ ] SetRHITint / SetRHITexture / HasLegacyPipeline 等の旧 API を削除する。
-   [ ] 不要な Bridge、Factory、重複型、古いコメントを削除する。
-   [ ] ビルド設定、テスト、ドキュメントを更新する。
-   [ ] OpenGL / Vulkan / D3D12 の最終回帰テストを実施する。

完了条件: 上位層に Legacy / Explicit の選択責務が残らず、既存機能が維持される。

## 依存関係と進め方

Phase 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 を基本順序とする。ただし Phase 2・3 に必要な最小限の CommandList / Device 拡張、Phase 6・7 に必要な Frame Lifecycle 契約の整理は先行してよい。既存経路を維持した縦断的な小規模移行を優先し、一括置換を避ける。

## 共通検証項目

  項目         確認内容
  ------------ -------------------------------------------------------
  Build        3 Backend の対象構成がコンパイル・リンクできる
  Material     色、Alpha、透明ソート、DepthWrite、Blend
  Texture      Asset、Fallback、UV、RenderTarget 原点
  Mesh         静的・動的 Geometry、Revision、更新失敗時の再試行
  Editor       Scene View、Picking、Resize、選択表示
  UI / Debug   Clip、Batch、Font、Physics / Animation Overlay
  Lifecycle    起動、最小化、復帰、終了、GPU Resource 寿命
  性能         DrawCalls、Upload 回数、Resource 重複生成、Frame 時間

各 Phase の PR に変更対象、互換性、実行したテスト、未実施項目、残課題を記録する。実行していない検証を完了扱いにしない。

## 最終アーキテクチャ

``` text
Scene / ECS / UI / Debug
          |
          v
Material / Mesh / Asset (共通データ)
          |
          v
Renderer / Render Queue / Render Pass
          |
          v
RHIDevice / RHICommandList / Frame Lifecycle
          |
          +-- OpenGL
          +-- Vulkan
          +-- DirectX12
```

統一の成功条件は、Backend の違いが上位 API の使い分けではなく、RHI の実装差として表現されることである。
