#include "Raven/UI/Animation/UIAnimationBinding.h"

#include <algorithm>
#include <variant>

namespace Raven
{
namespace
{

bool ResolveProperty(
    const AnimationPropertyTrack& track,
    UIAnimationProperty& outProperty)
{
    const AnimationPropertyBinding& binding = GetAnimationPropertyBinding(track);

    // 現段階のUI AnimationはVec2 Propertyだけを対象にします。
    // Property名だけ一致して型が異なるTrackを受理するとApply時に暗黙変換が必要になるため、Resolve時に拒否します。
    if (std::holds_alternative<PropertyAnimationTrack<math::Vec2>>(track) == false)
    {
        return false;
    }

    if (binding.Property == "Position")
    {
        outProperty = UIAnimationProperty::Position;
        return true;
    }
    if (binding.Property == "Size")
    {
        outProperty = UIAnimationProperty::Size;
        return true;
    }
    return false;
}

} // namespace

bool UIAnimationBinding::Resolve(UIElement& root, const AnimationClip& clip)
{
    std::vector<UIResolvedAnimationBinding> resolved;
    resolved.reserve(clip.GetPropertyTrackCount());

    const auto& tracks = clip.GetPropertyTracks();
    for (std::size_t index = 0u; index < tracks.size(); ++index)
    {
        const AnimationPropertyTrack& track = tracks[index];
        const AnimationPropertyBinding& binding = GetAnimationPropertyBinding(track);
        UIElement* target = root.FindByPath(binding.TargetPath);
        if (target == nullptr)
        {
            Clear();
            return false;
        }

        UIAnimationProperty property = UIAnimationProperty::Position;
        if (ResolveProperty(track, property) == false)
        {
            Clear();
            return false;
        }

        UIResolvedAnimationBinding entry;
        entry.Target = target;
        entry.Property = property;
        entry.TrackIndex = index;
        resolved.push_back(entry);
    }

    m_Bindings = std::move(resolved);
    m_Root = &root;
    m_TreeGeneration = root.GetTreeGeneration();
    m_Resolved = true;
    return true;
}

bool UIAnimationBinding::Apply(const AnimationClip& clip, float time) const
{
    if (m_Resolved == false || m_Root == nullptr)
    {
        return false;
    }

    // UI Tree変更後は解決済みraw pointerの生存・Path対応を保証できません。
    // Targetへ触るより前にGenerationを確認し、再Resolveが必要な状態を安全に検出します。
    if (m_Root->GetTreeGeneration() != m_TreeGeneration)
    {
        return false;
    }

    const auto& tracks = clip.GetPropertyTracks();
    for (const UIResolvedAnimationBinding& binding : m_Bindings)
    {
        if (binding.Target == nullptr || binding.TrackIndex >= tracks.size())
        {
            return false;
        }

        const AnimationPropertySample sample = SampleAnimationPropertyTrack(
            tracks[binding.TrackIndex],
            std::max(time, 0.0f));
        const math::Vec2* value = std::get_if<math::Vec2>(&sample.Value);
        if (value == nullptr)
        {
            return false;
        }

        if (binding.Property == UIAnimationProperty::Position)
        {
            binding.Target->SetPosition(*value);
        }
        else if (binding.Property == UIAnimationProperty::Size)
        {
            binding.Target->SetSize(*value);
        }
    }
    return true;
}

void UIAnimationBinding::Clear()
{
    m_Bindings.clear();
    m_Root = nullptr;
    m_TreeGeneration = 0u;
    m_Resolved = false;
}

} // namespace Raven
