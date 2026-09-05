#pragma once

#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{

class Application;
class UISvg;

// 実SVGファイルをRaven UIへ読み込み、Runtime更新でアニメーションを再生する検証Layerです。
// SVG Runtime本体へDemo固有のAsset Pathを持ち込まないため、サンプル読込責務をDebug Layerへ分離します。
class UISvgDemoLayer final : public Layer
{
public:
    explicit UISvgDemoLayer(Application& application);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;

private:
    Application& m_Application;

    // LifetimeはUIContextのRoot Elementが所有します。
    // ApplicationはLayerをUIContextより先にDetachするため、OnDetach()で安全にTreeから除去できます。
    UISvg* m_Svg = nullptr;
};

} // namespace Raven
