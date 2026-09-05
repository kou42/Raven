#pragma once

#include "Raven/UI/Animation/UIAnimationBinding.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Svg/SvgDocument.h"

#include <string>

namespace Raven
{

// SVGをRaven UI Treeへ展開し、SVG由来AnimationClipを既存UI Animation Bindingで再生するWidgetです。
// SVG独自の毎frame補間器を持たず、AnimationClipを共有することでAnimation基盤を二重化しません。
class UISvg final : public UIElement
{
public:
    bool LoadFromFile(const std::string& path, std::string* outError = nullptr);

    void Play();
    void Pause();
    void Stop();
    void Update(float deltaTime);

    bool IsPlaying() const { return m_Playing; }
    float GetPlaybackTime() const { return m_PlaybackTime; }
    const SvgDocument& GetDocument() const { return m_Document; }

private:
    bool BuildRuntimeTree(std::string* outError);
    bool ApplyAnimation();

private:
    SvgDocument m_Document;
    UIAnimationBinding m_AnimationBinding;
    float m_PlaybackTime = 0.0f;
    bool m_Playing = false;
};

} // namespace Raven
