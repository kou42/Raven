#pragma once

#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"
#include "Raven/Scene/SceneCamera.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace Raven
{

// Scene MeshのAPI非依存な描画Snapshotです。ModelはGLSLと同じcolumn-major順です。
struct RHISceneMesh
{
    Ref<RHIBuffer> VertexBuffer;
    Ref<RHIBuffer> IndexBuffer;
    uint32_t IndexCount = 0;
    // 0はScene Entityに属さない補助描画です。Picking Attachmentでは背景扱いにします。
    uint32_t EntityID = 0u;
    RHIMaterialProperties Material;
    std::array<float, 3> LocalCenter = {0.0f, 0.0f, 0.0f};
    std::array<float, 16> Model = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

class RHISceneDrawItemBuilder final
{
public:
    // VulkanはRavenのNDC深度[-1,1]を[0,1]へ変換し、ViewportのY方向も補正します。
    // 他Backendでは呼び出し元が適切なClip Correctionを選択します。
    static math::Mat4 VulkanClipCorrection();

    static math::Mat4 FromColumnMajor(const std::array<float, 16>& values);

    static std::array<float, 16> ToColumnMajor(const math::Mat4& matrix);

    // GPU操作前に描画データを値で構築し、Frame中のScene変更を参照しません。
    // ソート深度はMeshごとに事前計算したLocalCenterを代表点とする近似です。
    static std::vector<RHISceneDrawItem> Build(
        const math::Mat4& view,
        const math::Mat4& projection,
        const std::vector<RHISceneMesh>& meshes,
        const math::Mat4& clipCorrection);

    // 既存DemoのSceneCamera入口は行列版へ委譲し、変換規約を一か所に保ちます。
    static std::vector<RHISceneDrawItem> Build(
        const SceneCamera& camera,
        const std::vector<RHISceneMesh>& meshes,
        const math::Mat4& clipCorrection);
    // 呼び出し側のcapacityを再利用します。各Frameで全件再構築し、Sceneの参照や
    // 前FrameのMaterial値は保持しません。空入力でも古いResource参照を解放します。
    static void BuildInto(
        const math::Mat4& view,
        const math::Mat4& projection,
        const std::vector<RHISceneMesh>& meshes,
        const math::Mat4& clipCorrection,
        std::vector<RHISceneDrawItem>& items);
};

} // namespace Raven
