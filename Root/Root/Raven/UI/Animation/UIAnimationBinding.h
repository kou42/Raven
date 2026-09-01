#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/UI/Core/UIElement.h"

#include <cstdint>
#include <vector>

namespace Raven
{

// Generic Animationの論理BindingをUIElementへ一度だけ解決したRuntime Bindingです。
// 毎frameのAnimation適用では文字列Path検索を行わず、解決済みPointerとProperty種別だけを利用します。
enum class UIAnimationProperty
{
    Position,
    Size,
    Opacity,
    Color
};

struct UIResolvedAnimationBinding
{
    UIElement* Target = nullptr;
    UIAnimationProperty Property = UIAnimationProperty::Position;
    std::size_t TrackIndex = 0u;
};

class UIAnimationBinding
{
public:
    // Clip内の全Generic Property Trackをroot以下のUIElementへ解決します。
    // 1つでもTarget/Property/型が不正ならfalseを返し、部分的なBindingは残しません。
    bool Resolve(UIElement& root, const AnimationClip& clip);

    // Resolve済みBindingへ指定時刻の値を適用します。
    // Resolve後にUI Tree構造または論理名が変化した場合はGeneration不一致としてfalseを返し、
    // 古いraw pointerへアクセスしません。呼び出し側はResolve()をやり直してください。
    bool Apply(const AnimationClip& clip, float time) const;

    void Clear();
    bool IsResolved() const { return m_Resolved; }
    std::size_t GetBindingCount() const { return m_Bindings.size(); }

private:
    std::vector<UIResolvedAnimationBinding> m_Bindings;
    UIElement* m_Root = nullptr;
    uint64_t m_TreeGeneration = 0u;
    bool m_Resolved = false;
};

} // namespace Raven
