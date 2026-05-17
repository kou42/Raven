#include "Layer.h"
#include "../Shader/Shader.h"
#include "../Buffer/VertexArray.h"
#include "../../Core/Base.h"

namespace Raven
{

class SandboxLayer : public Layer
{

public:

    SandboxLayer();

    virtual void OnAttach() override;

    virtual void OnUpdate() override;

private:
    Ref<Shader> m_Shader;
    Ref<VertexArray> m_VertexArray;
};

}