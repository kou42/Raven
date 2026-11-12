#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"

namespace Raven
{
math::Mat4 RHISceneDrawItemBuilder::VulkanClipCorrection()
{
    return math::Mat4(
        1.0f,  0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f,  0.0f, 0.5f, 0.5f,
        0.0f,  0.0f, 0.0f, 1.0f);
}

math::Mat4 RHISceneDrawItemBuilder::FromColumnMajor(const std::array<float, 16>& values)
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

std::array<float, 16> RHISceneDrawItemBuilder::ToColumnMajor(const math::Mat4& matrix)
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

std::vector<RHISceneDrawItem> RHISceneDrawItemBuilder::Build(
    const math::Mat4& view,
    const math::Mat4& projection,
    const std::vector<RHISceneMesh>& meshes,
    const math::Mat4& clipCorrection)
{
    std::vector<RHISceneDrawItem> items;
    BuildInto(view, projection, meshes, clipCorrection, items);
    return items;
}

std::vector<RHISceneDrawItem> RHISceneDrawItemBuilder::Build(
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

void RHISceneDrawItemBuilder::BuildInto(
    const math::Mat4& view,
    const math::Mat4& projection,
    const std::vector<RHISceneMesh>& meshes,
    const math::Mat4& clipCorrection,
    std::vector<RHISceneDrawItem>& items)
{
    const math::Mat4 viewProjection =
        clipCorrection * projection * view;
    // 確保失敗時は既存Snapshotを残します。容量確保後の構築は値と共有参照の
    // コピーだけなので、古いResource参照を解放して同じ領域へ再構築できます。
    items.reserve(meshes.size());
    items.clear();
    for (const RHISceneMesh& mesh : meshes)
    {
        RHISceneDrawItem item;
        item.VertexBuffer = mesh.VertexBuffer;
        item.IndexBuffer = mesh.IndexBuffer;
        item.IndexCount = mesh.IndexCount;
        item.EntityID = mesh.EntityID;
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
}

} // namespace Raven
