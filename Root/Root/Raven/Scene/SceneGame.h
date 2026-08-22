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

#include <unordered_map>
#include <vector>

namespace Raven
{

// ============================================================================
// SceneGame
// ============================================================================
// Runtime Sceneとして通常のSceneライフサイクルを実装すると同時に、Scene View用に
// SceneViewportRendererも実装します。
//
// Camera行列はSceneGameへミラーせず、描画時にCameraをRenderer::BeginScene()へ渡します。
// Game ViewはPrimary SceneCamera、Scene Viewは渡されたEditor Cameraを同じRenderScene()へ
// 流すことで、Material / PhysicsDebugを含む全描画がRenderer Camera Contextを共有します。
class SceneGame : public Scene, public SceneViewportRenderer
{
    // Human検証Layerは既存SceneGame.cppを変更せず、共通MaterialとEntity生成管理へ
    // 最小限アクセスするためfriendとします。Human固有処理そのものはLayer側へ隔離します。
    friend class Gltf::HumanSkinningDebugLayer;

public:
    SceneGame()
        : m_PhysicsDebugRenderer(*this)
        , m_AnimationDebugRenderer(*this)
    {
        // Human.glbが未配置でもLayer側が安全にskipします。
        // 実際のGLB読込はSceneGame::OnCreate()完了後、最初のUpdateまで遅延されます。
        PushLayer(CreateScope<Gltf::HumanSkinningDebugLayer>(*this));
    }

    virtual void OnCreate() override;
    virtual void OnDestroy() override;
    virtual void OnUpdateGame(float dt) override;
    virtual void OnRender() override;
    virtual void OnEvent(Event& e) override;

    // Scene ViewはEditor Cameraをそのまま描画入口へ渡します。
    // Runtime Cameraの状態を書き換えないため、Game ViewとScene ViewのCameraが完全に分離されます。
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

    // Box物理の目視確認用Entityを生成します。
    // SceneGameは「何を置くか」だけを担当し、Cube頂点生成はPrimitiveMeshFactoryへ分離します。
    void SpawnBoxTestBody();

    // AnimationClip -> Animator -> AnimatorComponent -> AnimationSystem の
    // 一連の再生経路を目視確認するためのCubeを生成します。
    // Physics Componentを付けないことで、Transformの所有者をAnimationSystemだけに限定します。
    void SpawnAnimationTestCube();

    // StateMachine検証CubeへSpeed / Grounded / Jumpを自動入力します。
    // Physicsや実入力に依存しない決定的なシーケンスにすることで、Transition実装だけを
    // Scene上で切り分けて目視確認できるようにします。
    void UpdateAnimationStateMachineTest(float deltaTime);

    // ========================================================================
    // Runtime Camera
    // ========================================================================
    // Primary Camera EntityのTransform/CameraComponentを同期し、実際に利用するSceneCameraを返します。
    // SceneGameはView/Projectionを保持せず、Cameraオブジェクト自体を正規データとして扱います。
    SceneCamera* UpdateRuntimeCamera();

    // 指定CameraでScene本体を描画する共通入口です。
    // Game View / Scene Viewの差は引数Cameraだけに限定し、Renderer Camera Contextを確定します。
    // 描画対象はm_SpawnedEntitiesではなくECSのTransform + MeshRendererを直接走査します。
    // これによりSoftBodyやDebug Layerが生成した通常Entityも自動的に描画対象になります。
    void RenderScene(const Camera& camera);

    // ========================================================================
    // Mouse Drag Impulse / Physics Ray Picking
    // ========================================================================
    // Mouse PickingはPrimary SceneCameraのViewを直接参照します。
    // SceneGameにCamera行列を複製しないことで、描画Cameraとの状態二重化を防ぎます。
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

    // SceneGame自身が生成したEntityの破棄管理にだけ使用します。
    // 描画対象の正規データはECSのMeshRendererComponentです。
    std::vector<Entity> m_SpawnedEntities;
    std::vector<SphereBody> m_SphereBodies;
    std::unordered_map<EntityID, size_t> m_SphereBodyIndexByEntity;
    Entity m_RuntimeCameraEntity;
    Entity m_FloorEntity;
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
