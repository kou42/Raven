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
    static math::Mat4 VulkanClipCorrection()
    {
        return math::Mat4(
            1.0f,  0.0f, 0.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            0.0f,  0.0f, 0.5f, 0.5f,
            0.0f,  0.0f, 0.0f, 1.0f);
    }

    static math::Mat4 FromColumnMajor(const std::array<float, 16>& values)
    {
        math::Mat4 result{};
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                result.m[row][column] = values[column * 4 + row];
            }
        }
        return result;
    }

    static std::array<float, 16> ToColumnMajor(const math::Mat4& matrix)
    {
        std::array<float, 16> result{};
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                result[column * 4 + row] = matrix.m[row][column];
            }
        }
        return result;
    }

    // GPU操作前に描画データを値で構築し、Frame中のScene変更を参照しません。
    // ソート深度は従来どおりMeshの頂点平均位置を代表点とする近似です。
    static std::vector<RHISceneDrawItem> Build(
        const math::Mat4& view,
        const math::Mat4& projection,
        const std::vector<RHISceneMesh>& meshes,
        const math::Mat4& clipCorrection)
    {
        const math::Mat4 viewProjection =
            clipCorrection * projection * view;
        std::vector<RHISceneDrawItem> items;
        items.reserve(meshes.size());
        for (const RHISceneMesh& mesh : meshes)
        {
            RHISceneDrawItem item;
            item.VertexBuffer = mesh.VertexBuffer;
            item.IndexBuffer = mesh.IndexBuffer;
            item.IndexCount = mesh.IndexCount;
            item.Material = mesh.Material;
            const math::Mat4 model = FromColumnMajor(mesh.Model);
            item.ClipTransform = ToColumnMajor(viewProjection * model);
            const math::Mat4 modelView = view * model;
            const auto& center = mesh.LocalCenter;
            item.ViewDepth = modelView.m[2][0] * center[0] +
                modelView.m[2][1] * center[1] +
                modelView.m[2][2] * center[2] + modelView.m[2][3];
            items.push_back(std::move(item));
        }
        return items;
    }

    // 既存DemoのSceneCamera入口は行列版へ委譲し、変換規約を一か所に保ちます。
    static std::vector<RHISceneDrawItem> Build(
        const SceneCamera& camera,
        const std::vector<RHISceneMesh>& meshes,
        const math::Mat4& clipCorrection)
    {
        return Build(
            camera.GetViewMatrix(),
            camera.GetProjectionMatrix(),
            meshes,
            clipCorrection);
    }
};

} // namespace Raven
