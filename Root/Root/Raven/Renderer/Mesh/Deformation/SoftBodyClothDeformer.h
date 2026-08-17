#pragma once

#include <cstdint>

#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"

namespace Raven
{

class SoftBodyClothDeformer : public MeshDeformer
{
public:
    SoftBodyClothDeformer(uint32_t rows, uint32_t columns);

    void Update(Mesh& mesh, float deltaTime) override;

    void SetCollisionSphere(const math::Vec3& center, float radius);
    void DisableCollisionSphere();

    // Clothローカル空間のPlane Colliderです。
    // Plane式は dot(normal, x) - offset = 0 とし、normal側をClothが存在できる外側とします。
    void SetCollisionPlane(const math::Vec3& normal, float offset);
    void DisableCollisionPlane();

    ph::SoftBodySolver& GetSolver() { return m_Solver; }
    const ph::SoftBodySolver& GetSolver() const { return m_Solver; }

private:
    bool InitializeFromMesh(Mesh& mesh);
    void ApplyCollisionSphereToSolver();
    void ApplyCollisionPlaneToSolver();

private:
    uint32_t m_Rows = 0u;
    uint32_t m_Columns = 0u;
    bool m_Initialized = false;

    bool m_CollisionSphereEnabled = false;
    math::Vec3 m_CollisionSphereCenter{};
    float m_CollisionSphereRadius = 0.0f;
    uint32_t m_CollisionSphereIndex = 0u;

    bool m_CollisionPlaneEnabled = false;
    math::Vec3 m_CollisionPlaneNormal{ 0.0f, 1.0f, 0.0f };
    float m_CollisionPlaneOffset = 0.0f;
    uint32_t m_CollisionPlaneIndex = 0u;

    ph::SoftBodySolver m_Solver;
    ph::SoftBodyCloth m_Cloth;
};

} // namespace Raven
