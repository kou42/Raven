#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Math/MathMatrix.h"

#include <unordered_map>
#include <vector>

// 現在
// Application
//    |----SceneManager
//        |----ActiveScene
//            |----LayerStack

//Application
//----SceneManager
//    |----Scene
//         |----LayerStack
//         |----GameLayer
//         |----UILayer
//         |----DebugLayer

namespace Raven
{

class SceneGame : public Scene
{
public:
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

	bool m_WasSpacePressed = false;
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

}
