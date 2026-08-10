#include "Raven/Renderer/Mesh/Deformation/WaveMeshDeformer.h"

#include <cmath>

#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{

void WaveMeshDeformer::CaptureBasePositions(Mesh& mesh)
{
    const Ref<MeshGeometry>& geometry = mesh.GetGeometry();
    if (!geometry)
    {
        return;
    }

    const auto& vertices = geometry->GetVertices();
    m_BasePositions.clear();
    m_BasePositions.reserve(vertices.size());

    for (const MeshVertex& vertex : vertices)
    {
        m_BasePositions.push_back(vertex.Position);
    }
}

void WaveMeshDeformer::Update(Mesh& mesh, float deltaTime)
{
    const Ref<MeshGeometry>& geometry = mesh.GetGeometry();
    if (!geometry || geometry->GetGeometryUsage() != GeometryUsage::Dynamic)
    {
        return;
    }

    const auto& sourceVertices = geometry->GetVertices();
    if (sourceVertices.empty())
    {
        return;
    }

    // 初回だけ「変形前」の頂点位置を保存します。
    // 毎フレーム前回の変形結果へさらに加算すると波形が累積して崩れるため、
    // 常に基準形状から現在時刻のPoseを再計算します。
    if (m_BasePositions.size() != sourceVertices.size())
    {
        CaptureBasePositions(mesh);
    }

    if (m_BasePositions.size() != sourceVertices.size())
    {
        return;
    }

    m_ElapsedTime += deltaTime;

    std::vector<MeshVertex> deformedVertices = sourceVertices;

    for (std::size_t i = 0; i < deformedVertices.size(); ++i)
    {
        const math::Vec3& base = m_BasePositions[i];

        // X/Zの両方向を使った単純な進行波です。
        // GeometryのTopologyは一切変更せずPositionだけを書き換えるため、
        // Dynamic Geometry + Fixed Topologyの代表例になります。
        const float phase = (base.x + base.z) * m_Frequency + m_ElapsedTime * m_Speed;
        deformedVertices[i].Position = base;
        deformedVertices[i].Position.y += std::sin(phase) * m_Amplitude;
    }

    if (!geometry->SetVertices(std::move(deformedVertices)))
    {
        return;
    }

    // CPU側Revisionが更新されたので、既存VBOへ変更分を同期します。
    // Mesh::SyncGeometry()はRevisionが同じ場合にUploadを省略します。
    mesh.SyncGeometry();
}

} // namespace Raven
