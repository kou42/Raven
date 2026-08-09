#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

#include <unordered_map>
#include <vector>

namespace Raven
{

class SceneGame : public Scene
{
public:
    SceneGame()
        : m_PhysicsDebugRenderer(*this, m_View, m_Projection)
    {
    }

    virtual void OnCreate() override;
    virtual void OnDestroy() override;
    virtual void OnUpdateGame(float dt) override;
    virtual void OnRender() override;
    virtual void OnEvent(Event& e) override;

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

    // ========================================================================
    // Mouse Drag Impulse
    // ========================================================================
    // 画面上のマウス座標に最も近いDynamic RigidBodyを選択し、
    // ドラッグ終了時に画面のドラッグ方向をCamera Right/Upへ変換してImpulseを与えます。
    void UpdateMouseDragImpulse();
    Entity FindDraggableEntityAtScreenPoint(const math::Vec2& screenPoint) const;
    bool ProjectWorldToScreen(const math::Vec3& worldPosition, math::Vec2& outScreenPoint) const;
    float ComputeProjectedPickRadius(const Entity& entity, const math::Vec3& cameraRight) const;

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

    math::Mat4 m_View;
    math::Mat4 m_Projection;

    std::vector<Entity> m_SpawnedEntities;
    std::vector<SphereBody> m_SphereBodies;
    std::unordered_map<EntityID, size_t> m_SphereBodyIndexByEntity;
    Entity m_FloorEntity;
    Entity m_BoxEntity;

    // ========================================================================
    // Broad Phase Debug Visualization
    // ========================================================================
    // B : Collider AABBのワイヤーフレーム表示 ON/OFF
    // P : Broad Phase候補ペア線表示 ON/OFF
    ph::PhysicsDebugRenderer m_PhysicsDebugRenderer;

    bool m_WasSpacePressed = false;

    // 左ボタンの前フレーム状態を保持して、Pressed/Releasedのエッジを検出します。
    bool m_WasLeftMousePressed = false;
    Entity m_DraggedEntity{};
    math::Vec2 m_DragStartScreen{};

    // Projectionで使用している現在のViewportサイズです。
    // WindowResize時に更新することで、画面座標との対応を維持します。
    float m_ViewportWidth = 1280.0f;
    float m_ViewportHeight = 720.0f;

    // ドラッグ距離1pxあたりのImpulse量。
    // 長すぎるドラッグによる極端な速度を避けるため最大ピクセル数も制限します。
    float m_DragImpulsePerPixel = 0.035f;
    float m_MaxDragPixels = 350.0f;
    float m_MinDragPixels = 3.0f;
    float m_MinPickRadiusPixels = 12.0f;

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
