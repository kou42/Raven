#include "SceneGame.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathMatrix.h"

#include <cmath>
#include <vector>

namespace Raven
{

void SceneGame::OnCreate()
{
    // 広大な床パネル（XZ平面、Y=0）
    float vertices[] =
    {
        // position              // color              // uv
        -0.5f,  0.0f, -0.5f,    0.4f, 0.7f, 0.4f,    0.0f, 0.0f,
         0.5f,  0.0f, -0.5f,    0.3f, 0.6f, 0.3f,    1.0f, 0.0f,
         0.5f,  0.0f,  0.5f,    0.4f, 0.7f, 0.4f,    1.0f, 1.0f,
        -0.5f,  0.0f,  0.5f,    0.3f, 0.6f, 0.3f,    0.0f, 1.0f,
    };

    uint32_t indices[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    m_VertexArray = VertexArray::Create();

    uint32_t indexCount = sizeof(indices) / sizeof(uint32_t);
    auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));
    vertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });

    auto indexBuffer = IndexBuffer::Create(indices, indexCount);
    m_VertexArray->AddVertexBuffer(vertexBuffer);
    m_VertexArray->SetIndexBuffer(indexBuffer);

    m_Shader = m_ShaderLibrary.Load(
        "Test",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag"
    );

    m_Texture = m_TextureLibrary.Load(
        "Mountain",
        "Raven/Assets/Images/test/mountain1.png"
    );

    PipelineSpecification pipelineSpecification;
    pipelineSpecification.DebugName = "SceneGame Floor Pipeline";
    pipelineSpecification.Shader = m_Shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    Ref<Pipeline> pipeline = Pipeline::Create(pipelineSpecification);

    m_Material = CreateRef<Material>(pipeline);
    m_Mesh = CreateRef<Mesh>(m_VertexArray, static_cast<int32_t>(indexCount));

    // カメラ行列を設定
    // eye = (0, 20, 30) : カメラの位置。地面より上（Y = 20）で、少し後ろ（Z = 30）に置いています。
    // target = (0, 0, 0): カメラが見る先。原点を見下ろす構図です。
    // up = (0, 1, 0)    : カメラの「上方向」。ワールドのY軸を上として使う指定です。
    math::Vec3 eye    = { 0.0f, 20.0f, 30.0f };
    math::Vec3 target = { 0.0f,  0.0f,  0.0f };
    math::Vec3 up     = { 0.0f,  1.0f,  0.0f };

    //ワールド座標の頂点を「カメラから見た座標系」に変換する行列です。
    //要するに「世界を動かして、カメラが原点・前方固定に見える状態」にします。
    m_View       = math::Mat4::LookAt(eye, target, up);

    //fov = 0.7854 rad
    //: 視野角。約45度。値を大きくすると広角で迫力、ただし歪みが増えます。
    //aspect = 1280 / 720 ≒ 1.777...
    //: 横縦比（16 : 9）。ここが画面比とズレると、見た目が横に伸びたり縦に潰れたりします。
    //near = 0.1
    //: 手前のクリップ面。これより近いものは描画しません。
    //far = 1000.0
    //: 奥のクリップ面。これより遠いものは描画しません。
    //透視投影の本質は「遠いほど小さく見える」変換です。
	float fov = 0.7854f; // 45度 (π/4 rad)
	float aspect = 1280.0f / 720.0f; // 横縦比（16:9）
	float near = 0.1f; // 手前のクリップ面
	float far = 1000.0f; // 奥のクリップ面
    m_Projection = math::Mat4::Perspective(fov, aspect, near, far);

    // モデル座標 → ワールド座標（Model）
    // ワールド座標 → カメラ座標（View）
    // カメラ座標 → クリップ座標（Projection）
    // つまり最終的に
    //+--------------------------------------------------------------------
	// clip = Projection * View * Model * vertex_position
    //+--------------------------------------------------------------------

    m_Material->SetUniform("u_View",       m_View);
    m_Material->SetUniform("u_Projection", m_Projection);

    m_SpawnedEntities.clear();

    // 原点に広大なパネルを配置（100x100ユニット）
    Entity floor = CreateEntity("Floor");
    TransformComponent& transform = floor.GetComponent<TransformComponent>();
    transform.Position = { 0.0f, 0.0f, 0.0f };
    transform.Scale    = { 100.0f, 1.0f, 100.0f };
    floor.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_Mesh, m_Material });
    m_SpawnedEntities.push_back(floor);

    // ---- 球体メッシュの生成（UV球体, radius=0.5） ----
    {
        const int   stacks = 24;
        const int   slices = 48;
        const float radius = 0.5f;
        const float PI     = 3.14159265358979f;

        std::vector<float>    sv;
        std::vector<uint32_t> si;

        for (int i = 0; i <= stacks; ++i)
        {
            float phi = PI / 2.0f - i * PI / stacks;
            float y   = radius * sinf(phi);
            float r   = radius * cosf(phi);
            float vt  = static_cast<float>(i) / stacks;

            for (int j = 0; j <= slices; ++j)
            {
                float theta = j * 2.0f * PI / slices;
                float x     = r * cosf(theta);
                float z     = r * sinf(theta);
                float u     = static_cast<float>(j) / slices;

                // position
                sv.push_back(x);
                sv.push_back(y);
                sv.push_back(z);
                // color (青みがかった白)
                sv.push_back(0.7f + 0.3f * vt);
                sv.push_back(0.8f);
                sv.push_back(0.9f);
                // uv
                sv.push_back(u);
                sv.push_back(vt);
            }
        }

        for (int i = 0; i < stacks; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
                uint32_t b = a + static_cast<uint32_t>(slices + 1);
                si.push_back(a);     si.push_back(b);     si.push_back(a + 1);
                si.push_back(b);     si.push_back(b + 1); si.push_back(a + 1);
            }
        }

        m_SphereVertexArray = VertexArray::Create();
        auto svb = VertexBuffer::Create(sv.data(), static_cast<uint32_t>(sv.size() * sizeof(float)));
        svb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Color"    },
            { ShaderDataType::Float2, "a_Texcord"  }
        });
        auto sib = IndexBuffer::Create(si.data(), static_cast<uint32_t>(si.size()));
        m_SphereVertexArray->AddVertexBuffer(svb);
        m_SphereVertexArray->SetIndexBuffer(sib);
        m_SphereMesh = CreateRef<Mesh>(m_SphereVertexArray, static_cast<int32_t>(si.size()));
    }

    // 原点に球体を配置（床の上に乗るよう y=0.5 に配置）
    Entity sphere = CreateEntity("Sphere");
    TransformComponent& st = sphere.GetComponent<TransformComponent>();
    st.Position = { 0.0f, 0.5f, 0.0f };  // radius 分上にずらして床面と交差させない
    st.Scale    = { 1.0f, 1.0f, 1.0f };
    sphere.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_SphereMesh, m_Material });
    m_SpawnedEntities.push_back(sphere);

}

void SceneGame::OnDestroy()
{
    // ここでは、シーンのリソースを解放する処理を行います。例えば、シェーダーやテクスチャ、頂点配列などのリソースを解放する必要があります。また、レイヤーもクリアしておくと良いでしょう。
    m_layers.clear();

    for (Entity entity : m_SpawnedEntities)
    {
        DestroyEntity(entity);
    }
    m_SpawnedEntities.clear();

    m_Mesh.reset();
    m_Material.reset();
    m_VertexArray.reset();
    m_Texture.reset();
    m_Shader.reset();
    m_SphereMesh.reset();
    m_SphereVertexArray.reset();
}

void SceneGame::OnUpdate(float dt)
{
#if 0
    // プレイヤーの移動処理
    if (Input::IsKeyPressed(Key::W))
    {
        m_PlayerPosition.y += m_PlayerSpeed * dt;
    }

    // カメラの移動処理
    m_PhysicsWorld.Step(dt);

    // Entityの更新処理
    for (auto& entity : m_Entities)
    {
        entity.OnUpdate(dt);
    }

    // レイヤーの更新処理
    for (auto& layer : m_layers)
    {
        layer->OnUpdate(dt);
    }
#endif
}

void SceneGame::OnRender()
{
    RenderCommand::SetClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    RenderCommand::Clear();

    // カメラ行列をマテリアルに毎フレーム反映
    m_Material->SetUniform("u_View",       m_View);
    m_Material->SetUniform("u_Projection", m_Projection);

    Renderer::BeginScene();
    Scene::RenderEntities();
    Renderer::EndScene();

    for (auto& layer : m_layers)
    {
        layer->OnRender();
    }
#if 0
    RenderCommand::SetClearColor(
        math::Vec4{ 0.1f, 0.1f, 0.1f, 1.0f }
    );

    RenderCommand::Clear();

    for (const auto& [id, meshRenderer] : scene.GetMeshRenderers())
    {
        if (meshRenderer.IsValid() == false) {
            continue;
        }

        const auto& transform =scene.GetComponent<TransformComponent>(id);

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());
    }
#endif
}

void SceneGame::OnEvent(Event& e)
{
    for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
    {
        (*it)->OnEvent(e);

        if (e.Handled) {
            break;
        }
    }

    if (e.GetEventType() == EventType::WindowResize)
    {
        auto& resizeEvent = static_cast<WindowResizeEvent&>(e);
        RenderCommand::SetViewport(0, 0, resizeEvent.GetWidth(), resizeEvent.GetHeight());
    }
}

}
