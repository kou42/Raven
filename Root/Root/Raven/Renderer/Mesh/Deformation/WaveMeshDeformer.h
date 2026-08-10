#pragma once

#include <vector>

#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// WaveMeshDeformer
// ============================================================================
// Dynamic Geometry + Fixed Topologyの更新経路を実際に検証するための最小Deformerです。
// 初期頂点位置を保持し、X/Z位置と経過時間からY座標だけを波形変形します。
//
// このクラス自体は学習・検証用ですが、Update()の流れは将来のMorph/Skeletalと同じです。
class WaveMeshDeformer final : public MeshDeformer
{
public:
    WaveMeshDeformer(float amplitude = 0.15f, float frequency = 8.0f, float speed = 2.0f)
        : m_Amplitude(amplitude),
          m_Frequency(frequency),
          m_Speed(speed)
    {
    }

    void Update(Mesh& mesh, float deltaTime) override;

private:
    void CaptureBasePositions(Mesh& mesh);

private:
    std::vector<math::Vec3> m_BasePositions;
    float m_ElapsedTime = 0.0f;
    float m_Amplitude = 0.15f;
    float m_Frequency = 8.0f;
    float m_Speed = 2.0f;
};

} // namespace Raven
