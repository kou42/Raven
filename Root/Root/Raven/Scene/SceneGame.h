#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Texture/Texture.h"

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

	TextureLibrary m_TextureLibrary;
	Ref<Texture>     m_Texture;

};

}