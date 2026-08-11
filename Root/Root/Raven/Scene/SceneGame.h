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
// 通常のOnRender()ではSceneGame自身が保持するRuntime Camera行列を使用します。
// RenderWithCamera()ではCamera基底からView/Projectionを取得して一時的に差し替え、
// 描画後に元へ戻すことでEditor Camera操作がRuntime Camera状態を変更しないようにします。
class SceneGame : public Scene, public SceneViewportRenderer
{
public:
    SceneGame()
        : m_PhysicsDebugRenderer(*this, m_View, m_Projection)
        , m_AnimationDebugRenderer(*this)
    {
    }

    virtual void OnCreate() override;
    virtual void OnDestroy() override;
    virtual void OnUpdateGame(float dt) override;
    virtual void OnRender() override;
    virtual void OnEvent(Event& e) override;

    // ========================================================================
    // Scene View camera override
    // ========================================================================
    // PhysicsDebugRendererはm_View/m_Projectionへの参照を保持しているため、
    // 描画関数の引数だけを各Materialへ渡す方式ではDebug表示だけRuntime Cameraのままになります。
    // そこでSceneGameが現在利用している行列を一時的に差し替えてOnRender()を再利用します。
    //
    // SceneViewportRendererはCamera基底だけを受け取るため、ここではEditorCameraという具体型を
    // 一切参照しません。将来SceneCameraを渡す場合も同じ描画経路をそのまま利用できます。
    //
    // 描画終了後には必ず元のRuntime Camera行列へ戻すため、外部CameraによるScene View描画が
    // Game Viewや次frameのRuntime入力へ副作用を残しません。
    void RenderWithCamera(const Camera& camera) override
    {
        const math::Mat4 runtimeView = m_View;
        const math::Mat4 runtimeProjection = m_Projection;

        m_View = camera.GetViewMatrix();
        m_Projection = camera.GetProjectionMatrix();

        OnRender();

        m_View = runtimeView;
        m_Projection = runtimeProjection;
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
    // Mouse Drag Impulse / Physics Ray Picking
    // ========================================================================
    // マウス座標からCamera Rayを構築し、PhysicsWorld::RayCast()で実Colliderを選択します。
    // リリース時にはRayCastで得た実際のヒット点へAddImpulseAtPoint()を適用するため、
    // 重心から外れた場所を掴んだ場合は r x J による回転も自然に発生します。
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

    // ========================================================================
    // Runtime Camera matrices
    // ========================================================================
    // 通常のGame View / Runtime描画で利用するCamera行列です。
    // Scene View描画時だけRenderWithCamera()が一時差し替えしますが、描画完了後に復元されます。
    math::Mat4 m_View;
    math::Mat4 m_Projection;

    std::vector<Entity> m_SpawnedEntities;
    std::vector<SphereBody> m_SphereBodies;
    std::unordered_map<EntityID, size_t> m_SphereBodyIndexByEntity;
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
    // PhysicsDebugRendererはm_View/m_Projectionを参照するため、RenderWithCamera()の一時差し替えにも
    // 自動的に追従し、Scene ViewではEditor Cameraから見たDebug Shapeを描画できます。
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

    // Perspective()と同じFOVをMouse Ray生成でも使います。
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
