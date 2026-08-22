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
    // Human検証Layerは既存SceneGame.cppを変更せず、共通Materialと描画対象Entity Listへ
    // 最小限アクセスするためfriendとします。Human固有処理そのものはLayer側へ隔離します。
    //
    // 現在RenderScene()自体はECS Viewを直接走査しますが、Human Layerは生成したEntityの
    // ライフタイム管理など既存SceneGame内部状態へアクセスするため、このfriend関係は維持します。
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
    //
    // 描画対象はSceneGame固有のm_SpawnedEntitiesではなく、
    // View<TransformComponent, MeshRendererComponent>()を正規データとして直接走査します。
    // そのためCloth/JellyなどScene外部のLayerが生成した通常Entityも、MeshRendererComponentを
    // 持つだけでGame View / Scene Viewの両方へ自動的に参加できます。
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

    // SceneGame自身が生成したEntityの破棄管理用Listです。
    // 以前はRenderScene()の描画対象Listも兼ねていましたが、現在の描画対象はECS Viewが正規データです。
    // このListへ入っていないApplication Layer生成Entityでも、MeshRendererComponentを持てば描画されます。
    std::vector<Entity> m_SpawnedEntities;
    std::vector<SphereBody> m_SphereBodies;
    std::unordered_map<EntityID, size_t> m_SphereBodyIndexByEntity;
    Entity m_RuntimeCameraEntity;
    Entity m_FloorEntity;
    Entity m_BoxEntity;
    Entity m_AnimationTestEntity;

    // StateMachine検証用の周期タイマーです。Animation再生時間とは分離し、
    // Parameter入力シーケンスの時間だけを管理します。
    float m_AnimationStateMachineTime = 0.0f;

    // ========================================================================
    // Debug Visualization
    // ========================================================================
    // Physics Debug: H/B/O/F/T/P/C/N
    // Animation Debug: Y
    // PhysicsDebugRendererはRenderer Camera Contextを参照するため、Game ViewではSceneCamera、
    // Scene ViewではEditor Cameraへ自動的に追従します。
    ph::PhysicsDebugRenderer m_PhysicsDebugRenderer;
    AnimationDebugOverlayRenderer m_AnimationDebugRenderer;

    bool m_WasSpacePressed = false;

    // 左ボタンの前フレーム状態を保持してPressed/Releasedのエッジを検出します。
    bool m_WasLeftMousePressed = false;
    Entity m_DraggedEntity{};
    math::Vec2 m_DragStartScreen{};

    // RayCastが返した「実際にクリックしたワールド座標」です。
    // ドラッグ終了まで保持し、AddImpulseAtPoint()の作用点として使用します。
    math::Vec3 m_DragHitPoint{};

    // Projectionとマウス座標を対応させるViewportサイズ。
    // ※Windowサイズに合わせたほうがいいかも。TODO：合わせる対応をする
    float m_ViewportWidth = 1920.0f;
    float m_ViewportHeight = 1080.0f;

    // Mouse Ray生成ではPrimary SceneCameraのFOVを毎回同期して利用します。
    float m_CameraFovY = 0.7854f;
    float m_MouseRayMaxDistance = 1000.0f;

    // ドラッグ距離1pxあたりのImpulse量。
    // 長すぎるドラッグによる極端な速度を避けるため最大ピクセル数も制限します。
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
