#include "Raven/UI/Screens/LoadingScreen.h"

#include "Raven/UI/Widgets/UIPanel.h"

#include <algorithm>
#include <utility>

namespace Raven
{
namespace
{
constexpr float kProgressWidth = 320.0f;
constexpr float kProgressHeight = 10.0f;
}

LoadingScreen::LoadingScreen()
{
    UIElement& root = GetRootElement();
    root.SetName("LoadingScreen");
    root.SetLayoutMode(UILayoutMode::Absolute);
    root.SetHitTestVisible(true);

    auto panel = CreateScope<UIPanel>();
    panel->SetName("LoadingPanel");
    panel->SetPositionDIP(math::Vec2(48.0f, 96.0f));
    panel->SetPreferredSizeDIP(math::Vec2(352.0f, 104.0f));
    panel->SetLayoutMode(UILayoutMode::Vertical);
    panel->SetPaddingDIP(UIThickness(16.0f));
    panel->SetSpacingDIP(12.0f);
    panel->SetBackgroundColor(math::Vec4(0.03f, 0.03f, 0.05f, 0.98f));

    auto status = CreateScope<UIPanel>();
    status->SetName("LoadingStatus");
    status->SetPreferredSizeDIP(math::Vec2(kProgressWidth, 18.0f));
    status->SetBackgroundColor(math::Vec4(0.32f, 0.48f, 0.82f, 0.85f));
    m_StatusIndicator = status.get();
    panel->AddChild(std::move(status));

    auto track = CreateScope<UIPanel>();
    track->SetName("LoadingProgressTrack");
    track->SetPreferredSizeDIP(math::Vec2(kProgressWidth, kProgressHeight));
    track->SetLayoutMode(UILayoutMode::Absolute);
    track->SetBackgroundColor(math::Vec4(1.0f, 1.0f, 1.0f, 0.20f));

    auto fill = CreateScope<UIPanel>();
    fill->SetName("LoadingProgressFill");
    fill->SetPreferredSizeDIP(math::Vec2(0.0f, kProgressHeight));
    fill->SetBackgroundColor(math::Vec4(1.0f, 1.0f, 1.0f, 0.90f));
    m_ProgressFill = fill.get();
    track->AddChild(std::move(fill));

    panel->AddChild(std::move(track));
    AddChild(std::move(panel));
    RefreshVisualState();
}

void LoadingScreen::SetProgress(float progress)
{
    m_Progress = std::clamp(progress, 0.0f, 1.0f);
    RefreshVisualState();
}

void LoadingScreen::SetMessage(std::string message)
{
    m_Message = std::move(message);
    m_HasLoadError = false;
    RefreshVisualState();
}

void LoadingScreen::SetLoadError(std::string message)
{
    m_Message = std::move(message);
    m_HasLoadError = true;
    RefreshVisualState();
}

void LoadingScreen::RefreshVisualState()
{
    if (m_ProgressFill != nullptr)
    {
        m_ProgressFill->SetPreferredSizeDIP(
            math::Vec2(kProgressWidth * m_Progress, kProgressHeight));
    }

    if (m_StatusIndicator != nullptr)
    {
        // Font AssetがまだApplication共通Asset化されていないため、状態は色とElement名でも識別可能にします。
        // Message文字列は保持しておき、共通Font導入後にUILabelへそのまま接続できます。
        m_StatusIndicator->SetName(m_HasLoadError ? "LoadError" : "LoadingMessage");
        m_StatusIndicator->SetBackgroundColor(m_HasLoadError
            ? math::Vec4(0.72f, 0.16f, 0.16f, 0.95f)
            : math::Vec4(0.32f, 0.48f, 0.82f, 0.85f));
    }
}

} // namespace Raven
