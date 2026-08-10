#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"

#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Raven
{
namespace
{
// PrimitiveMeshFactoryは「形状を作る」責務だけを担当します。
// VertexArray / VertexBuffer / IndexBufferなどRenderer固有のGPUリソース生成はMeshへ移し、
// Factoryと描画バックエンドの依存を切り離します。
Ref<Mesh> CreateIndexedMesh(std::vector<MeshVertex> vertices, std::vector<uint32_t> indices)
{
    auto geometry = CreateRef<MeshGeometry>(
        std::move(vertices),
        std::move(indices),
        GeometryUsage::Static,
        TopologyUsage::Fixed);

    return CreateRef<Mesh>(std::move(geometry));
}

Ref<Mesh> CreateDynamicIndexedMesh(std::vector<MeshVertex> vertices, std::vector<uint32_t> indices)
{
    auto geometry = CreateRef<MeshGeometry>(
        std::move(vertices),
        std::move(indices),
        GeometryUsage::Dynamic,
        TopologyUsage::Fixed);

    return CreateRef<Mesh>(std::move(geometry));
}
} // namespace

Ref<Mesh> PrimitiveMeshFactory::CreateCube()
{
    constexpr float h = 0.5f;

    const std::vector<MeshVertex> vertices = {
        { {-h,-h, h}, {0.85f,0.55f,0.30f}, {0.0f,0.0f} },
        { { h,-h, h}, {0.85f,0.55f,0.30f}, {1.0f,0.0f} },
        { { h, h, h}, {0.85f,0.55f,0.30f}, {1.0f,1.0f} },
        { {-h, h, h}, {0.85f,0.55f,0.30f}, {0.0f,1.0f} },
        { { h,-h,-h}, {0.75f,0.45f,0.25f}, {0.0f,0.0f} },
        { {-h,-h,-h}, {0.75f,0.45f,0.25f}, {1.0f,0.0f} },
        { {-h, h,-h}, {0.75f,0.45f,0.25f}, {1.0f,1.0f} },
        { { h, h,-h}, {0.75f,0.45f,0.25f}, {0.0f,1.0f} },
        { { h,-h, h}, {0.90f,0.62f,0.34f}, {0.0f,0.0f} },
        { { h,-h,-h}, {0.90f,0.62f,0.34f}, {1.0f,0.0f} },
        { { h, h,-h}, {0.90f,0.62f,0.34f}, {1.0f,1.0f} },
        { { h, h, h}, {0.90f,0.62f,0.34f}, {0.0f,1.0f} },
        { {-h,-h,-h}, {0.70f,0.40f,0.22f}, {0.0f,0.0f} },
        { {-h,-h, h}, {0.70f,0.40f,0.22f}, {1.0f,0.0f} },
        { {-h, h, h}, {0.70f,0.40f,0.22f}, {1.0f,1.0f} },
        { {-h, h,-h}, {0.70f,0.40f,0.22f}, {0.0f,1.0f} },
        { {-h, h, h}, {0.95f,0.70f,0.40f}, {0.0f,0.0f} },
        { { h, h, h}, {0.95f,0.70f,0.40f}, {1.0f,0.0f} },
        { { h, h,-h}, {0.95f,0.70f,0.40f}, {1.0f,1.0f} },
        { {-h, h,-h}, {0.95f,0.70f,0.40f}, {0.0f,1.0f} },
        { {-h,-h,-h}, {0.65f,0.35f,0.20f}, {0.0f,0.0f} },
        { { h,-h,-h}, {0.65f,0.35f,0.20f}, {1.0f,0.0f} },
        { { h,-h, h}, {0.65f,0.35f,0.20f}, {1.0f,1.0f} },
        { {-h,-h, h}, {0.65f,0.35f,0.20f}, {0.0f,1.0f} },
    };

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
    }

    return CreateIndexedMesh(vertices, std::move(indices));
}

Ref<Mesh> PrimitiveMeshFactory::CreateSphere(int stacks, int slices)
{
    if (stacks < 2) stacks = 2;
    if (slices < 3) slices = 3;

    constexpr float radius = 0.5f;
    constexpr float pi = 3.14159265358979323846f;

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(stacks + 1) * static_cast<size_t>(slices + 1));
    indices.reserve(static_cast<size_t>(stacks) * static_cast<size_t>(slices) * 6);

    for (int i = 0; i <= stacks; ++i)
    {
        const float phi = pi * 0.5f - static_cast<float>(i) * pi / static_cast<float>(stacks);
        const float y = radius * std::sin(phi);
        const float ringRadius = radius * std::cos(phi);
        const float v = static_cast<float>(i) / static_cast<float>(stacks);

        for (int j = 0; j <= slices; ++j)
        {
            const float theta = static_cast<float>(j) * 2.0f * pi / static_cast<float>(slices);
            const float x = ringRadius * std::cos(theta);
            const float z = ringRadius * std::sin(theta);
            const float u = static_cast<float>(j) / static_cast<float>(slices);

            vertices.push_back({
                { x, y, z },
                { 0.7f + 0.3f * v, 0.8f, 0.9f },
                { u, v }
            });
        }
    }

    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
            const uint32_t b = a + static_cast<uint32_t>(slices + 1);
            indices.insert(indices.end(), { a, b, a + 1, b, b + 1, a + 1 });
        }
    }

    return CreateIndexedMesh(std::move(vertices), std::move(indices));
}

Ref<Mesh> PrimitiveMeshFactory::CreateDynamicGrid(int rows, int columns)
{
    rows = rows < 1 ? 1 : rows;
    columns = columns < 1 ? 1 : columns;

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(static_cast<size_t>(rows + 1) * static_cast<size_t>(columns + 1));
    indices.reserve(static_cast<size_t>(rows) * static_cast<size_t>(columns) * 6);

    // ========================================================================
    // Dynamic Grid Geometry
    // ========================================================================
    // XZ平面上の[-0.5,+0.5]を規則格子へ分割します。
    // Topologyは生成後固定し、WaveMeshDeformerなどはPositionだけを変更します。
    for (int row = 0; row <= rows; ++row)
    {
        const float v = static_cast<float>(row) / static_cast<float>(rows);
        const float z = v - 0.5f;

        for (int column = 0; column <= columns; ++column)
        {
            const float u = static_cast<float>(column) / static_cast<float>(columns);
            const float x = u - 0.5f;

            vertices.push_back({
                { x, 0.0f, z },
                { 0.25f + 0.65f * u, 0.55f + 0.35f * v, 0.95f },
                { u, v }
            });
        }
    }

    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const uint32_t a = static_cast<uint32_t>(row * (columns + 1) + column);
            const uint32_t b = a + static_cast<uint32_t>(columns + 1);

            indices.insert(indices.end(), {
                a, b, a + 1,
                b, b + 1, a + 1
            });
        }
    }

    return CreateDynamicIndexedMesh(std::move(vertices), std::move(indices));
}

} // namespace Raven
