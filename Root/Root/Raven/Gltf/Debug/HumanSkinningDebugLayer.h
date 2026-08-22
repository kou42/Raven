// Raven/Gltf/Debug/HumanSkinningDebugLayer.h
#pragma once

#include <string>

#include "Raven/Gltf/Debug/HumanSkinningDebugController.h"
#include "Raven/Gltf/SkinnedMeshSceneSpawner.h"
#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{

class SceneGame;

namespace Gltf
{

// ============================================================================
// HumanSkinningDebugLayer
// ============================================================================
// SceneGame本体へHuman専用処理を混ぜず、glTF Skinningの目視確認だけを独立させるLayerです。
//
// SceneGameは既にLayerをUpdate/Render/Eventへ接続しているため、このLayerを1つPushするだけで
// Human.glbの遅延読込・Bone手動操作・Entity Lifetimeをまとめて管理できます。
//
// Raven/Assets/Models/Human.glb が存在しない場合は何も生成しません。
// 既存Physics/Animation検証Sceneを壊さず、Assetを配置した瞬間からHuman検証を有効化できます。
class HumanSkinningDebugLayer final : public Layer
{
public:
    explicit HumanSkinningDebugLayer(SceneGame& scene)
        : m_Scene(scene)
    {
    }

    ~HumanSkinningDebugLayer() override;

    // Layer::OnAttach()で獲得したものではありませんが、このLayerがRuntime中に生成したHuman Entityは
    // Layerの終了境界で解放するのが責務として最も明確です。
    // DestroyHuman()は多重呼び出し可能なので、Destructor側のfallbackと重なっても安全です。
    void OnDetach() override
    {
        DestroyHuman();
    }

    void OnUpdate(float deltaTime) override;
    void OnRender() override;

private:
    bool TryInitialize();
    void DestroyHuman();

private:
    SceneGame& m_Scene;

    SkinnedMeshSceneInstance m_HumanInstance;
    HumanSkinningDebugController m_Controller;

    std::string m_ModelPath = "Raven/Assets/Models/Raven_human_test.glb";

    bool m_InitializationAttempted = false;
    bool m_Initialized = false;

    // SceneGameは現在Layer::OnUpdate()を1 frame内で2経路から呼ぶため、
    // OnRender()までに1回だけ処理する簡易guardです。
    // Human Debug固有の暫定対策で、SceneのLayer更新経路整理後には削除できます。
    bool m_UpdatedSinceRender = false;
};

} // namespace Gltf
} // namespace Raven
