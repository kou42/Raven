#pragma once

#include <utility>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"

namespace Raven
{

class Mesh;

// ============================================================================
// MeshDeformationInstance
// ============================================================================
// 1つのMeshと1つのDeformerを組として保持し、毎フレームの更新入口を1箇所にまとめます。
//
// Scene / ECS側がSkeletal・Morph・SoftBodyそれぞれの具体型を意識せず、
// instance.Update(dt)だけで変形を進められる形を先に定義しておきます。
class MeshDeformationInstance
{
public:
    MeshDeformationInstance(Ref<Mesh> mesh, Scope<MeshDeformer> deformer)
        : m_Mesh(std::move(mesh)),
          m_Deformer(std::move(deformer))
    {
    }

    void Update(float deltaTime)
    {
        if (!m_Mesh || !m_Deformer)
        {
            return;
        }

        m_Deformer->Update(*m_Mesh, deltaTime);
    }

    const Ref<Mesh>& GetMesh() const
    {
        return m_Mesh;
    }

    MeshDeformer* GetDeformer() const
    {
        return m_Deformer.get();
    }

private:
    Ref<Mesh> m_Mesh;
    Scope<MeshDeformer> m_Deformer;
};

} // namespace Raven
