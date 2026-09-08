#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneViewportRenderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"
#include "Raven/Animation/Debug/AnimationDebugOverlayRenderer.h"
#include "Raven/Gltf/Debug/HumanSkinningDebugLayer.h"
#include "Raven/Gltf/StaticSceneSpawner.h"

#include <unordered_map>
#include <vector>

namespace Raven
{

class SceneGame : public Scene, public SceneViewportRenderer
{
    friend class Gltf::HumanSkinningDebugLayer;

public:
    SceneGame()
        : m_PhysicsDebugRenderer(*this)
        , m_AnimationDebugRenderer(*this)
    {
        // Human/Terrainとも実Asset読込はOnCreate完了後の最初のUpdateまで遅延します。
        // Terrain.glbが未配置でもLayer側で安全にskipするため、既存検証Sceneはそのまま利用できます。
        PushLayer(CreateScope<Gltf::HumanSkinningDebugLayer>(*this));
        PushLayer(CreateScope<Gltf::TerrainStaticSceneLayer>(*this));
    }

    virtual void OnCreate() override;
    virtual void OnDestroy() override;
    virtual void OnUpdateGame(float dt) override;
    virtual void OnRender() override;
    virtual void OnEvent(Event& e) override;

    void RenderWithCamera(const Camera& camera) override
    {
        RenderScene(camera);
    }

private:
    struct SphereBody
    {
        Entity EntityHandle;
        math::Vec3 Velocity{ 0.0f, 0.0f, 0.0f };
        math::Vec3 Tint{ 1.0f, 1.0f, 1.0f };
        float Radius = 0.5f;
    };

    void SpawnSphereBatch(int count);
    void ClearSphereBatch();
    int ComputeOptimizedSpawnCount() const;
    void SpawnBoxTestBody();
    void SpawnAnimationTestCube();
    void UpdateAnimationStateMachineTest(float deltaTime);
    SceneCamera* UpdateRuntimeCamera();
    void RenderScene(const Camera& camera);
    void UpdateMouseDragImpulse();
    bool BuildMouseRay(const math::Vec2& screenPoint, math::Vec3& outOrigin, math::Vec3& outDirection) const;

    ShaderLibrary m_ShaderLibrary;
    Ref<Shader> m_Shader;
    Ref<VertexArray> m_VertexArray;
    Ref<Mesh> m_Mesh;
    Ref<Material> m_Material;
    Ref<VertexArray> m_ShadowVertexArray;
    Ref<Mesh> m_ShadowMesh;
    Ref<Material> m_ShadowMaterial;

    Ref<Mesh> m_SphereMesh;
    Ref<Mesh> m_BoxMesh;

    TextureLibrary m_TextureLibrary;
    Ref<Texture> m_Texture;

    std::vector<SphereBody> m_SphereBodies;
    std::unordered_map<EntityID, size_t> m_SphereBodyIndexByEntity;
    Entity m_RuntimeCameraEntity;
    Entity m_FloorEntity;
    Entity m_WaveEntity;
    Entity m_BoxEntity;
    Entity m_AnimationTestEntity;

    float m_AnimationStateMachineTime = 0.0f;

    ph::PhysicsDebugRenderer m_PhysicsDebugRenderer;
    AnimationDebugOverlayRenderer m_AnimationDebugRenderer;

    bool m_WasSpacePressed = false;
    bool m_WasLeftMousePressed = false;
    Entity m_DraggedEntity{};
    math::Vec2 m_DragStartScreen{};
    math::Vec3 m_DragHitPoint{};

    float m_ViewportWidth = 1920.0f;
    float m_ViewportHeight = 1080.0f;
    float m_CameraFovY = 0.7854f;
    float m_MouseRayMaxDistance = 1000.0f;
    float m_DragImpulsePerPixel = 0.035f;
    float m_MaxDragPixels = 350.0f;
    float m_MinDragPixels = 3.0f;

    int m_MinSphereCount = 50;
    int m_MaxSphereCount = 100;
    float m_TargetSphereDensity = 0.015f;

    float m_Gravity = -9.8f;
    float m_SphereRadius = 0.5f;
    float m_FloorY = 0.0f;
    float m_BounceDamping = 0.65f;
    float m_GroundFriction = 3.0f;
    float m_BounceTangentialDamping = 0.92f;
    float m_StopVelocityEpsilon = 0.08f;

    float m_SpawnRangeXZ = 24.0f;
    float m_SpawnHeightMin = 6.0f;
    float m_SpawnHeightMax = 14.0f;
    float m_InitialVelocityXMin = -6.0f;
    float m_InitialVelocityXMax = 6.0f;
    float m_InitialVelocityZMin = -6.0f;
    float m_InitialVelocityZMax = 6.0f;
    float m_SphereScaleMin = 0.7f;
    float m_SphereScaleMax = 1.5f;
};

} // namespace Raven
