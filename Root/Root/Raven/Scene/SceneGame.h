#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Math/MathMatrix.h"

#include <vector>

namespace Raven
{

class SceneGame : public Scene
{
public:
	virtual void OnCreate() override;
	virtual void OnDestroy() override;
	virtual void OnUpdate(float dt) override;
	virtual void OnRender() override;
	virtual void OnEvent(Event& e) override;

private:
	ShaderLibrary m_ShaderLibrary;
	Ref<Shader> m_Shader;
	Ref<VertexArray> m_VertexArray;
	Ref<Mesh> m_Mesh;
	Ref<Material> m_Material;

	Ref<VertexArray> m_SphereVertexArray;
	Ref<Mesh>        m_SphereMesh;

	TextureLibrary m_TextureLibrary;
	Ref<Texture>     m_Texture;

	math::Mat4 m_View;
	math::Mat4 m_Projection;

	std::vector<Entity> m_SpawnedEntities;

};

}