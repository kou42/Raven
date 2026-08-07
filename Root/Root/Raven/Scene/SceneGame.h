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
    // DebugRendererにはSceneとカメラ行列への参照を渡します。
    // 行列そのものをコピーしないため、WindowResize後のProjection更新も
    // 次の描画からそのままデバッグ表示へ反映されます。
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

	ShaderLibrary m_ShaderLibrary;
	Ref<Shader> m_Shader;
	Ref<VertexArray> m_VertexArray;
	Ref<Mesh> m_Mesh;
	Ref<Material> m_Material;
	Ref<VertexArray> m_ShadowVertexArray;
	Ref<Mesh> m_ShadowMesh;
	Ref<Material> m_ShadowMaterial;

	Ref<VertexArray> m_SphereVertexArray;
	Ref<Mesh>        m_SphereMesh;

	TextureLibrary m_TextureLibrary;
	Ref<Texture>     m_Texture;

	math::Mat4 m_View;
	math::Mat4 m_Projection;

	std::vector<Entity> m_SpawnedEntities;
	std::vector<SphereBody> m_SphereBodies;
	std::unordered_map<EntityID, size_t> m_SphereBodyIndexByEntity;
	Entity m_FloorEntity;

	// ========================================================================
	// Broad Phase Debug Visualization
	// ========================================================================
	// B : Collider AABBのワイヤーフレーム表示 ON/OFF
	// P : Broad Phase候補ペア線表示 ON/OFF
	ph::PhysicsDebugRenderer m_PhysicsDebugRenderer;

	bool m_WasSpacePressed = false;
	int m_MinSphereCount = 16;
	int m_MaxSphereCount = 72;
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

}
