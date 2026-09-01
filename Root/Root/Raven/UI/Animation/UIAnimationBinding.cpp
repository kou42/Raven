#include "Raven/UI/Animation/UIAnimationBinding.h"

#include <algorithm>
#include <variant>

namespace Raven
{
namespace
{

bool ResolveProperty(const AnimationPropertyTrack& track, UIAnimationProperty& outProperty)
{
    const AnimationPropertyBinding& binding = GetAnimationPropertyBinding(track);

    if (binding.Property == "Position" || binding.Property == "Size")
    {
        if (!std::holds_alternative<PropertyAnimationTrack<math::Vec2>>(track)) return false;
        outProperty = binding.Property == "Position" ? UIAnimationProperty::Position : UIAnimationProperty::Size;
        return true;
    }

    if (binding.Property == "Opacity")
    {
        if (!std::holds_alternative<PropertyAnimationTrack<float>>(track)) return false;
        outProperty = UIAnimationProperty::Opacity;
        return true;
    }

    if (binding.Property == "Color")
    {
        if (!std::holds_alternative<PropertyAnimationTrack<math::Vec4>>(track)) return false;
        outProperty = UIAnimationProperty::Color;
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
        if (!ResolveProperty(track, property))
        {
            Clear();
            return false;
        }

        resolved.push_back(UIResolvedAnimationBinding{ target, property, index });
    }

    m_Bindings = std::move(resolved);
    m_Root = &root;
    m_TreeGeneration = root.GetTreeGeneration();
    m_Resolved = true;
    return true;
}

bool UIAnimationBinding::Apply(const AnimationClip& clip, float time) const
{
    if (!m_Resolved || m_Root == nullptr) return false;

    // UI Tree変更後はTargetへ触る前に失敗させ、古いraw pointerを使用しません。
    if (m_Root->GetTreeGeneration() != m_TreeGeneration) return false;

    const auto& tracks = clip.GetPropertyTracks();
    for (const UIResolvedAnimationBinding& binding : m_Bindings)
    {
        if (binding.Target == nullptr || binding.TrackIndex >= tracks.size()) return false;

        const AnimationPropertySample sample = SampleAnimationPropertyTrack(
            tracks[binding.TrackIndex], std::max(time, 0.0f));

        switch (binding.Property)
        {
        case UIAnimationProperty::Position:
        case UIAnimationProperty::Size:
        {
            const math::Vec2* value = std::get_if<math::Vec2>(&sample.Value);
            if (value == nullptr) return false;
            if (binding.Property == UIAnimationProperty::Position) binding.Target->SetPosition(*value);
            else binding.Target->SetSize(*value);
            break;
        }
        case UIAnimationProperty::Opacity:
        {
            const float* value = std::get_if<float>(&sample.Value);
            if (value == nullptr) return false;
            binding.Target->SetOpacity(*value);
            break;
        }
        case UIAnimationProperty::Color:
        {
            const math::Vec4* value = std::get_if<math::Vec4>(&sample.Value);
            if (value == nullptr) return false;
            binding.Target->SetTintColor(*value);
            break;
        }
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
