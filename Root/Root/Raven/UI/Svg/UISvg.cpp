#include "Raven/UI/Svg/UISvg.h"

#include "Raven/UI/Svg/SvgImporter.h"
#include "Raven/UI/Widgets/UIPanel.h"

#include <cmath>
#include <utility>

namespace Raven
{

bool UISvg::LoadFromFile(const std::string& path, std::string* outError)
{
    SvgDocument imported;
    if (SvgImporter::ImportFile(path, imported, outError) == false)
    {
        return false;
    }

    m_AnimationBinding.Clear();
    ClearChildren();
    m_Document = std::move(imported);
    m_PlaybackTime = 0.0f;
    m_Playing = false;

    return BuildRuntimeTree(outError);
}

bool UISvg::BuildRuntimeTree(std::string* outError)
{
    SetSize(m_Document.ViewportSize);

    for (const SvgRectElement& rectangle : m_Document.Rectangles)
    {
        auto panel = CreateScope<UIPanel>();
        if (panel->SetName(rectangle.Name) == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG element name is invalid or duplicated: " + rectangle.Name;
            }
            ClearChildren();
            return false;
        }

        panel->SetPosition(rectangle.Position);
        panel->SetSize(rectangle.Size);
        panel->SetBackgroundColor(rectangle.FillColor);
        if (AddChild(std::move(panel)) == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to attach SVG rectangle to UI tree.";
            }
            ClearChildren();
            return false;
        }
    }

    if (m_Document.Animation.GetPropertyTrackCount() > 0u)
    {
        if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to resolve imported SVG animation bindings.";
            }
            ClearChildren();
            return false;
        }

        if (ApplyAnimation() == false)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to apply initial SVG animation pose.";
            }
            ClearChildren();
            return false;
        }
    }

    return true;
}

void UISvg::Play()
{
    m_Playing = true;
}

void UISvg::Pause()
{
    m_Playing = false;
}

void UISvg::Stop()
{
    m_Playing = false;
    m_PlaybackTime = 0.0f;
    ApplyAnimation();
}

void UISvg::Update(float deltaTime)
{
    if (m_Playing == false || deltaTime <= 0.0f)
    {
        return;
    }

    const float duration = m_Document.Animation.GetDuration();
    if (duration <= 0.0f)
    {
        m_Playing = false;
        return;
    }

    m_PlaybackTime += deltaTime;
    if (m_Document.LoopAnimation == true)
    {
        m_PlaybackTime = std::fmod(m_PlaybackTime, duration);
    }
    else if (m_PlaybackTime >= duration)
    {
        m_PlaybackTime = duration;
        m_Playing = false;
    }

    ApplyAnimation();
}

bool UISvg::ApplyAnimation()
{
    if (m_Document.Animation.GetPropertyTrackCount() == 0u)
    {
        return true;
    }

    // LoadFromFile()時点ではUISvgがまだ親Treeへ接続されていない場合があります。
    // UIAnimationBindingはTree Generationを記録するため、その後RootへAddChild()されると
    // 正しいTarget pointerを保持していてもGeneration不一致でApply()が失敗します。
    //
    // そこでApply失敗時だけ現在のTreeに対して一度再Resolveし、Tree Mutationによる
    // Binding無効化をRuntime側で安全に吸収します。毎frame文字列Path検索は発生せず、
    // Treeが変化したframeだけ再解決するため既存UI Animationの設計意図も維持できます。
    if (m_AnimationBinding.IsResolved() == true &&
        m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime) == true)
    {
        return true;
    }

    if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false)
    {
        return false;
    }

    return m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime);
}

} // namespace Raven
