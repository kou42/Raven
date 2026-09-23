// UIElementの幅制約付き再MeasureをGPU/Fontに依存せず検証する回帰テストです。
// 単独実行する場合はRaven UIのCore実装をリンクし、このファイルをテスト用exeの入口にしてください。
#include "Raven/UI/Core/UIContext.h"
#include "UIImmediateTests.h"
#include "Raven/UI/Rendering/UIRenderer.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UITextEditBuffer.h"
#include "Raven/UI/Text/UITextLayout.h"
#include "Raven/UI/Text/UITextMeasurement.h"
#include "Raven/UI/Widgets/UIInputNumber.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UISlider.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UIInputText.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UITooltip.h"
#include "Raven/UI/Widgets/UITreeView.h"
#include "Raven/UI/Widgets/UITable.h"
#include "Raven/UI/Widgets/UITabView.h"
#include "Raven/UI/Docking/UIDockLayout.h"
#include "Raven/UI/Docking/UIDockGeometry.h"
#include "Raven/UI/Docking/UIDockSpace.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <memory>
#include <limits>
#include <string>

namespace
{
class WrappingElement final : public Raven::UIElement
{
protected:
    Raven::math::Vec2 OnMeasureContent() const override
    {
        return Raven::math::Vec2(100.0f, 10.0f);
    }

    Raven::math::Vec2 OnMeasureContentForWidth(float availableWidth) const override
    {
        // 一文字10px、10文字の仮想テキストで、Font Atlasを用意せず幅依存の高さを再現します。
        const float width = std::max(1.0f, availableWidth);
        const float lineCount = std::ceil(100.0f / width);
        return Raven::math::Vec2(std::min(100.0f, width), lineCount * 10.0f);
    }
};

void TestDockLayout();



bool Near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.001f;
}

void CheckNear(const char* label, float actual, float expected)
{
    // Release構成でも検証が無効化されないよう、assertではなく終了コードで失敗を通知します。
    if (Near(actual, expected) == false)
    {
        std::cerr << label << ": expected " << expected << ", actual " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void Check(bool condition, const char* label)
{
    if (condition == false)
    {
        std::cerr << label << ": failed\n";
        std::exit(EXIT_FAILURE);
    }
}

void TestDPIFontBuildFailureDetails()
{
    Raven::UIFontAtlas atlas;
    Raven::UIFontAtlasBuildOptions options{};
    Raven::UIFontAtlasBuildFailure failure = Raven::UIFontAtlasBuildFailure::None;
    options.AtlasWidth = 0u;
    Check(Raven::UIFontAtlasBuilder::BuildFromFile("missing-font.ttf", { 65u },
        options, atlas, &failure) == false, "invalid atlas options rejected");
    Check(failure == Raven::UIFontAtlasBuildFailure::InvalidDPIOptions,
        "builder invalid options diagnostic");
    options.AtlasWidth = 1024u;
    Check(Raven::UIFontAtlasBuilder::BuildFromFile("missing-font.ttf", { 65u },
        options, atlas, &failure) == false, "missing font rejected before GPU work");
    Check(failure == Raven::UIFontAtlasBuildFailure::FontFileUnavailable,
        "builder missing file diagnostic");
    Raven::UIFontAtlasDPICache cache;
    float rasterScale = 1.0f;
    Check(cache.GetOrBuild("missing-font.ttf", { 65u }, options, 0.0f,
        rasterScale, &failure) == nullptr, "invalid dpi rejected");
    Check(failure == Raven::UIFontAtlasBuildFailure::InvalidDPIOptions,
        "cache invalid dpi diagnostic");
}

// Font解析とGlyph配置で失敗する場合はTexture::Createまで進まないため、GPUなしで診断を検証できます。
// RHI未初期化の検証はnative OpenGL呼び出し前に終了するためGPU Context不要です。
void TestTextureDeviceUnavailableDiagnostic()
{
    if (Raven::RenderCommand::GetDevice() != nullptr)
    {
        std::cout << "[SKIP] Texture Device diagnostic: RHI device already initialized\n";
        return;
    }
    Raven::TextureSpecification specification{};
    specification.Width = 1u;
    specification.Height = 1u;
    specification.Format = Raven::TextureFormat::RGBA8;
    specification.GenerateMips = false;
    const std::uint8_t pixels[4] = { 255u, 255u, 255u, 255u };
    Raven::TextureCreationFailure failure = Raven::TextureCreationFailure::None;
    Check(Raven::Texture::Create(specification, pixels, sizeof(pixels), &failure) == nullptr,
        "uninitialized RHI rejects diagnostic texture creation");
    Check(failure == Raven::TextureCreationFailure::DeviceUnavailable,
        "uninitialized RHI device diagnostic");
}

void TestDPIFontDataAndCapacityFailure()
{
    namespace fs = std::filesystem;
    const fs::path invalidFont = fs::temp_directory_path() /
        ("RavenInvalidFont_" + std::to_string(
            static_cast<std::uint64_t>(std::chrono::steady_clock::now()
                .time_since_epoch().count())) + ".ttf");
    {
        std::ofstream stream(invalidFont, std::ios::binary | std::ios::trunc);
        Check(stream.is_open(), "invalid font fixture created");
        stream << "not a TrueType font";
        Check(stream.good(), "invalid font fixture written");
    }
    Raven::UIFontAtlas atlas;
    Raven::UIFontAtlasBuildOptions options{};
    Raven::UIFontAtlasBuildFailure failure = Raven::UIFontAtlasBuildFailure::None;
    Check(Raven::UIFontAtlasBuilder::BuildFromFile(invalidFont.string(), { 65u },
        options, atlas, &failure) == false, "invalid font data rejected");
    Check(failure == Raven::UIFontAtlasBuildFailure::FontDataInvalid,
        "invalid font data diagnostic");
    std::error_code removeError;
    fs::remove(invalidFont, removeError);
    Check(removeError.value() == 0, "invalid font fixture removed");

    // CI/開発機のFontを使用し、存在しない環境では明示的にスキップします。
    // 1x1 AtlasにASCII 'A'を収められないため、GPU Texture生成前に容量不足が確定します。
    std::string fontPath;
#if defined(_MSC_VER)
    // MSVCではgetenvが非推奨のため、呼び出し側所有のコピーを取得して使用後に解放します。
    char* configured = nullptr;
    size_t configuredLength = 0u;
    const errno_t environmentResult = _dupenv_s(
        &configured, &configuredLength, "RAVEN_UI_TEST_FONT");
    if (environmentResult == 0 && configured != nullptr && configured[0] != '\0')
    {
        fontPath = configured;
    }
    std::free(configured);
#else
    const char* configured = std::getenv("RAVEN_UI_TEST_FONT");
    if (configured != nullptr && configured[0] != '\0')
    {
        fontPath = configured;
    }
#endif
    if (fontPath.empty() == true)
    {
        constexpr const char* candidates[] =
        {
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/meiryo.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf"
        };
        for (const char* candidate : candidates)
        {
            std::error_code existsError;
            if (fs::is_regular_file(candidate, existsError) == true)
            {
                fontPath = candidate;
                break;
            }
        }
    }
    std::error_code existsError;
    if (fontPath.empty() == true || fs::is_regular_file(fontPath, existsError) == false)
    {
        std::cout << "[SKIP] DPI Font capacity test: set RAVEN_UI_TEST_FONT to a valid TTF/TTC path\n";
        return;
    }
    options.AtlasWidth = 1u;
    options.AtlasHeight = 1u;
    options.Padding = 0u;
    Check(Raven::UIFontAtlasBuilder::BuildFromFile(fontPath, { 65u },
        options, atlas, &failure) == false, "one-pixel atlas rejects glyph A");
    Check(failure == Raven::UIFontAtlasBuildFailure::AtlasCapacityExceeded,
        "atlas capacity diagnostic before GPU creation");
}

void TestDPIFontBatchRefresh()
{
    Raven::UIContext context;
    auto cache = Raven::CreateRef<Raven::UIFontAtlasDPICache>();
    auto panel = std::make_unique<Raven::UIElement>();
    auto label = std::make_unique<Raven::UILabel>();
    Raven::UILabel* ptr = label.get();
    panel->AddChild(std::move(label));
    context.GetRootElement().AddChild(std::move(panel));
    Check(context.GetPendingDPIFontCount() == 0u, "initial pending font count");
    Raven::UIFontAtlasBuildOptions options{};
    ptr->BindDPIFontCache(cache, "missing-font.ttf", { 65u }, options);
    Check(context.GetPendingDPIFontCount() == 1u, "nested label pending count");
    Check(context.RefreshPendingDPIFonts() == 0u, "missing font batch refresh fails safely");
    Check(ptr->IsDPIFontPending(), "failed label retains pending state");
    Check(ptr->GetDPIFontFailure() == Raven::UIFontAtlasBuildFailure::FontFileUnavailable,
        "missing font failure diagnostic");
    Check(context.GetPendingDPIFontCount() == 0u, "failed batch suppresses automatic retry");
    Check(context.RefreshPendingDPIFonts() == 0u, "blocked batch does not retry");
    context.SetDPIScale(2.0f, 2.0f);
    Check(context.GetPendingDPIFontCount() == 1u, "new dpi permits a fresh attempt");
    Check(context.RefreshPendingDPIFonts() == 0u, "new dpi missing font still fails");
    Check(context.GetPendingDPIFontCount() == 0u, "new dpi failure is also blocked");
    ptr->RetryDPIFont();
    Check(ptr->GetDPIFontFailure() == Raven::UIFontAtlasBuildFailure::None,
        "explicit retry clears diagnostic");
    Check(context.GetPendingDPIFontCount() == 1u, "explicit retry schedules batch");
    ptr->SetFont(nullptr);
    Check(context.GetPendingDPIFontCount() == 0u, "legacy font clears batch pending");
    Check(context.RefreshPendingDPIFonts() == 0u, "empty batch refresh");
}

void TestDPIFontAutoRebind()
{
    Raven::UIContext context;
    auto cache = Raven::CreateRef<Raven::UIFontAtlasDPICache>();
    auto label = std::make_unique<Raven::UILabel>();
    Raven::UILabel* ptr = label.get();
    context.GetRootElement().AddChild(std::move(label));
    Raven::UIFontAtlasBuildOptions options{};
    ptr->BindDPIFontCache(cache, "missing-font.ttf", { 65u, 66u }, options);
    Check(ptr->IsDPIFontPending(), "uncached font waits for GPU context");
    Check(cache->GetEntryCount() == 0u, "dpi callback does not build atlas");
    context.SetDPIScale(2.0f, 2.0f);
    Check(ptr->IsDPIFontPending(), "dpi switch leaves uncached font pending");
    Check(cache->GetEntryCount() == 0u, "dpi switch remains GPU free");
    Check(ptr->RefreshDPIFont() == false, "missing font build fails safely");
    Check(ptr->IsDPIFontPending(), "failed build stays pending");
    Check(ptr->GetDPIFontFailure() == Raven::UIFontAtlasBuildFailure::FontFileUnavailable,
        "missing font is diagnosed");
    ptr->SetFont(nullptr);
    Check(ptr->IsDPIFontPending() == false, "legacy font clears pending binding");
    context.SetDPIScale(1.0f, 1.0f);
    Check(ptr->IsDPIFontPending() == false, "legacy font remains unbound");
}

void TestDPIAtlasLabelBinding()
{
    Raven::UILabel label;
    label.SetScaleGlyphsWithDPI(false);
    label.SetFontDPI(nullptr, 2.0f);
    Check(label.GetScaleGlyphsWithDPI(), "dpi atlas enables glyph scaling");
    label.SetScaleGlyphsWithDPI(false);
    label.SetFontDPI(nullptr, 0.0f);
    Check(label.GetScaleGlyphsWithDPI() == false, "invalid atlas raster scale rejected");
    label.SetFont(nullptr);
    Check(label.GetFont() == nullptr, "legacy atlas setter retained");
}

void TestDPIGlyphScale()
{
    Raven::UIContext context;
    auto label = std::make_unique<Raven::UILabel>();
    label->SetScaleGlyphsWithDPI(true);
    Raven::UILabel* ptr = label.get();
    context.GetRootElement().AddChild(std::move(label));
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f));
    context.EndFrame();
    Check(ptr->IsMeasureDirty() == false, "glyph scale clean after frame");
    context.SetDPIScale(1.5f, 2.0f);
    Check(ptr->IsMeasureDirty(), "glyph scale dpi invalidates measure");
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f));
    context.EndFrame();
    Check(ptr->IsMeasureDirty() == false, "glyph scale remeasured");
    context.SetUserScale(1.25f);
    Check(ptr->IsMeasureDirty(), "glyph scale user scale invalidates measure");
    ptr->SetScaleGlyphsWithDPI(false);
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f));
    context.EndFrame();
    context.SetDPIScale(2.0f, 2.0f);
    Check(ptr->IsMeasureDirty() == false, "legacy glyph scale unaffected");

    Raven::UIFontAtlas atlas;
    Raven::UITextLayoutOptions options{};
    options.GlyphScale = Raven::math::Vec2(0.0f, 1.0f);
    Check(Raven::UITextLayout::Build(atlas, "A", options).Metrics.LineCount == 0u,
        "invalid glyph scale rejected");
}

void TestDPILabelTypographyMetrics()
{
    Raven::UIContext context;
    auto label = std::make_unique<Raven::UILabel>();
    label->SetBaselineOffsetDIP(12.0f);
    label->SetLineHeightDIP(18.0f);
    Raven::UILabel* ptr = label.get();
    context.GetRootElement().AddChild(std::move(label));
    context.SetDPIScale(1.5f, 2.0f);
    CheckNear("label baseline dpi y", ptr->GetBaselineOffset(), 24.0f);
    CheckNear("label line height dpi y", ptr->GetLineHeight(), 36.0f);
    context.SetUserScale(1.25f);
    CheckNear("label baseline user scale", ptr->GetBaselineOffset(), 30.0f);
    CheckNear("label line height user scale", ptr->GetLineHeight(), 45.0f);
    ptr->SetLineHeight(21.0f);
    context.SetDPIScale(2.0f, 1.5f);
    CheckNear("label legacy line height", ptr->GetLineHeight(), 21.0f);
    CheckNear("label dip baseline updated", ptr->GetBaselineOffset(), 22.5f);
    ptr->SetBaselineOffset(7.0f);
    context.SetUserScale(2.0f);
    CheckNear("label legacy baseline", ptr->GetBaselineOffset(), 7.0f);
    CheckNear("label legacy line height after scale", ptr->GetLineHeight(), 21.0f);
}

void TestDPIAbsolutePosition()
{
    Raven::UIContext context;
    auto container = std::make_unique<Raven::UIElement>();
    container->SetPositionDIP(Raven::math::Vec2(10.0f, 20.0f));
    container->SetPreferredSizeDIP(Raven::math::Vec2(100.0f, 80.0f));
    auto child = std::make_unique<Raven::UIElement>();
    child->SetPositionDIP(Raven::math::Vec2(5.0f, 7.0f));
    child->SetPreferredSizeDIP(Raven::math::Vec2(20.0f, 10.0f));
    Raven::UIElement* childPtr = child.get();
    container->AddChild(std::move(child));
    Raven::UIElement* containerPtr = context.GetRootElement().AddChild(std::move(container));
    context.SetDPIScale(1.5f, 2.0f);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("dpi absolute parent x", containerPtr->GetPosition().x, 15.0f);
    CheckNear("dpi absolute parent y", containerPtr->GetPosition().y, 40.0f);
    CheckNear("dpi absolute child x", childPtr->GetPosition().x, 7.5f);
    CheckNear("dpi absolute child y", childPtr->GetPosition().y, 14.0f);
    context.SetUserScale(2.0f);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("rescaled parent x", containerPtr->GetPosition().x, 30.0f);
    CheckNear("rescaled child x", childPtr->GetPosition().x, 15.0f);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("no position accumulation", childPtr->GetPosition().x, 15.0f);
    childPtr->SetPosition(Raven::math::Vec2(9.0f, 11.0f));
    context.SetDPIScale(2.0f, 2.0f);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("legacy position after dpi", childPtr->GetPosition().x, 9.0f);
    CheckNear("legacy position after dpi y", childPtr->GetPosition().y, 11.0f);
    // Flow Layoutは親の配置結果が優先され、Absolute指定は再配置時まで保存します。
    containerPtr->SetLayoutMode(Raven::UILayoutMode::Vertical);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("flow overrides position", childPtr->GetPosition().x, 0.0f);
    containerPtr->SetLayoutMode(Raven::UILayoutMode::Absolute);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("absolute restores position", childPtr->GetPosition().x, 9.0f);
}

void TestDPISizeConstraints()
{
    Raven::UIContext context;
    auto element = std::make_unique<Raven::UIElement>();
    element->SetPreferredSizeDIP(Raven::math::Vec2(80.0f, 40.0f));
    element->SetMinSizeDIP(Raven::math::Vec2(90.0f, 20.0f));
    element->SetMaxSizeDIP(Raven::math::Vec2(100.0f, 30.0f));
    Raven::UIElement* ptr = context.GetRootElement().AddChild(std::move(element));
    context.SetDPIScale(1.5f, 2.0f);
    CheckNear("dpi min clamps width", ptr->GetPreferredSize().x, 135.0f);
    CheckNear("dpi max clamps height", ptr->GetPreferredSize().y, 60.0f);
    context.SetUserScale(1.25f);
    CheckNear("user scale min width", ptr->GetPreferredSize().x, 168.75f);
    CheckNear("user scale max height", ptr->GetPreferredSize().y, 75.0f);
    // Legacy setterへの切替後はDPIが変わっても、その軸の制約は固定値のままです。
    ptr->SetMinSize(Raven::math::Vec2(10.0f, 10.0f));
    ptr->SetMaxSize(Raven::math::Vec2(120.0f, 80.0f));
    context.SetDPIScale(2.0f, 2.0f);
    CheckNear("legacy max clamps dip width", ptr->GetPreferredSize().x, 120.0f);
    CheckNear("legacy max clamps dip height", ptr->GetPreferredSize().y, 80.0f);
    // Contextを離れたElementはDIP等倍で再計算されます。
    Raven::Scope<Raven::UIElement> detached = context.GetRootElement().DetachChild(ptr);
    CheckNear("detached dip width", detached->GetPreferredSize().x, 80.0f);
    CheckNear("detached dip height", detached->GetPreferredSize().y, 40.0f);
}

void TestDPILayoutMetrics()
{
    Raven::UIContext context;
    auto container = std::make_unique<Raven::UIElement>();
    container->SetLayoutMode(Raven::UILayoutMode::Vertical);
    container->SetPaddingDIP(Raven::UIThickness(4.0f));
    container->SetSpacingDIP(3.0f);
    auto child = std::make_unique<Raven::UIElement>();
    child->SetPreferredSizeDIP(Raven::math::Vec2(80.0f, 20.0f));
    child->SetMarginDIP(Raven::UIThickness(2.0f));
    Raven::UIElement* childPtr = child.get();
    container->AddChild(std::move(child));
    Raven::UIElement* containerPtr = context.GetRootElement().AddChild(std::move(container));
    context.SetDPIScale(1.5f, 2.0f);
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    CheckNear("dpi preferred width", childPtr->GetPreferredSize().x, 120.0f);
    CheckNear("dpi preferred height", childPtr->GetPreferredSize().y, 40.0f);
    CheckNear("dpi padding x", containerPtr->GetPadding().Left, 6.0f);
    CheckNear("dpi padding y", containerPtr->GetPadding().Top, 8.0f);
    CheckNear("dpi margin x", childPtr->GetMargin().Left, 3.0f);
    CheckNear("dpi margin y", childPtr->GetMargin().Top, 4.0f);
    context.SetUserScale(1.25f);
    CheckNear("user scaled width", childPtr->GetPreferredSize().x, 150.0f);
    CheckNear("user scaled height", childPtr->GetPreferredSize().y, 50.0f);
    // 同じDPI通知ではLayoutを再度Dirtyにしません。
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f));
    context.EndFrame();
    context.SetDPIScale(1.5f, 2.0f);
    Check(childPtr->IsMeasureDirty() == false, "unchanged dpi does not invalidate");
    // 従来のWindow座標setterでDIP指定を明示的に解除できます。
    childPtr->SetPreferredSize(Raven::math::Vec2(33.0f, 11.0f));
    context.SetDPIScale(2.0f, 2.0f);
    CheckNear("legacy size after dpi", childPtr->GetPreferredSize().x, 33.0f);
    CheckNear("legacy size after dpi y", childPtr->GetPreferredSize().y, 11.0f);
}

// GPU不要のRenderer Spyで、DPIとFramebuffer倍率が混同されないことを検証します。
class FramebufferSizeRenderer final : public Raven::UIRenderer
{
public:
    Raven::math::Vec2 LastViewport{};
    Raven::math::Vec2 LastFramebuffer{};
    std::size_t Calls = 0u;

    void Render(const Raven::UIDrawList&,
        const Raven::math::Vec2& viewportSize,
        const Raven::math::Vec2& framebufferSize) override
    {
        LastViewport = viewportSize;
        LastFramebuffer = framebufferSize;
        ++Calls;
    }
};

void TestWindowFramebufferMetrics()
{
    Raven::UIContext context;
    auto renderer = std::make_unique<FramebufferSizeRenderer>();
    FramebufferSizeRenderer* spy = renderer.get();
    context.SetRenderer(std::move(renderer));

    // Content ScaleとFramebuffer比率はOS/Monitorにより一致するとは限りません。
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f),
        Raven::math::Vec2(1200.0f, 900.0f), 2.0f, 1.5f);
    CheckNear("window dpi x", context.GetDPIScaleX(), 2.0f);
    CheckNear("window dpi y", context.GetDPIScaleY(), 1.5f);
    CheckNear("window layout width", context.GetLayoutViewportSize().x, 400.0f);
    CheckNear("window layout height", context.GetLayoutViewportSize().y, 400.0f);
    context.EndFrame();
    Check(spy->Calls == 1u, "window renderer called");
    CheckNear("window logical width", spy->LastViewport.x, 800.0f);
    CheckNear("window framebuffer width", spy->LastFramebuffer.x, 1200.0f);

    // 最小化中の0 PixelをWindow論理サイズで代替しないことを確認します。
    context.BeginFrame(Raven::math::Vec2(800.0f, 600.0f),
        Raven::math::Vec2(0.0f, 0.0f), 1.0f, 1.0f);
    context.EndFrame();
    CheckNear("minimized framebuffer width", spy->LastFramebuffer.x, 0.0f);
    CheckNear("minimized framebuffer height", spy->LastFramebuffer.y, 0.0f);

    // 復帰後は以前のDPI/Framebuffer値を持ち越しません。
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f),
        Raven::math::Vec2(640.0f, 480.0f), 1.0f, 1.0f);
    context.EndFrame();
    CheckNear("restored dpi", context.GetDPIScaleX(), 1.0f);
    CheckNear("restored framebuffer", spy->LastFramebuffer.x, 640.0f);

    // 補助Windowが別UIContextを所有する場合、DPI/Viewport/FramebufferがMainへ漏れません。
    Raven::UIContext auxiliary;
    auto auxiliaryRenderer = std::make_unique<FramebufferSizeRenderer>();
    FramebufferSizeRenderer* auxiliarySpy = auxiliaryRenderer.get();
    auxiliary.SetRenderer(std::move(auxiliaryRenderer));
    auxiliary.BeginFrame(Raven::math::Vec2(480.0f, 320.0f),
        Raven::math::Vec2(720.0f, 480.0f), 1.5f, 1.5f);
    auxiliary.EndFrame();
    CheckNear("auxiliary dpi", auxiliary.GetDPIScaleX(), 1.5f);
    CheckNear("auxiliary framebuffer", auxiliarySpy->LastFramebuffer.x, 720.0f);
    CheckNear("main dpi isolated", context.GetDPIScaleX(), 1.0f);
    CheckNear("main viewport isolated", context.GetViewportSize().x, 640.0f);
    CheckNear("main framebuffer isolated", spy->LastFramebuffer.x, 640.0f);

    // Root直下Widgetの所有権を補助Windowへ移し、同一ObjectをMainへ戻せます。
    auto movable = std::make_unique<Raven::UIElement>();
    Raven::UIElement* widget = movable.get();
    Check(context.GetRootElement().AddChild(std::move(movable)) == widget,
        "transfer source child added");
    Check(context.TransferRootChildTo(auxiliary, widget),
        "transfer to auxiliary");
    Check(widget->GetContext() == &auxiliary, "transfer updates context");
    Check(widget->GetParent() == &auxiliary.GetRootElement(),
        "transfer updates parent");
    Check(auxiliary.GetRootElement().GetChildren().back().get() != widget,
        "transfer keeps popup layer in front");
    Check(auxiliary.TransferRootChildTo(context, widget),
        "transfer back to main");
    Check(widget->GetContext() == &context, "transfer restores context");
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f));
    Check(context.DetachRootChild(widget) == nullptr,
        "close child extraction rejects active frame");
    context.EndFrame();
    Raven::Scope<Raven::UIElement> saved = context.DetachRootChild(widget);
    Check(saved.get() == widget && widget->GetContext() == nullptr,
        "close child extraction preserves ownership");
    Check(context.AddRootChild(std::move(saved)) == widget,
        "close child extraction restores main root");
    Check(context.GetRootElement().GetChildren().back().get() != widget,
        "close child extraction retains popup front order");
    Check(context.TransferRootChildTo(context, widget) == false,
        "transfer rejects same context");
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f));
    Check(context.TransferRootChildTo(auxiliary, widget) == false,
        "transfer rejects active frame");
    context.EndFrame();

    auxiliary.BeginFrame(Raven::math::Vec2(480.0f, 320.0f));
    Check(context.TransferRootChildTo(auxiliary, widget) == false,
        "transfer rejects destination active frame");
    auxiliary.EndFrame();

    Raven::UIContext unrelated;
    auto foreign = std::make_unique<Raven::UIElement>();
    Raven::UIElement* foreignWidget = foreign.get();
    unrelated.GetRootElement().AddChild(std::move(foreign));
    Check(context.TransferRootChildTo(auxiliary, foreignWidget) == false,
        "transfer rejects child of another context");
    Check(foreignWidget->GetContext() == &unrelated,
        "rejected transfer preserves ownership");
}

void TestDPIContextCoordinates()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(1200.0f, 800.0f));
    CheckNear("default layout viewport x", context.GetLayoutViewportSize().x, 1200.0f);
    CheckNear("default layout viewport y", context.GetLayoutViewportSize().y, 800.0f);

    // X/Y別DPIとユーザー倍率を合成しても、Window座標との往復変換が成立します。
    context.SetDPIScale(1.5f, 2.0f);
    context.SetUserScale(1.25f);
    CheckNear("effective dpi x", context.GetEffectiveScaleX(), 1.875f);
    CheckNear("effective dpi y", context.GetEffectiveScaleY(), 2.5f);
    CheckNear("layout viewport x", context.GetLayoutViewportSize().x, 640.0f);
    CheckNear("layout viewport y", context.GetLayoutViewportSize().y, 320.0f);
    const Raven::math::Vec2 window(375.0f, 250.0f);
    const Raven::math::Vec2 layout = context.WindowToLayoutPosition(window);
    CheckNear("window to layout x", layout.x, 200.0f);
    CheckNear("window to layout y", layout.y, 100.0f);
    const Raven::math::Vec2 restored = context.LayoutToWindowPosition(layout);
    CheckNear("layout to window x", restored.x, window.x);
    CheckNear("layout to window y", restored.y, window.y);
    // 既存APIの座標系を勝手に変えないことを保証します。
    CheckNear("window viewport unchanged", context.GetViewportSize().x, 1200.0f);
    context.SetDPIScale(0.0f, std::numeric_limits<float>::infinity());
    context.SetUserScale(-1.0f);
    CheckNear("invalid dpi x", context.GetEffectiveScaleX(), 1.0f);
    CheckNear("invalid dpi y", context.GetEffectiveScaleY(), 1.0f);
    context.EndFrame();
}

void TestUITheme()
{
    Raven::UIContext context;
    Raven::UIContext otherContext;
    auto button = std::make_unique<Raven::UIButton>();
    Raven::UIButton* buttonPtr = button.get();
    button->SetSize(Raven::math::Vec2(80.0f, 24.0f));
    context.GetRootElement().AddChild(std::move(button));

    auto panel = std::make_unique<Raven::UIPanel>();
    Raven::UIPanel* panelPtr = panel.get();
    panel->SetSize(Raven::math::Vec2(80.0f, 24.0f));
    context.GetRootElement().AddChild(std::move(panel));

    auto slider = std::make_unique<Raven::UISlider>();
    slider->SetSize(Raven::math::Vec2(80.0f, 24.0f));
    context.GetRootElement().AddChild(std::move(slider));

    auto input = std::make_unique<Raven::UIInputText>();
    input->SetSize(Raven::math::Vec2(80.0f, 24.0f));
    context.GetRootElement().AddChild(std::move(input));

    auto scroll = std::make_unique<Raven::UIScrollView>();
    Raven::UIScrollView* scrollPtr = scroll.get();
    scroll->SetSize(Raven::math::Vec2(80.0f, 24.0f));
    context.GetRootElement().AddChild(std::move(scroll));

    // Theme変更が既存Treeへ反映され、別Contextへ漏れないことを検証します。
    const Raven::UITheme light = Raven::UITheme::CreateDefaultLight();
    context.SetTheme(light);
    CheckNear("theme button light", buttonPtr->GetNormalColor().x, light.Button.NormalColor.x);
    CheckNear("theme panel light", panelPtr->GetBackgroundColor().x, light.Panel.BackgroundColor.x);
    CheckNear("theme scrollbar light", scrollPtr->GetScrollBarTrackColor().x, light.ScrollBar.TrackColor.x);
    CheckNear("other context dark", otherContext.GetTheme().Button.NormalColor.x, 0.24f);

    context.BeginFrame(Raven::math::Vec2(320.0f, 240.0f));
    context.EndFrame();
    const auto& commands = context.GetDrawList().GetCommands();
    Check(commands.size() >= 5u, "theme draw commands");
    CheckNear("theme button draw", commands[0u].Color.x, light.Button.NormalColor.x);
    CheckNear("theme panel draw", commands[1u].Color.x, light.Panel.BackgroundColor.x);
    CheckNear("theme slider draw", commands[2u].Color.x, light.Slider.TrackColor.x);
    CheckNear("theme input draw", commands[4u].Color.x, light.InputText.BackgroundColor.x);

    // 個別指定した状態だけThemeより優先し、未指定状態はTheme変更に追従します。
    buttonPtr->SetNormalColor(Raven::math::Vec4(0.11f, 0.22f, 0.33f, 1.0f));
    scrollPtr->SetScrollBarThumbColor(Raven::math::Vec4(0.15f, 0.25f, 0.35f, 1.0f));
    context.SetTheme(Raven::UITheme::CreateDefaultDark());
    CheckNear("theme button override", buttonPtr->GetNormalColor().x, 0.11f);
    CheckNear("theme button hover fallback", buttonPtr->GetHoveredColor().x, 0.32f);
    CheckNear("theme scrollbar override", scrollPtr->GetScrollBarThumbColor().x, 0.15f);
    CheckNear("theme scrollbar fallback", scrollPtr->GetScrollBarTrackColor().x, 0.06f);
    context.BeginFrame(Raven::math::Vec2(320.0f, 240.0f));
    context.EndFrame();
    CheckNear("theme button draw override",
        context.GetDrawList().GetCommands()[0u].Color.x, 0.11f);
}



// UTF-8のCursor/SelectionとUndo/Redoを描画・GPUなしで検証します。




// 保存失敗で旧版を壊さず、退避中のクラッシュを想定したBackup読み込みを確認します。
void TestDockSnapshotFileRecovery()
{
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() /
        ("RavenDockSnapshotTest_" + std::to_string(
            static_cast<std::uint64_t>(std::chrono::steady_clock::now()
                .time_since_epoch().count())));
    Check(fs::create_directory(directory), "dock file test directory");
    const fs::path path = directory / "layout.json";
    const fs::path backup = directory / "layout.json.bak";
    Raven::UIDockSpace dock;
    const std::uint64_t leaf = dock.GetLayout().GetRoot()->GetId();
    Check(dock.CreateTabView(leaf) != nullptr, "dock file test view");
    Check(dock.AddTab(leaf, 901u, "Before",
        std::make_unique<Raven::UIElement>()), "dock file first tab");
    const Raven::UIDockSpaceSnapshot before = dock.SaveSnapshot();
    std::string error;
    Check(Raven::SaveDockSnapshot(path.string(), before, &error),
        "dock file first save");
    Check(dock.AddTab(leaf, 902u, "After",
        std::make_unique<Raven::UIElement>()), "dock file second tab");
    const Raven::UIDockSpaceSnapshot after = dock.SaveSnapshot();
    Check(Raven::SaveDockSnapshot(path.string(), after, &error),
        "dock file replacement save");
    Raven::UIDockSpaceSnapshot loaded;
    Check(Raven::LoadDockSnapshot(path.string(), loaded, &error) &&
        loaded.Tabs.size() == 2u, "dock file newest snapshot");
    Check(Raven::LoadDockSnapshot(backup.string(), loaded, &error) &&
        loaded.Tabs.size() == 1u, "dock file previous snapshot retained");
    fs::remove(path);
    Check(Raven::LoadDockSnapshot(path.string(), loaded, &error) &&
        loaded.Tabs.size() == 1u, "dock file missing primary fallback");
    Check(Raven::SaveDockSnapshot(path.string(), after, &error),
        "dock file recovery save");
    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "{";
    }
    const auto originalCount = loaded.Tabs.size();
    Check(Raven::LoadDockSnapshot(path.string(), loaded, &error) == false,
        "dock file corrupted primary rejected");
    Check(loaded.Tabs.size() == originalCount,
        "dock file corrupted primary leaves output unchanged");
    Check(Raven::SaveDockSnapshot((directory / "missing" / "layout.json").string(),
        after, &error) == false, "dock file write failure");
    fs::remove_all(directory);
}

// Factory失敗時は復元先の既存Split比率とIDを変更しません。
void TestDockRestoreFailurePreservesStructure()
{
    Raven::UIDockSpace source;
    const std::uint64_t leaf = source.GetLayout().GetRoot()->GetId();
    Check(source.CreateTabView(leaf) != nullptr, "dock rollback source view");
    Check(source.AddTab(leaf, 702u, "Scene",
        std::make_unique<Raven::UIElement>()), "dock rollback source tab");
    const Raven::UIDockSpaceSnapshot saved = source.SaveSnapshot();

    Raven::UIDockSpace target;
    Raven::UIDockNode* newLeaf = target.Split(
        target.GetLayout().GetRoot()->GetId(),
        Raven::UIDockSplitAxis::Vertical, 0.37f);
    Check(newLeaf != nullptr, "dock rollback initial split");
    const auto before = target.GetLayout().SaveStructure();
    const auto failFactory = [](std::uint64_t, const Raven::UITabItem&)
        -> Raven::Scope<Raven::UIElement>
    {
        return nullptr;
    };
    Check(target.RestoreSnapshot(saved, failFactory) == false,
        "dock rollback factory rejected");
    const auto after = target.GetLayout().SaveStructure();
    Check(after.size() == before.size(), "dock rollback node count");
    for (std::size_t i = 0u; i < before.size(); ++i)
    {
        Check(after[i].Id == before[i].Id &&
            after[i].Ratio == before[i].Ratio &&
            after[i].Depth == before[i].Depth,
            "dock rollback structure unchanged");
    }
    Check(target.GetSplitter(before[0u].Id) != nullptr,
        "dock rollback existing splitter preserved");
}

void TestDockSnapshotJson()
{
    Raven::UIDockSpace dock;
    const std::uint64_t first = dock.GetLayout().GetRoot()->GetId();
    Raven::UIDockNode* second = dock.Split(first, Raven::UIDockSplitAxis::Vertical, 0.3f);
    if (second == nullptr)
    {
        Check(false, "dock json split");
        return;
    }
    Check(dock.CreateTabView(first) != nullptr, "dock json first view");
    Check(dock.CreateTabView(second->GetId()) != nullptr, "dock json second view");
    const std::uint64_t largeId = UINT64_MAX - 10u;
    Check(dock.AddTab(first, largeId, "Scene 日本語",
        std::make_unique<Raven::UIElement>(), false), "dock json large id");
    const Raven::UIDockSpaceSnapshot original = dock.SaveSnapshot();
    std::string json;
    std::string error;
    Check(Raven::SerializeDockSnapshot(original, json, &error), "dock json serialize");
    Raven::UIDockSpaceSnapshot decoded;
    Check(Raven::DeserializeDockSnapshot(json, decoded, &error), "dock json parse");
    Check(decoded.Tabs.size() == 1u && decoded.Tabs[0u].Tab.Id == largeId &&
        decoded.Tabs[0u].Tab.Title == "Scene 日本語" &&
        decoded.Tabs[0u].Tab.Closable == false, "dock json exact tab data");
    Raven::UIDockSpace restored;
    const auto factory = [](std::uint64_t, const Raven::UITabItem&)
        -> Raven::Scope<Raven::UIElement>
    {
        return std::make_unique<Raven::UIElement>();
    };
    Check(restored.RestoreSnapshot(decoded, factory), "dock json restore");
    Check(restored.GetTabView(first)->GetTabContent(largeId) != nullptr,
        "dock json content factory");
    const auto before = decoded;
    Check(Raven::DeserializeDockSnapshot("{", decoded, &error) == false,
        "dock json malformed rejected");
    Check(decoded.Tabs.size() == before.Tabs.size() &&
        decoded.Tabs[0u].Tab.Id == before.Tabs[0u].Tab.Id,
        "dock json parse failure atomic");
    Check(Raven::DeserializeDockSnapshot(
        "{\"type\":\"RavenDockSnapshot\",\"version\":2,"
        "\"structure\":[],\"tabs\":[],\"selections\":[]}",
        decoded, &error) == false, "dock json version rejected");
}

void TestDockFullSnapshot()
{
    Raven::UIDockSpace source;
    const std::uint64_t first = source.GetLayout().GetRoot()->GetId();
    Raven::UIDockNode* second = source.Split(first,
        Raven::UIDockSplitAxis::Horizontal, 0.4f);
    if (second == nullptr)
    {
        Check(false, "dock full snapshot split");
        return;
    }
    const std::uint64_t secondId = second->GetId();
    Check(source.CreateTabView(first) != nullptr, "dock full snapshot first view");
    Check(source.CreateTabView(secondId) != nullptr, "dock full snapshot second view");
    Check(source.AddTab(first, 91u, "Scene", std::make_unique<Raven::UIElement>(), false),
        "dock full snapshot scene");
    Check(source.AddTab(first, 92u, "Game", std::make_unique<Raven::UIElement>()),
        "dock full snapshot game");
    Check(source.AddTab(secondId, 93u, "Inspector",
        std::make_unique<Raven::UIElement>()), "dock full snapshot inspector");
    Check(source.SelectTab(first, 92u), "dock full snapshot select game");
    const Raven::UIDockSpaceSnapshot saved = source.SaveSnapshot();
    Raven::UIDockSpace restored;
    int created = 0;
    const auto factory = [&created](std::uint64_t, const Raven::UITabItem&)
        -> Raven::Scope<Raven::UIElement>
    {
        ++created;
        return std::make_unique<Raven::UIElement>();
    };
    Check(restored.RestoreSnapshot(saved, factory), "dock full snapshot restore");
    Check(created == 3, "dock full snapshot factory count");
    Check(restored.GetTabView(first)->GetModel().GetTabs()[0u].Id == 91u &&
        restored.GetTabView(first)->GetModel().GetTabs()[1u].Id == 92u,
        "dock full snapshot order");
    Check(restored.GetTabView(first)->GetModel().GetSelectedTabId() == 92u,
        "dock full snapshot selection");
    Check(restored.CloseTab(first, 91u) == false,
        "dock full snapshot fixed tab");
    Check(restored.GetTabView(secondId)->GetTabContent(93u) != nullptr,
        "dock full snapshot recreated content");
    Raven::UIDockSpace rejected;
    const auto invalidFactory = [](std::uint64_t, const Raven::UITabItem&)
        -> Raven::Scope<Raven::UIElement>
    {
        return nullptr;
    };
    Check(rejected.RestoreSnapshot(saved, invalidFactory) == false,
        "dock full snapshot factory failure");
    Check(rejected.GetLayout().GetRoot()->GetId() == 1u &&
        rejected.GetTabView(first) == nullptr,
        "dock full snapshot failure leaves empty space");
    auto duplicate = saved;
    duplicate.Tabs.push_back(duplicate.Tabs[0u]);
    Check(rejected.RestoreSnapshot(duplicate, factory) == false,
        "dock full snapshot duplicate tab rejected");
    Check(rejected.RestoreSnapshot(saved, factory),
        "dock full snapshot retry after failure");
    Check(rejected.RestoreSnapshot(saved, factory) == false,
        "dock full snapshot refuses live pane");
}

void TestDockStructureSnapshot()
{
    Raven::UIDockSpace source;
    source.SetSize(Raven::math::Vec2(500.0f, 300.0f));
    const std::uint64_t originalId = source.GetLayout().GetRoot()->GetId();
    Raven::UIDockNode* second = source.Split(originalId,
        Raven::UIDockSplitAxis::Horizontal, 0.35f);
    if (second == nullptr)
    {
        Check(false, "dock snapshot split");
        return;
    }
    const std::uint64_t secondId = second->GetId();
    Raven::UIDockNode* third = source.Split(secondId,
        Raven::UIDockSplitAxis::Vertical, 0.7f);
    if (third == nullptr)
    {
        Check(false, "dock snapshot nested split");
        return;
    }
    const auto records = source.GetLayout().SaveStructure();
    Raven::UIDockSpace restored;
    restored.SetSize(Raven::math::Vec2(500.0f, 300.0f));
    Check(restored.RestoreStructure(records), "dock snapshot restore");
    const auto roundtrip = restored.GetLayout().SaveStructure();
    Check(roundtrip.size() == records.size(), "dock snapshot count");
    for (std::size_t i = 0u; i < records.size(); ++i)
    {
        Check(roundtrip[i].Id == records[i].Id &&
            roundtrip[i].Kind == records[i].Kind &&
            roundtrip[i].Axis == records[i].Axis &&
            roundtrip[i].Ratio == records[i].Ratio &&
            roundtrip[i].Depth == records[i].Depth, "dock snapshot preorder identity");
    }
    Check(restored.GetSplitter(records[0u].Id) != nullptr,
        "dock snapshot splitter rebuilt");
    Check(restored.CreateTabView(originalId) != nullptr,
        "dock snapshot original pane binding");
    Check(restored.CreateTabView(secondId) != nullptr,
        "dock snapshot second pane binding");
    Check(restored.CreateTabView(third->GetId()) != nullptr,
        "dock snapshot third pane binding");
    Check(restored.RestoreStructure(records) == false,
        "dock snapshot refuses live panes");
    Raven::UIDockLayout layout;
    auto invalid = records;
    invalid[1u].Id = invalid[0u].Id;
    const auto before = layout.SaveStructure();
    Check(layout.RestoreStructure(invalid) == false, "dock snapshot duplicate rejected");
    Check(layout.SaveStructure()[0u].Id == before[0u].Id,
        "dock snapshot failure atomic");
    invalid = records;
    invalid[0u].Ratio = 0.0f;
    Check(layout.RestoreStructure(invalid) == false, "dock snapshot ratio rejected");
    Check(layout.RestoreStructure(records), "dock snapshot layout restored");
    Check(layout.FindNode(third->GetId()) != nullptr,
        "dock snapshot leaf ids preserved");
    Check(layout.Split(third->GetId(), Raven::UIDockSplitAxis::Horizontal) != nullptr,
        "dock snapshot next id valid");
}

// 空Paneを畳んだときSiblingのID/Contentと祖先の配置を保持します。
void TestDockCollapse()
{
    Raven::UIDockSpace dock;
    dock.SetSize(Raven::math::Vec2(600.0f, 400.0f));
    const std::uint64_t leftId = dock.GetLayout().GetRoot()->GetId();
    Check(dock.CloseEmptyPane(leftId) == false, "dock root cannot collapse");
    Raven::UIDockNode* right = dock.Split(leftId, Raven::UIDockSplitAxis::Horizontal);
    if (right == nullptr)
    {
        Check(false, "dock collapse first split");
        return;
    }
    const std::uint64_t rightId = right->GetId();
    const std::uint64_t splitId = dock.GetLayout().GetRoot()->GetId();
    Raven::UIDockNode* bottom = dock.Split(rightId, Raven::UIDockSplitAxis::Vertical);
    if (bottom == nullptr)
    {
        Check(false, "dock collapse nested split");
        return;
    }
    const std::uint64_t bottomId = bottom->GetId();
    const std::uint64_t nestedId = dock.GetLayout().FindNode(rightId)->GetParent()->GetId();
    Check(dock.CreateTabView(rightId) != nullptr, "dock collapse right view");
    Check(dock.CreateTabView(bottomId) != nullptr, "dock collapse bottom view");
    auto page = std::make_unique<Raven::UIElement>();
    Raven::UIElement* original = page.get();
    Check(dock.AddTab(rightId, 81u, "Keep", std::move(page)), "dock collapse keep tab");
    Check(dock.CloseEmptyPane(rightId) == false, "dock nonempty cannot collapse");
    Check(dock.CloseEmptyPane(bottomId), "dock collapse empty bottom");
    Check(dock.GetLayout().FindNode(bottomId) == nullptr, "dock removed leaf id");
    Check(dock.GetLayout().FindNode(nestedId) == nullptr, "dock removed split id");
    Check(dock.GetSplitter(nestedId) == nullptr, "dock removed splitter widget");
    Check(dock.GetLayout().FindNode(rightId)->GetParent()->GetId() == splitId,
        "dock promoted sibling parent");
    Check(dock.GetTabView(rightId)->GetTabContent(81u) == original,
        "dock promoted content preserved");
    Check(dock.CloseEmptyPane(leftId), "dock collapse other empty pane");
    Check(dock.GetLayout().GetRoot()->GetId() == rightId, "dock promoted root id");
    Check(dock.GetSplitter(splitId) == nullptr, "dock old root splitter removed");
    Check(dock.CloseEmptyPane(rightId) == false, "dock last leaf retained");
}

// Pane間移動ではTabのContentインスタンスとClosable設定を保持します。
void TestDockTabTransfer()
{
    Raven::UIDockSpace dock;
    dock.SetSize(Raven::math::Vec2(400.0f, 300.0f));
    const std::uint64_t firstId = dock.GetLayout().GetRoot()->GetId();
    Raven::UIDockNode* second = dock.Split(firstId, Raven::UIDockSplitAxis::Horizontal);
    if (second == nullptr)
    {
        Check(false, "dock transfer split");
        return;
    }
    const std::uint64_t secondId = second->GetId();
    Raven::UITabView* firstView = dock.CreateTabView(firstId);
    Raven::UITabView* secondView = dock.CreateTabView(secondId);
    Check(firstView != nullptr && secondView != nullptr, "dock transfer views");
    auto content = std::make_unique<Raven::UIElement>();
    Raven::UIElement* original = content.get();
    Check(dock.AddTab(firstId, 71u, "Persistent", std::move(content), false),
        "dock transfer add");
    Check(dock.MoveTabToPane(firstId, firstId, 71u) == false,
        "dock transfer same pane rejected");
    Check(dock.MoveTabToPane(firstId, secondId, 71u), "dock transfer to second");
    Check(firstView->GetTabContent(71u) == nullptr, "dock transfer source removed");
    Check(secondView->GetTabContent(71u) == original,
        "dock transfer content address stable");
    Check(dock.GetLayout().FindNode(firstId)->GetTabs()->GetTabCount() == 0u,
        "dock transfer source model");
    Check(dock.GetLayout().FindNode(secondId)->GetTabs()->GetSelectedTabId() == 71u,
        "dock transfer destination selection");
    Check(dock.CloseTab(secondId, 71u) == false,
        "dock transfer closable retained");
    Check(dock.MoveTabToPane(secondId, firstId, 71u),
        "dock transfer back");
    Check(firstView->GetTabContent(71u) == original,
        "dock transfer round trip");
    Raven::UIDrawList drawList;
    dock.BuildDrawList(drawList);
    Raven::UIDragDropPayload payload{ "Raven/UITab", "71" };
    Raven::UIDragDropEvent over;
    over.Type = Raven::UIDragDropEventType::Over;
    over.Payload = &payload;
    over.Source = firstView->GetTabBar();
    over.ScreenPosition = Raven::math::Vec2(300.0f, 100.0f);
    Check(dock.HandleDragDropEvent(over), "dock preview over accepted");
    Check(over.Accepted, "dock preview event accepted");
    over.Type = Raven::UIDragDropEventType::Drop;
    Check(dock.HandleDragDropEvent(over), "dock drop transfer");
    Check(secondView->GetTabContent(71u) == original,
        "dock drop content preserved");
}

// DockSpace経由とView上の操作の両方で論理Tab状態を同期します。
void TestDockTabView()
{
    Raven::UIDockSpace dock;
    dock.SetSize(Raven::math::Vec2(500.0f, 300.0f));
    const std::uint64_t leafId = dock.GetLayout().GetRoot()->GetId();
    Raven::UITabView* view = dock.CreateTabView(leafId);
    Check(view != nullptr, "dock tabview create");
    Check(dock.GetPane(leafId) == view, "dock tabview pane");
    Check(dock.CreateTabView(leafId) == nullptr, "dock duplicate tabview");
    Check(dock.AddTab(leafId, 11u, "Scene", std::make_unique<Raven::UIElement>()),
        "dock add scene");
    Check(dock.AddTab(leafId, 12u, "Console", std::make_unique<Raven::UIElement>()),
        "dock add console");
    Check(dock.GetLayout().GetRoot()->GetTabs()->GetTabCount() == 2u,
        "dock model tab count");
    Check(dock.SelectTab(leafId, 12u), "dock select console");
    Check(dock.GetLayout().GetRoot()->GetTabs()->GetSelectedTabId() == 12u,
        "dock model selection synced");
    Check(dock.MoveTab(leafId, 12u, 0u), "dock reorder");
    Check(dock.GetLayout().GetRoot()->GetTabs()->GetTabs()[0u].Id == 12u,
        "dock model order synced");
    Check(view->CloseTab(12u), "dock view close callback");
    Check(dock.GetLayout().GetRoot()->GetTabs()->GetTabCount() == 1u,
        "dock model close synced");
    Check(dock.GetLayout().GetRoot()->GetTabs()->GetSelectedTabId() == 11u,
        "dock model fallback selection");
    Check(dock.CloseTab(leafId, 11u), "dock close last");
    Check(dock.GetLayout().GetRoot()->GetTabs()->GetSelectedTabId() == 0u,
        "dock empty selection");
    Check(dock.AddTab(leafId, 13u, "Inspector",
        std::make_unique<Raven::UIElement>(), false), "dock nonclosable tab");
    Check(dock.CloseTab(leafId, 13u) == false, "dock nonclosable respected");
}

// Phase 9-3: Dockingの配置をUIElement/UISplitterへ反映する経路を検証します。
void TestDockSpace()
{
    Raven::UIDockSpace dock;
    dock.SetSize(Raven::math::Vec2(400.0f, 300.0f));
    const std::uint64_t leftId = dock.GetLayout().GetRoot()->GetId();
    auto left = std::make_unique<Raven::UIElement>();
    Raven::UIElement* leftRaw = left.get();
    Check(dock.SetPane(leftId, std::move(left)), "dockspace first pane");
    Check(dock.SetPane(leftId, std::make_unique<Raven::UIElement>()) == false,
        "dockspace duplicate pane");
    Raven::UIDockNode* right = dock.Split(leftId, Raven::UIDockSplitAxis::Horizontal, 0.5f);
    if (right == nullptr)
    {
        Check(false, "dockspace split");
        return;
    }
    const std::uint64_t splitId = dock.GetLayout().GetRoot()->GetId();
    Check(dock.GetSplitter(splitId) != nullptr, "dockspace splitter created");
    Check(dock.GetSplitter(splitId)->GetOrientation() == Raven::UISplitterOrientation::Vertical,
        "dockspace splitter orientation");
    Check(dock.SetPane(right->GetId(), std::make_unique<Raven::UIElement>()),
        "dockspace second pane");
    Raven::UIDrawList drawList;
    dock.BuildDrawList(drawList);
    dock.BuildDrawList(drawList);
    CheckNear("dockspace first width", leftRaw->GetSize().x, 197.5f);
    CheckNear("dockspace second x", dock.GetPane(right->GetId())->GetPosition().x, 202.5f);
    CheckNear("dockspace splitter x", dock.GetSplitter(splitId)->GetPosition().x, 197.5f);
    Check(dock.SetPane(splitId, std::make_unique<Raven::UIElement>()) == false,
        "dockspace split cannot host pane");
}

void TestDockTabWindowExtraction()
{
    Raven::UIContext context;
    auto dock = std::make_unique<Raven::UIDockSpace>();
    Raven::UIDockSpace* dockPtr = dock.get();
    context.GetRootElement().AddChild(std::move(dock));
    const std::uint64_t leafId = dockPtr->GetLayout().GetRoot()->GetId();
    Check(dockPtr->CreateTabView(leafId) != nullptr,
        "window extraction creates dock tab view");
    auto page = std::make_unique<Raven::UIElement>();
    Raven::UIElement* original = page.get();
    Check(dockPtr->AddTab(leafId, 1001u, "Inspector", std::move(page), false),
        "window extraction adds nonclosable tab");

    Raven::UITabItem metadata;
    context.BeginFrame(Raven::math::Vec2(640.0f, 480.0f));
    Check(dockPtr->ExtractTabForWindow(leafId, 1001u, metadata) == nullptr,
        "window extraction rejects active frame");
    context.EndFrame();
    Check(dockPtr->ExtractTabForWindow(leafId, 9999u, metadata) == nullptr,
        "window extraction rejects unknown tab");
    Raven::Scope<Raven::UIElement> extracted =
        dockPtr->ExtractTabForWindow(leafId, 1001u, metadata);
    Check(extracted.get() == original, "window extraction retains page identity");
    Check(extracted->GetContext() == nullptr && extracted->GetParent() == nullptr,
        "window extraction detaches page from source context");
    Check(metadata.Id == 1001u && metadata.Title == "Inspector" &&
        metadata.Closable == false, "window extraction retains tab metadata");
    Check(dockPtr->GetTabView(leafId)->GetModel().FindTab(1001u) == nullptr &&
        dockPtr->GetLayout().FindNode(leafId)->GetTabs()->FindTab(1001u) == nullptr,
        "window extraction synchronizes view and logical model");
    Check(dockPtr->AddTab(leafId, metadata.Id, metadata.Title,
        std::move(extracted), metadata.Closable), "window extraction can restore tab");
    Check(dockPtr->GetTabView(leafId)->GetTabContent(1001u) == original,
        "window extraction restore retains page identity");
}

// WindowManagerを起動しないCPU側の復帰シミュレーション。
// 複数補助WindowのClose順が変わってもContentの所有権とTabメタデータを保持します。
void TestDockTabMultipleWindowRestore()
{
    Raven::UIContext main;
    Raven::UIContext firstWindow;
    Raven::UIContext secondWindow;
    auto dock = std::make_unique<Raven::UIDockSpace>();
    Raven::UIDockSpace* dockPtr = dock.get();
    main.GetRootElement().AddChild(std::move(dock));
    const std::uint64_t leaf = dockPtr->GetLayout().GetRoot()->GetId();
    Check(dockPtr->CreateTabView(leaf) != nullptr, "multiwindow creates tab view");

    auto firstPage = std::make_unique<Raven::UIElement>();
    auto secondPage = std::make_unique<Raven::UIElement>();
    Raven::UIElement* firstRaw = firstPage.get();
    Raven::UIElement* secondRaw = secondPage.get();
    Check(dockPtr->AddTab(leaf, 2001u, "First", std::move(firstPage), false),
        "multiwindow adds first tab");
    Check(dockPtr->AddTab(leaf, 2002u, "Second", std::move(secondPage), true),
        "multiwindow adds second tab");

    Raven::UITabItem firstTab;
    Raven::UITabItem secondTab;
    Raven::Scope<Raven::UIElement> first =
        dockPtr->ExtractTabForWindow(leaf, 2001u, firstTab);
    Raven::Scope<Raven::UIElement> second =
        dockPtr->ExtractTabForWindow(leaf, 2002u, secondTab);
    Check(first.get() == firstRaw && second.get() == secondRaw,
        "multiwindow extracts both original pages");
    Check(firstWindow.AddRootChild(std::move(first)) == firstRaw,
        "multiwindow attaches first page");
    Check(secondWindow.AddRootChild(std::move(second)) == secondRaw,
        "multiwindow attaches second page");

    // 先に2番目のWindowを閉じ、続いて1番目を閉じる順序を模擬します。
    Raven::Scope<Raven::UIElement> returnedSecond =
        secondWindow.DetachRootChild(secondRaw);
    Raven::Scope<Raven::UIElement> returnedFirst =
        firstWindow.DetachRootChild(firstRaw);
    Check(returnedSecond.get() == secondRaw && returnedFirst.get() == firstRaw,
        "multiwindow close keeps both contents alive");
    Check(dockPtr->AddTab(leaf, secondTab.Id, secondTab.Title,
        std::move(returnedSecond), secondTab.Closable),
        "multiwindow restores second tab");
    Check(dockPtr->AddTab(leaf, firstTab.Id, firstTab.Title,
        std::move(returnedFirst), firstTab.Closable),
        "multiwindow restores first tab");
    Check(dockPtr->GetTabView(leaf)->GetTabContent(2001u) == firstRaw &&
        dockPtr->GetTabView(leaf)->GetTabContent(2002u) == secondRaw,
        "multiwindow restore preserves both identities");
    const Raven::UITabItem* restoredFirst =
        dockPtr->GetTabView(leaf)->GetModel().FindTab(2001u);
    const Raven::UITabItem* restoredSecond =
        dockPtr->GetTabView(leaf)->GetModel().FindTab(2002u);
    Check(restoredFirst != nullptr && restoredSecond != nullptr &&
        restoredFirst->Title == "First" && restoredFirst->Closable == false &&
        restoredSecond->Title == "Second" && restoredSecond->Closable == true,
        "multiwindow restore preserves metadata");
}

// 元DockSpaceがWindow Close前に削除された場合はMain Rootへ退避します。
void TestDockTabMissingSourceFallback()
{
    Raven::UIContext main;
    Raven::UIContext auxiliary;
    auto dock = std::make_unique<Raven::UIDockSpace>();
    Raven::UIDockSpace* dockPtr = dock.get();
    main.GetRootElement().AddChild(std::move(dock));
    const std::uint64_t leaf = dockPtr->GetLayout().GetRoot()->GetId();
    Check(dockPtr->CreateTabView(leaf) != nullptr,
        "missing source creates tab view");
    auto page = std::make_unique<Raven::UIElement>();
    Raven::UIElement* raw = page.get();
    Check(dockPtr->AddTab(leaf, 3001u, "Detached", std::move(page)),
        "missing source adds tab");
    Raven::UITabItem tab;
    Raven::Scope<Raven::UIElement> content =
        dockPtr->ExtractTabForWindow(leaf, 3001u, tab);
    Check(auxiliary.AddRootChild(std::move(content)) == raw,
        "missing source attaches auxiliary page");
    Check(main.GetRootElement().RemoveChild(dockPtr),
        "missing source removes original dock");
    Raven::Scope<Raven::UIElement> returned = auxiliary.DetachRootChild(raw);
    Check(returned.get() == raw && returned->GetContext() == nullptr,
        "missing source close retains detached content");
    returned->SetVisible(true);
    Check(main.AddRootChild(std::move(returned)) == raw,
        "missing source returns content to main root");
    Check(raw->GetContext() == &main &&
        main.GetRootElement().GetChildren().back().get() != raw,
        "missing source fallback preserves context and popup order");
}

// Phase 9-1: Docking論理Treeの所有権・安定ID・不正Split拒否を検証します。
void TestDockLayout()
{
    Raven::UIDockLayout layout;
    Raven::UIDockNode* original = layout.GetRoot();
    const std::uint64_t originalId = original->GetId();
    Check(original->GetTabs() != nullptr, "dock root tabs");
    Check(original->GetTabs()->AddTab(101u, "Scene"), "dock tab add");
    Raven::UIDockNode* right = layout.Split(originalId, Raven::UIDockSplitAxis::Horizontal, 0.35f);
    if (right == nullptr)
    {
        Check(false, "dock split");
        return;
    }
    Check(layout.GetRoot()->GetKind() == Raven::UIDockNodeKind::Split, "dock split kind");
    Check(layout.GetRoot()->GetFirst() == original, "dock leaf address stable");
    Check(layout.GetRoot()->GetSecond() == right, "dock second leaf");
    Check(original->GetParent() == layout.GetRoot(), "dock parent");
    Check(original->GetTabs()->GetSelectedTabId() == 101u, "dock selection preserved");
    CheckNear("dock split ratio", layout.GetRoot()->GetSplitRatio(), 0.35f);
    Check(layout.GetRoot()->SetSplitRatio(0.0f) == false, "dock zero ratio rejected");
    Check(layout.GetRoot()->SetSplitRatio(0.7f), "dock ratio update");
    Check(layout.Split(originalId, Raven::UIDockSplitAxis::Vertical, 0.5f, true) != nullptr,
        "dock nested split");
    Check(layout.FindNode(originalId) == original, "dock nested stable ID");
    Check(layout.FindNode(right->GetId()) == right, "dock sibling stable ID");
    Check(layout.Split(layout.GetRoot()->GetId(), Raven::UIDockSplitAxis::Horizontal) == nullptr,
        "dock split nonleaf rejected");
    Check(layout.Split(0u, Raven::UIDockSplitAxis::Horizontal) == nullptr,
        "dock unknown ID rejected");
    Check(layout.Split(right->GetId(), Raven::UIDockSplitAxis::Horizontal, 1.0f) == nullptr,
        "dock endpoint rejected");
    Check(layout.FindNode(999999u) == nullptr, "dock missing node");
    // 既存Leafは入れ子SplitのSecond側にあり、再配置してもIDは変わりません。
    const Raven::UIDockRect viewport{ 10.0f, 20.0f, 400.0f, 300.0f };
    auto placements = Raven::UIDockGeometry::Calculate(layout, viewport);
    Check(placements.size() == 5u, "dock nested placement count");
    Check(placements[0u].NodeId == layout.GetRoot()->GetId(), "dock root placement");
    CheckNear("dock root splitter x", placements[0u].Splitter.X, 10.0f + 395.0f * 0.7f);
    CheckNear("dock root splitter width", placements[0u].Splitter.Width, 5.0f);
    CheckNear("dock right pane x", placements[4u].Bounds.X, 10.0f + 395.0f * 0.7f + 5.0f);
    CheckNear("dock right pane width", placements[4u].Bounds.Width, 395.0f * 0.3f);
    Check(Raven::UIDockGeometry::Resize(*layout.GetRoot(), viewport, 39.5f),
        "dock resize horizontal");
    CheckNear("dock resize ratio", layout.GetRoot()->GetSplitRatio(), 0.8f);
    Check(Raven::UIDockGeometry::Resize(*layout.GetRoot(), viewport, 10000.0f),
        "dock resize clamp");
    CheckNear("dock resize minimum pane", layout.GetRoot()->GetSplitRatio(),
        1.0f - 32.0f / 395.0f);
    Check(Raven::UIDockGeometry::Resize(*layout.GetRoot(),
        Raven::UIDockRect{ 0.0f, 0.0f, 30.0f, 20.0f }, 1.0f) == false,
        "dock resize insufficient extent");
    Check(Raven::UIDockGeometry::Calculate(layout,
        Raven::UIDockRect{ 0.0f, 0.0f, -1.0f, 20.0f }).empty(),
        "dock negative viewport rejected");
    const auto tiny = Raven::UIDockGeometry::Calculate(layout,
        Raven::UIDockRect{ 0.0f, 0.0f, 2.0f, 2.0f });
    Check(tiny.size() == 5u, "dock tiny viewport traversal");
    for (const auto& placement : tiny)
    {
        Check(placement.Bounds.Width >= 0.0f && placement.Bounds.Height >= 0.0f,
            "dock tiny viewport nonnegative");
    }

}

void TestTextEditBuffer()
{
    Raven::UITextEditBuffer buffer;
    buffer.SetText("A\xE3\x81\x82" "B");
    Check(buffer.GetLength() == 3u, "UTF-8 codepoint length");
    buffer.MoveCursor(1u);
    buffer.MoveCursor(2u, true);
    Check(buffer.GetSelectedText() == "\xE3\x81\x82", "UTF-8 selected text");
    Check(buffer.InsertText("X"), "replace selection");
    Check(buffer.GetText() == "AXB", "replace UTF-8 selection");
    Check(buffer.Undo(), "undo replace");
    Check(buffer.GetText() == "A\xE3\x81\x82" "B", "undo restores text");
    Check(buffer.GetCursor() == 2u && buffer.GetAnchor() == 1u, "undo restores selection");
    Check(buffer.Redo(), "redo replace");
    Check(buffer.GetText() == "AXB", "redo restores text");
    Check(buffer.Backspace(), "backspace");
    Check(buffer.GetText() == "AB", "backspace text");
    Check(buffer.Undo(), "undo backspace");
    Check(buffer.GetText() == "AXB", "undo backspace text");
    Check(buffer.InsertText("!"), "insert after undo");
    Check(buffer.Redo() == false, "new edit invalidates redo");
    buffer.SetText("reset");
    Check(buffer.Undo() == false, "SetText clears history");
}

// 数値確定・範囲制限・Stepと通知を、Window/Fontなしで検証します。
void TestInputNumber()
{
    Raven::UIInputNumber number;
    int notifications = 0;
    number.SetOnValueChanged([&notifications](double)
        {
            ++notifications;
        });
    number.SetRange(-10.0, 10.0);
    number.SetValue(2.0);
    number.SetStep(0.5);
    number.Increment();
    CheckNear("step up", static_cast<float>(number.GetValue()), 2.5f);
    number.Decrement();
    CheckNear("step down", static_cast<float>(number.GetValue()), 2.0f);
    Check(notifications == 2, "step change notifications");
    number.SetValue(100.0);
    CheckNear("SetValue clamp", static_cast<float>(number.GetValue()), 10.0f);
    number.Increment();
    CheckNear("step at maximum", static_cast<float>(number.GetValue()), 10.0f);
    Check(notifications == 2, "no notification at limit");
    number.GetInputText().SetText("-4.25");
    number.Commit();
    CheckNear("commit valid value", static_cast<float>(number.GetValue()), -4.25f);
    Check(number.GetEditText() == "-4.25", "commit normalizes text");
    number.GetInputText().SetText("-");
    number.Commit();
    CheckNear("incomplete input retains value", static_cast<float>(number.GetValue()), -4.25f);
    Check(number.GetEditText() == "-4.25", "incomplete input restores text");
    number.GetInputText().SetText("999");
    number.Commit();
    CheckNear("commit clamp", static_cast<float>(number.GetValue()), 10.0f);
    Check(number.GetEditText() == "10", "commit clamp normalizes text");
    number.GetInputText().SetText("1e2");
    number.Commit();
    CheckNear("exponent clamp", static_cast<float>(number.GetValue()), 10.0f);
    number.SetStep(0.0);
    CheckNear("invalid step ignored", static_cast<float>(number.GetStep()), 0.5f);
}

Raven::UIKeyEvent Press(Raven::UIKey key, bool control = false, bool shift = false)
{
    Raven::UIKeyEvent event;
    event.Key = key;
    event.Pressed = true;
    event.Control = control;
    event.Shift = shift;
    return event;
}

// UIContextを通して実際のFocus/Keyboard/Character/Clipboard配送を検証します。
void TestInputEventRouting()
{
    Raven::UIContext context;
    auto number = std::make_unique<Raven::UIInputNumber>();
    Raven::UIInputNumber* numberPtr = number.get();
    number->SetRange(-10.0, 10.0);
    number->SetValue(2.0);
    number->SetStep(0.5);
    std::string clipboard;
    number->SetClipboard([&clipboard]() { return clipboard; },
        [&clipboard](const std::string& text) { clipboard = text; });
    context.GetRootElement().AddChild(std::move(number));

    auto other = std::make_unique<Raven::UIInputText>();
    Raven::UIInputText* otherPtr = other.get();
    other->SetText("other");
    context.GetRootElement().AddChild(std::move(other));

    Raven::UIInputText* edit = &numberPtr->GetInputText();
    Check(context.SetFocus(edit), "focus numeric input");
    Check(context.GetFocusedElement() == edit, "numeric input focused");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select all");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('-')), "type minus");
    Check(numberPtr->GetEditText() == "-", "incomplete numeric prefix");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('x')), "reject invalid character");
    Check(numberPtr->GetEditText() == "-", "invalid character unchanged");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "commit invalid number");
    Check(numberPtr->GetEditText() == "2", "Enter restores committed value");

    Check(context.RouteKeyEvent(Press(Raven::UIKey::Up)), "step up routed");
    CheckNear("routed step up", static_cast<float>(numberPtr->GetValue()), 2.5f);
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "step down routed");
    CheckNear("routed step down", static_cast<float>(numberPtr->GetValue()), 2.0f);

    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select numeric value");
    clipboard = "4.5";
    Check(context.RouteKeyEvent(Press(Raven::UIKey::V, true)), "paste numeric value");
    Check(numberPtr->GetEditText() == "4.5", "numeric clipboard paste");
    clipboard = "invalid";
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select pasted number");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::V, true)), "reject invalid paste");
    Check(numberPtr->GetEditText() == "4.5", "invalid paste is atomic");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Z, true)), "undo paste");
    Check(numberPtr->GetEditText() == "2", "undo paste restores text");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Y, true)), "redo paste");
    Check(numberPtr->GetEditText() == "4.5", "redo paste restores text");

    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select for copy");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::C, true)), "copy numeric value");
    Check(clipboard == "4.5", "clipboard copy");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select for cut");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::X, true)), "cut numeric value");
    Check(numberPtr->GetEditText().empty(), "cut clears edit text");
    Check(context.SetFocus(otherPtr), "focus other widget");
    Check(numberPtr->GetEditText() == "4.5", "Focus Lost restores incomplete edit");
    Check(context.GetFocusedElement() == otherPtr, "focus transferred");
    Check(context.SetFocus(edit), "refocus numeric input");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select before focus loss");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('-')), "type invalid prefix");
    context.ClearFocus();
    Check(context.GetFocusedElement() == nullptr, "focus cleared");
    Check(numberPtr->GetEditText() == "4.5", "ClearFocus commits number");
    Check(context.SetFocus(edit), "focus before outside click");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select before outside click");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('-')), "incomplete before outside click");
    context.RouteMouseDown(Raven::math::Vec2(-100.0f, -100.0f), Raven::UIMouseButton::Left);
    Check(context.GetFocusedElement() == nullptr, "outside click clears focus");
    Check(numberPtr->GetEditText() == "4.5", "outside click commits number");
}
// Popupの開閉・Painter順・外側入力消費・Focus/Capture破棄をGPUなしで検証します。
void TestPopupRouting()
{
    Raven::UIContext context;
    auto behind = std::make_unique<Raven::UIButton>();
    behind->SetPosition(Raven::math::Vec2(10.0f, 10.0f));
    behind->SetSize(Raven::math::Vec2(120.0f, 80.0f));
    int behindClicks = 0;
    behind->SetOnClick([&behindClicks]() { ++behindClicks; });
    Raven::UIElement* behindPtr = context.GetRootElement().AddChild(std::move(behind));

    auto popup = std::make_unique<Raven::UIElement>();
    popup->SetPosition(Raven::math::Vec2(10.0f, 10.0f));
    popup->SetSize(Raven::math::Vec2(120.0f, 80.0f));
    auto item = std::make_unique<Raven::UIButton>();
    item->SetPosition(Raven::math::Vec2(5.0f, 5.0f));
    item->SetSize(Raven::math::Vec2(60.0f, 30.0f));
    item->SetFocusable(true);
    int itemClicks = 0;
    item->SetOnClick([&itemClicks]() { ++itemClicks; });
    Raven::UIButton* itemPtr = item.get();
    popup->AddChild(std::move(item));
    Raven::UIElement* popupPtr = context.AddPopup(std::move(popup));
    if (popupPtr == nullptr)
    {
        Check(false, "popup registered");
        return;
    }
    Check(context.OpenPopup(behindPtr) == false, "reject non-popup element");
    Check(context.OpenPopup(popupPtr), "open popup");
    Check(context.GetOpenPopup() == popupPtr, "open popup identity");
    Check(context.SetFocus(itemPtr), "focus popup item");
    context.RouteMouseDown(Raven::math::Vec2(20.0f, 20.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(20.0f, 20.0f), Raven::UIMouseButton::Left);
    Check(itemClicks == 1 && behindClicks == 0, "popup is topmost");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Escape)), "Escape closes popup");
    Check(context.GetOpenPopup() == nullptr && popupPtr->IsVisible() == false, "popup hidden");
    Check(context.GetFocusedElement() == nullptr, "popup focus cleared");

    Check(context.OpenPopup(popupPtr), "reopen popup");
    Check(context.CaptureMouse(itemPtr), "capture popup item");
    Check(context.RouteMouseDown(Raven::math::Vec2(200.0f, 200.0f), Raven::UIMouseButton::Left),
        "outside Down consumed");
    Check(context.HasMouseCapture() == false, "popup capture cancelled");
    Check(context.GetOpenPopup() == nullptr, "outside Down closes popup");
    context.RouteMouseUp(Raven::math::Vec2(200.0f, 200.0f), Raven::UIMouseButton::Left);
    Check(behindClicks == 0, "outside Down not forwarded");

    // Viewport右下のAnchorでは左へClampし、下側に収まらない場合は上へ反転します。
    auto anchor = std::make_unique<Raven::UIElement>();
    anchor->SetPosition(Raven::math::Vec2(170.0f, 130.0f));
    anchor->SetSize(Raven::math::Vec2(20.0f, 20.0f));
    Raven::UIElement* anchorPtr = context.GetRootElement().AddChild(std::move(anchor));
    context.BeginFrame(Raven::math::Vec2(200.0f, 160.0f));
    Check(context.OpenPopupAt(popupPtr, anchorPtr), "anchor popup opens");
    CheckNear("anchor right clamp", popupPtr->GetPosition().x, 80.0f);
    CheckNear("anchor above flip", popupPtr->GetPosition().y, 46.0f);
    context.ClosePopup();
    Check(context.OpenPopup(popupPtr), "reopen for detach");
    Check(context.GetRootElement().GetChildren().size() >= 2u, "popup layer attached");
    // Layerの所有権をContextが保持するため、通常のRoot Childを消してもPopupは生存します。
    Check(context.GetRootElement().RemoveChild(behindPtr), "remove ordinary child");
    Check(context.GetOpenPopup() == popupPtr, "popup survives ordinary removal");
    context.ClosePopup();
    context.ClosePopup();
}
// ComboBoxの選択変更、Keyboard操作、Popup経由のMouse選択を検証します。
void TestComboBox()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto combo = std::make_unique<Raven::UIComboBox>();
    combo->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    combo->SetOptions({ "Idle", "Walk", "Run" });
    int notifications = 0;
    combo->SetOnSelectionChanged([&notifications](std::size_t, const std::string&)
        {
            ++notifications;
        });
    Raven::UIComboBox* comboPtr = combo.get();
    context.GetRootElement().AddChild(std::move(combo));
    Check(comboPtr->GetSelectedIndex() == Raven::UIComboBox::NoSelection, "combo initial selection");
    Check(comboPtr->SetSelectedIndex(1u), "combo set selection");
    Check(comboPtr->GetSelectedText() == "Walk", "combo selected text");
    Check(comboPtr->SetSelectedIndex(1u), "combo same selection");
    Check(notifications == 1, "combo unchanged selection no notification");
    Check(comboPtr->SetSelectedIndex(10u) == false, "combo invalid index");
    Check(context.SetFocus(comboPtr), "combo focus");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "combo keyboard open");
    Check(comboPtr->IsOpen(), "combo opened");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "combo next item");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "combo keyboard select");
    Check(comboPtr->GetSelectedIndex() == 2u && comboPtr->IsOpen() == false,
        "combo keyboard selection");
    Check(notifications == 2, "combo keyboard notification");

    Check(comboPtr->Open(), "combo reopen");
    context.RouteMouseDown(Raven::math::Vec2(30.0f, 58.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(30.0f, 58.0f), Raven::UIMouseButton::Left);
    Check(comboPtr->GetSelectedIndex() == 0u && comboPtr->IsOpen() == false,
        "combo mouse selects first row");
    Check(notifications == 3, "combo mouse notification");
    Check(comboPtr->Open(), "combo open for Escape");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Escape)), "combo Escape");
    Check(comboPtr->IsOpen() == false, "combo Escape closed");
    comboPtr->SetOptions({ "Only" });
    Check(comboPtr->GetSelectedIndex() == Raven::UIComboBox::NoSelection,
        "combo options reset selection");
    Check(comboPtr->SetSelectedIndex(0u), "combo new options selection");
    Check(notifications == 4, "combo new selection notification");
    Check(context.GetRootElement().RemoveChild(comboPtr), "combo removal releases popup");
}
// TooltipのHover・入力透過・Popupとの排他・対象削除を検証します。
void TestTooltip()
{
    Raven::UIContext context;
    auto button = std::make_unique<Raven::UIButton>();
    button->SetPosition(Raven::math::Vec2(170.0f, 140.0f));
    button->SetSize(Raven::math::Vec2(30.0f, 20.0f));
    int clicks = 0;
    button->SetOnClick([&clicks]() { ++clicks; });
    Raven::UIElement* target = context.GetRootElement().AddChild(std::move(button));
    Check(context.SetTooltip(target, "Tooltip", nullptr, 0.0f), "tooltip registration");
    context.BeginFrame(Raven::math::Vec2(200.0f, 170.0f));
    context.RouteMouseMove(Raven::math::Vec2(180.0f, 150.0f));
    context.EndFrame();
    const Raven::UITooltip* tip = context.GetVisibleTooltip();
    if (tip == nullptr)
    {
        Check(false, "tooltip visible on hover");
        return;
    }
    Check(tip->GetText() == "Tooltip", "tooltip visible on hover");
    Check(tip->GetPosition().x + tip->GetSize().x <= 200.0f,
        "tooltip right viewport clamp");
    Check(tip->GetPosition().y + tip->GetSize().y <= 170.0f,
        "tooltip bottom viewport clamp");
    context.RouteMouseDown(Raven::math::Vec2(180.0f, 150.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(180.0f, 150.0f), Raven::UIMouseButton::Left);
    Check(clicks == 1, "tooltip does not block button click");
    Check(context.GetVisibleTooltip() == nullptr, "tooltip hidden on click");
    context.RouteMouseMove(Raven::math::Vec2(181.0f, 150.0f));
    Check(context.GetVisibleTooltip() != nullptr, "tooltip reappears after pointer move");
    auto popup = std::make_unique<Raven::UIElement>();
    popup->SetSize(Raven::math::Vec2(40.0f, 30.0f));
    Raven::UIElement* popupPtr = context.AddPopup(std::move(popup));
    Check(context.OpenPopup(popupPtr), "popup opens over tooltip");
    Check(context.GetVisibleTooltip() == nullptr, "popup suppresses tooltip");
    context.ClosePopup();
    context.RouteMouseMove(Raven::math::Vec2(0.0f, 0.0f));
    Check(context.GetVisibleTooltip() == nullptr, "tooltip hides on hover leave");
    Check(context.GetRootElement().RemoveChild(target), "tooltip target removed");
    Check(context.ClearTooltip(target) == false, "tooltip registration cleaned on removal");
}
// Phase 4: Popupを閉じる外側Clickが背後のButtonへ伝播しないことを検証します。
void TestPopupOutsideClickConsumption()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto behind = std::make_unique<Raven::UIButton>();
    behind->SetPosition(Raven::math::Vec2(150.0f, 10.0f));
    behind->SetSize(Raven::math::Vec2(80.0f, 40.0f));
    int clicks = 0;
    behind->SetOnClick([&clicks]() { ++clicks; });
    context.GetRootElement().AddChild(std::move(behind));

    auto popup = std::make_unique<Raven::UIElement>();
    popup->SetPosition(Raven::math::Vec2(10.0f, 10.0f));
    popup->SetSize(Raven::math::Vec2(100.0f, 60.0f));
    Raven::UIElement* popupPtr = context.AddPopup(std::move(popup));
    Check(context.OpenPopup(popupPtr), "outside click popup opens");
    Check(context.RouteMouseDown(Raven::math::Vec2(160.0f, 20.0f),
        Raven::UIMouseButton::Left), "outside click Down consumed");
    Check(context.GetOpenPopup() == nullptr, "outside click closes popup");
    context.RouteMouseUp(Raven::math::Vec2(160.0f, 20.0f), Raven::UIMouseButton::Left);
    Check(clicks == 0, "outside click does not activate underlying button");

    // Popupが閉じた後の通常Clickは妨げず、入力抑制が次の操作に残らないことも確認します。
    context.RouteMouseDown(Raven::math::Vec2(160.0f, 20.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(160.0f, 20.0f), Raven::UIMouseButton::Left);
    Check(clicks == 1, "normal click works after popup closes");
}

// Phase 4: ComboBoxの空状態、Keyboard端、選択肢更新時のPopup解放を検証します。
void TestComboBoxKeyboardBoundaries()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto combo = std::make_unique<Raven::UIComboBox>();
    Raven::UIComboBox* comboPtr = combo.get();
    context.GetRootElement().AddChild(std::move(combo));
    Check(context.SetFocus(comboPtr), "empty combo focus");
    Check(comboPtr->Open() == false, "empty combo cannot open");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Space)) == false,
        "empty combo does not consume Space");

    comboPtr->SetOptions({ "Idle", "Walk", "Run" });
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Space)), "combo Space opens");
    Check(comboPtr->IsOpen(), "combo Space opened popup");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::End)), "combo End");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Space)), "combo Space selects End");
    Check(comboPtr->GetSelectedIndex() == 2u, "combo End selects last");

    Check(context.RouteKeyEvent(Press(Raven::UIKey::Up)), "combo Up opens");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "combo Down wraps");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "combo Enter selects wrapped");
    Check(comboPtr->GetSelectedIndex() == 0u, "combo Down wraps last to first");

    Check(comboPtr->Open(), "combo opens for Home");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::End)), "combo End again");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Home)), "combo Home");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "combo Home selection");
    Check(comboPtr->GetSelectedIndex() == 0u, "combo Home selects first");

    Check(comboPtr->Open(), "combo opens before options update");
    comboPtr->SetOptions({ "Only" });
    Check(comboPtr->IsOpen() == false, "combo options update closes popup");
    Check(context.GetOpenPopup() == nullptr, "combo options update releases open popup");
    Check(comboPtr->GetSelectedIndex() == Raven::UIComboBox::NoSelection,
        "combo options update clears selection");
}

// Phase 4: Tooltipの待機中・再登録・Hover離脱を実時計Sleepなしで検証します。
void TestTooltipDelayAndReset()
{
    Raven::UIContext context;
    auto button = std::make_unique<Raven::UIButton>();
    button->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    button->SetSize(Raven::math::Vec2(80.0f, 30.0f));
    Raven::UIElement* target = context.GetRootElement().AddChild(std::move(button));
    Check(context.SetTooltip(target, "Delayed", nullptr, 3600.0f),
        "delayed tooltip registration");
    context.BeginFrame(Raven::math::Vec2(300.0f, 200.0f));
    context.RouteMouseMove(Raven::math::Vec2(30.0f, 30.0f));
    context.EndFrame();
    Check(context.GetVisibleTooltip() == nullptr, "tooltip hidden before delay");

    // Delay経過をSleepで待たず、同じHover対象を即時表示設定で再登録します。
    Check(context.SetTooltip(target, "Immediate", nullptr, 0.0f),
        "tooltip delay reset registration");
    context.BeginFrame(Raven::math::Vec2(300.0f, 200.0f));
    context.EndFrame();
    const Raven::UITooltip* tooltip = context.GetVisibleTooltip();
    Check(tooltip != nullptr && tooltip->GetText() == "Immediate",
        "tooltip re-registration shows updated text");
    context.RouteMouseMove(Raven::math::Vec2(200.0f, 150.0f));
    Check(context.GetVisibleTooltip() == nullptr, "tooltip hides after leaving target");
    Check(context.ClearTooltip(target), "tooltip registration cleared");
}

// TreeViewの所有権・展開・Scroll・Keyboard/Mouse経路をFont/GPUなしで検証します。
void TestTreeView()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 48.0f));
    Raven::UITreeView* view = tree.get();
    Raven::UITreeNode* root = tree->AddRoot(1u, "Scene");
    Raven::UITreeNode* child = tree->AddNode(root, 2u, "Player");
    Raven::UITreeNode* leaf = tree->AddNode(child, 3u, "Mesh");
    Raven::UITreeNode* sibling = tree->AddRoot(4u, "Environment");
    Check(root != nullptr && child != nullptr && leaf != nullptr && sibling != nullptr, "tree nodes added");
    Check(tree->AddRoot(2u, "duplicate") == nullptr, "tree rejects duplicate id");
    Raven::UITreeNode external;
    Check(tree->AddNode(&external, 5u, "foreign") == nullptr, "tree rejects foreign parent");
    Check(tree->FindNode(3u) == leaf, "tree find nested node");
    int selections = 0;
    int expansions = 0;
    tree->SetOnSelectionChanged([&selections](std::uint64_t) { ++selections; });
    tree->SetOnExpansionChanged([&expansions](std::uint64_t, bool) { ++expansions; });
    context.GetRootElement().AddChild(std::move(tree));
    Check(view->Select(leaf), "tree select leaf");
    CheckNear("tree selected row visible", view->GetScrollOffset(), 24.0f);
    Check(view->Select(leaf), "tree select same leaf");
    Check(selections == 1, "tree unchanged selection no callback");
    Check(view->SetExpanded(root, false), "tree collapse root");
    Check(view->GetSelectedNode() == root, "tree collapse selects visible ancestor");
    CheckNear("tree collapsed range", view->GetMaxScrollOffset(), 0.0f);
    Check(expansions == 1, "tree collapse callback");
    Check(view->SetExpanded(root, true), "tree expand root");
    Check(view->SetExpanded(child, false), "tree collapse child");
    CheckNear("tree collapsed child range", view->GetMaxScrollOffset(), 24.0f);
    Check(view->SetExpanded(child, true), "tree expand child");
    CheckNear("tree expanded range", view->GetMaxScrollOffset(), 48.0f);
    view->SetScrollOffset(1000.0f);
    CheckNear("tree scroll max clamp", view->GetScrollOffset(), 48.0f);
    view->SetScrollOffset(-10.0f);
    CheckNear("tree scroll min clamp", view->GetScrollOffset(), 0.0f);
    Check(context.SetFocus(view), "tree focus");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "tree key down");
    Check(view->GetSelectedNode() == child, "tree keyboard next row");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "tree key down to leaf");
    Check(view->GetSelectedNode() == leaf, "tree keyboard leaf");
    CheckNear("tree keyboard ensure visible", view->GetScrollOffset(), 24.0f);
    Check(context.RouteMouseScroll(Raven::math::Vec2(30.0f, 30.0f),
        Raven::math::Vec2(0.0f, -1.0f)), "tree wheel scroll");
    CheckNear("tree wheel clamp", view->GetScrollOffset(), 48.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(60.0f, 32.0f),
        Raven::UIMouseButton::Left), "tree mouse selects scrolled row");
    Check(view->GetSelectedNode() == leaf, "tree scroll-aware hit test");
    context.RouteMouseUp(Raven::math::Vec2(60.0f, 32.0f), Raven::UIMouseButton::Left);
    Check(view->IsScrollBarVisible(), "tree scrollbar visible on overflow");
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 60.0f),
        Raven::UIMouseButton::Left), "tree scrollbar track click");
    CheckNear("tree track page scroll", view->GetScrollOffset(), 48.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 60.0f), Raven::UIMouseButton::Left);
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 24.0f),
        Raven::UIMouseButton::Left), "tree scrollbar thumb down");
    Check(context.HasMouseCapture(view), "tree scrollbar capture");
    context.RouteMouseMove(Raven::math::Vec2(215.0f, 60.0f));
    CheckNear("tree scrollbar drag", view->GetScrollOffset(), 48.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 60.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "tree scrollbar releases capture");
    view->SetExpanded(root, false);
    Check(view->IsScrollBarVisible() == false, "tree scrollbar hidden without overflow");
    view->Clear();
    Check(view->GetSelectedNode() == nullptr, "tree clear selection");
    Check(view->FindNode(1u) == nullptr, "tree clear nodes");
    CheckNear("tree clear scroll", view->GetScrollOffset(), 0.0f);
}
// Tableの列数検証・単一選択・Keyboard・ScrollをGPUなしで確認します。
void TestTable()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto table = std::make_unique<Raven::UITable>();
    table->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    table->SetSize(Raven::math::Vec2(200.0f, 76.0f));
    Raven::UITable* view = table.get();
    Check(table->AddColumn("Name", 100.0f), "table first column");
    Check(table->AddColumn("Type", 100.0f), "table second column");
    Check(table->AddColumn("Invalid", -1.0f) == false, "table invalid width");
    Check(table->AddRow({ "Only one" }) == false, "table rejects invalid cell count");
    Check(table->AddRow({ "A", "Mesh" }), "table first row");
    Check(table->AddRow({ "B", "Light" }), "table second row");
    Check(table->AddRow({ "C", "Camera" }), "table third row");
    Check(table->GetRows().size() == 3u, "table row count");
    int notifications = 0;
    table->SetOnSelectionChanged([&notifications](std::size_t) { ++notifications; });
    context.GetRootElement().AddChild(std::move(table));
    CheckNear("table scroll range", view->GetMaxScrollOffset(), 24.0f);
    Check(view->GetVisibleRowRange().first == 0u &&
        view->GetVisibleRowRange().second == 2u, "table initial visible row range");
    view->SetScrollOffset(24.0f);
    Check(view->GetVisibleRowRange().first == 1u &&
        view->GetVisibleRowRange().second == 3u, "table scrolled visible row range");
    view->SetScrollOffset(0.0f);
    Check(view->SelectRow(2u), "table select last");
    CheckNear("table ensure selected visible", view->GetScrollOffset(), 24.0f);
    Check(view->SelectRow(2u), "table repeat selection");
    Check(notifications == 1, "table repeat selection no callback");
    Check(view->SelectRow(3u) == false, "table invalid selection");
    Check(context.SetFocus(view), "table focus");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Home)), "table home");
    Check(view->GetSelectedIndex() == 0u, "table first selected");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::End)), "table end");
    Check(view->GetSelectedIndex() == 2u, "table last selected");
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseScroll(Raven::math::Vec2(30.0f, 60.0f),
        Raven::math::Vec2(0.0f, -1.0f)), "table wheel");
    CheckNear("table wheel clamp", view->GetScrollOffset(), 24.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(30.0f, 55.0f),
        Raven::UIMouseButton::Left), "table click scrolled row");
    Check(view->GetSelectedIndex() == 1u, "table scroll-aware hit test");
    context.RouteMouseUp(Raven::math::Vec2(30.0f, 55.0f), Raven::UIMouseButton::Left);
    Check(view->SetColumnWidth(0u, 150.0f), "table set column width");
    CheckNear("table resized width", view->GetColumns()[0u].Width, 150.0f);
    Check(view->SetColumnWidth(0u, 10.0f) == false, "table reject too narrow");
    Check(view->SetColumnWidth(2u, 100.0f) == false, "table reject missing column");
    Check(context.RouteMouseDown(Raven::math::Vec2(170.0f, 30.0f),
        Raven::UIMouseButton::Left), "table resize header down");
    Check(context.HasMouseCapture(view), "table resize capture");
    context.RouteMouseMove(Raven::math::Vec2(190.0f, 30.0f));
    CheckNear("table drag resized width", view->GetColumns()[0u].Width, 170.0f);
    context.RouteMouseUp(Raven::math::Vec2(190.0f, 30.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "table resize capture released");
    Check(view->IsScrollBarVisible(), "table scrollbar overflow visible");
    // 列幅変更で横Scrollbarも表示されるため、Body高は10px縮み、縦Scroll範囲は34pxになります。
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 88.0f),
        Raven::UIMouseButton::Left), "table scrollbar track down");
    CheckNear("table scrollbar page", view->GetScrollOffset(), 34.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 88.0f), Raven::UIMouseButton::Left);
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 52.0f),
        Raven::UIMouseButton::Left), "table scrollbar thumb down");
    Check(context.HasMouseCapture(view), "table scrollbar capture");
    context.RouteMouseMove(Raven::math::Vec2(215.0f, 80.0f));
    CheckNear("table scrollbar drag", view->GetScrollOffset(), 34.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 80.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "table scrollbar release");
    view->Clear();
    Check(view->GetColumns().empty() && view->GetRows().empty(), "table clear data");
    Check(view->GetSelectedIndex() == Raven::UITable::NoSelection, "table clear selection");
    Check(view->AddColumn("Wide", 260.0f), "table horizontal wide column");
    Check(view->AddColumn("Other", 100.0f), "table horizontal second column");
    Check(view->AddRow({ "Wide cell", "Value" }), "table horizontal first row");
    Check(view->AddRow({ "Another", "Value" }), "table horizontal second row");
    Check(view->IsHorizontalScrollBarVisible(), "table horizontal scrollbar visible");
    // 横ScrollbarでBodyが縮んで縦Scrollbarも現れ、横の実表示幅は190pxになります。
    CheckNear("table horizontal max", view->GetMaxHorizontalOffset(), 170.0f);
    view->SetHorizontalOffset(1000.0f);
    CheckNear("table horizontal clamp", view->GetHorizontalOffset(), 170.0f);
    view->SetHorizontalOffset(0.0f);
    Check(context.RouteMouseScroll(Raven::math::Vec2(30.0f, 60.0f),
        Raven::math::Vec2(-1.0f, 0.0f)), "table horizontal wheel");
    CheckNear("table horizontal wheel offset", view->GetHorizontalOffset(), 48.0f);
    view->SetHorizontalOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(180.0f, 91.0f),
        Raven::UIMouseButton::Left), "table horizontal track click");
    CheckNear("table horizontal track page", view->GetHorizontalOffset(), 170.0f);
    context.RouteMouseUp(Raven::math::Vec2(180.0f, 91.0f), Raven::UIMouseButton::Left);
    view->SetHorizontalOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(40.0f, 91.0f),
        Raven::UIMouseButton::Left), "table horizontal thumb down");
    Check(context.HasMouseCapture(view), "table horizontal capture");
    context.RouteMouseMove(Raven::math::Vec2(130.0f, 91.0f));
    Check(view->GetHorizontalOffset() > 0.0f, "table horizontal thumb drag");
    context.RouteMouseUp(Raven::math::Vec2(130.0f, 91.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "table horizontal release");
    view->Clear();
    CheckNear("table horizontal clear", view->GetHorizontalOffset(), 0.0f);
    Check(view->GetVisibleRowRange().first == 0u &&
        view->GetVisibleRowRange().second == 0u, "table empty visible range");
    // 10,000行でも可視範囲はViewport内の数行だけになります。
    Check(view->AddColumn("Index", 100.0f), "table bulk column");
    for (std::size_t i = 0u; i < 10000u; ++i)
    {
        Check(view->AddRow({ std::to_string(i) }), "table bulk row");
    }
    view->SetScrollOffset(view->GetMaxScrollOffset());
    const auto visible = view->GetVisibleRowRange();
    Check(visible.second == 10000u, "table bulk last row visible");
    Check(visible.second - visible.first <= 3u, "table bulk bounded visible rows");
    // 外部モデルは全行の文字列をTableに保持せず、表示セルだけを問い合わせます。
    std::size_t externalCount = 1000000u;
    std::size_t cellQueries = 0u;
    Check(view->SetDataSource([&externalCount]() { return externalCount; },
        [&cellQueries](std::size_t row, std::size_t column)
        {
            ++cellQueries;
            return std::to_string(row) + ":" + std::to_string(column);
        }), "table set external model");
    Check(view->GetColumns().size() == 1u, "table external keeps columns");
    Check(view->GetRows().empty(), "table external does not copy rows");
    Check(view->GetRowCount() == 1000000u, "table external count");
    Check(view->AddRow({ "invalid" }) == false, "table external rejects internal rows");
    view->SetScrollOffset(view->GetMaxScrollOffset());
    const auto externalVisible = view->GetVisibleRowRange();
    Check(externalVisible.second == externalCount, "table external last visible");
    Check(externalVisible.second - externalVisible.first <= 3u, "table external bounded visible");
    Check(cellQueries == 0u, "table external no eager cell requests");
    Check(view->SelectRow(externalCount - 1u), "table external select last");
    externalCount = 2u;
    view->NotifyDataSourceChanged();
    Check(view->GetSelectedIndex() == Raven::UITable::NoSelection,
        "table external invalid selection cleared");
    CheckNear("table external scroll clamped", view->GetScrollOffset(), 0.0f);
    view->ClearDataSource();
    Check(view->HasDataSource() == false, "table external detached");
    Check(view->GetColumns().size() == 1u, "table external detach keeps columns");
    Check(view->AddRow({ "local" }), "table local rows restored");
}
} // namespace

namespace
{
// Drag & DropのCapture・閾値・Drop先・CancelをGPUなしで検証します。
class DragProbe final : public Raven::UIElement
{
public:
    int Begins = 0;
    int Drops = 0;
    int Cancels = 0;
    int Ends = 0;
    int Ups = 0;
    bool Accept = false;
    bool RemoveSourceOnDrop = false;
    bool RemoveSelfOnOver = false;
    std::string LastData;

protected:
    void OnMouseEvent(Raven::UIMouseEvent& event) override
    {
        if (event.Type == Raven::UIMouseEventType::Up)
        {
            ++Ups;
        }
    }

    bool OnDragDropEvent(Raven::UIDragDropEvent& event) override
    {
        if (event.Type == Raven::UIDragDropEventType::Over && RemoveSelfOnOver == true)
        {
            GetParent()->RemoveChild(this);
            return false;
        }
        if (event.Type == Raven::UIDragDropEventType::Begin) { ++Begins; }
        if (event.Type == Raven::UIDragDropEventType::Cancel) { ++Cancels; }
        if (event.Type == Raven::UIDragDropEventType::End) { ++Ends; }
        if (event.Type == Raven::UIDragDropEventType::Drop)
        {
            ++Drops;
            LastData = event.Payload->Data;
            if (RemoveSourceOnDrop == true && event.Source != nullptr)
            {
                event.Source->GetParent()->RemoveChild(event.Source);
            }
        }
        return Accept && event.Type == Raven::UIDragDropEventType::Over;
    }
};

void TestTreeViewDragDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    tree->SetNodeDragDropEnabled(true);
    Raven::UITreeView* view = tree.get();
    Raven::UITreeNode* root = tree->AddRoot(1u, "Root");
    Raven::UITreeNode* child = tree->AddNode(root, 2u, "Child");
    Raven::UITreeNode* destination = tree->AddRoot(3u, "Destination");
    std::uint64_t moved = 0u;
    std::uint64_t parent = 0u;
    tree->SetOnNodeDropped([&](std::uint64_t source, std::uint64_t target)
    {
        moved = source;
        parent = target;
    });
    context.GetRootElement().AddChild(std::move(tree));
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    Check(context.HasPendingDrag(), "tree node reserves drag on down");
    Check(context.GetDragPreviewText() == "Child", "tree drag preview uses node name");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 80.0f));
    Check(context.IsDragging() && context.GetDropTarget() == view, "tree accepts sibling root");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    Check(child->Parent == destination && moved == 2u && parent == 3u, "tree reparents node on drop");
    Check(context.HasMouseCapture() == false, "tree drop releases capture");
    Check(context.GetDragPreviewText().empty(), "tree drop clears preview");
    // 親を子へDropしても循環を作らないことを検証します。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 80.0f));
    Check(context.GetDropTarget() == nullptr, "tree rejects descendant target");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    Check(destination->Parent == nullptr, "tree cycle guard keeps root");

    // ChildをDestinationの前へ移すとRoot直下へ戻ります。
    Raven::UITreeView::DropPlacement placement = Raven::UITreeView::DropPlacement::Child;
    view->SetOnNodePlaced([&](std::uint64_t, std::uint64_t, Raven::UITreeView::DropPlacement value)
    {
        placement = value;
    });
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 45.0f));
    Check(context.GetDropTarget() == view, "tree accepts before insertion");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 45.0f), Raven::UIMouseButton::Left);
    Check(child->Parent == nullptr && placement == Raven::UITreeView::DropPlacement::Before,
        "tree inserts before root");
    Check(view->FindNode(2u) == child, "tree preserves moved node identity");
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 90.0f));
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 90.0f), Raven::UIMouseButton::Left);
    Check(placement == Raven::UITreeView::DropPlacement::After &&
        child->Parent == nullptr, "tree inserts after root");
}

void TestTreeViewCrossDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(600.0f, 300.0f));
    auto left = std::make_unique<Raven::UITreeView>();
    auto right = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* sourceView = left.get();
    Raven::UITreeView* targetView = right.get();
    left->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    left->SetSize(Raven::math::Vec2(200.0f, 100.0f));
    right->SetPosition(Raven::math::Vec2(260.0f, 20.0f));
    right->SetSize(Raven::math::Vec2(200.0f, 100.0f));
    left->SetNodeDragDropEnabled(true);
    right->SetNodeDragDropEnabled(true);
    Raven::UITreeNode* moved = left->AddRoot(10u, "Moved");
    Raven::UITreeNode* nested = left->AddNode(moved, 11u, "Nested");
    Raven::UITreeNode* target = right->AddRoot(20u, "Target");
    right->AddRoot(11u, "Collision");
    context.GetRootElement().AddChild(std::move(left));
    context.GetRootElement().AddChild(std::move(right));

    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 32.0f));
    Check(context.GetDropTarget() == nullptr, "external tree drop disabled by default");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 32.0f), Raven::UIMouseButton::Left);
    targetView->SetExternalNodeDropEnabled(true);
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 32.0f));
    Check(context.GetDropTarget() == nullptr, "external subtree id collision rejected");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 32.0f), Raven::UIMouseButton::Left);

    targetView->Clear();
    target = targetView->AddRoot(20u, "Target");
    Check(sourceView->Select(nested), "external source selects nested node");
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 32.0f));
    Check(context.GetDropTarget() == targetView, "external tree accepts node");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 32.0f), Raven::UIMouseButton::Left);
    Check(sourceView->FindNode(10u) == nullptr && targetView->FindNode(10u) == moved,
        "external tree transfers node ownership");
    Check(moved->Parent == target && targetView->FindNode(11u) == nested,
        "external tree preserves subtree");
    Check(sourceView->GetSelectedNode() == nullptr, "external tree clears moved selection");
}

void TestTreeViewEmptyAreaDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(600.0f, 300.0f));
    auto left = std::make_unique<Raven::UITreeView>();
    auto right = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* sourceView = left.get();
    Raven::UITreeView* targetView = right.get();
    left->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    left->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    right->SetPosition(Raven::math::Vec2(260.0f, 20.0f));
    right->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    left->SetNodeDragDropEnabled(true);
    right->SetNodeDragDropEnabled(true);
    right->SetExternalNodeDropEnabled(true);
    Raven::UITreeNode* moved = left->AddRoot(100u, "Moved");
    Raven::UITreeNode* child = left->AddNode(moved, 101u, "Child");
    std::uint64_t targetId = 999u;
    Raven::UITreeView::DropPlacement placement = Raven::UITreeView::DropPlacement::Child;
    right->SetOnNodePlaced([&](std::uint64_t, std::uint64_t target,
        Raven::UITreeView::DropPlacement value)
    {
        targetId = target;
        placement = value;
    });
    context.GetRootElement().AddChild(std::move(left));
    context.GetRootElement().AddChild(std::move(right));

    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 60.0f));
    Check(context.GetDropTarget() == targetView, "empty tree accepts external root");
    context.EndFrame();
    // 空TreeのRootEnd線はViewport上端の外へ半分消えないよう内側に描画します。
    bool hasVisibleRootEndLine = false;
    for (const auto& command : context.GetDrawList().GetCommands())
    {
        if (command.Type == Raven::UIDrawCommandType::SolidRect &&
            std::abs(command.Color.y - 0.90f) < 0.001f &&
            std::abs(command.Rect.Min.x - 260.0f) < 0.001f &&
            command.Rect.Min.y >= 20.0f && command.Rect.Max.y <= 140.0f)
        {
            hasVisibleRootEndLine = true;
        }
    }
    Check(hasVisibleRootEndLine, "empty tree root end line stays inside viewport");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 60.0f), Raven::UIMouseButton::Left);
    Check(sourceView->FindNode(100u) == nullptr && targetView->FindNode(100u) == moved,
        "empty tree receives subtree");
    Check(targetView->FindNode(101u) == child && moved->Parent == nullptr,
        "empty tree preserves descendants");
    Check(targetId == 0u && placement == Raven::UITreeView::DropPlacement::RootEnd,
        "empty tree reports root end placement");

    // 既存Rootより下の空白も、子への移動ではなくRoot末尾への移動になります。
    Raven::UITreeNode* another = sourceView->AddRoot(102u, "Another");
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 90.0f));
    Check(context.GetDropTarget() == targetView, "tree blank area accepts root append");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 90.0f), Raven::UIMouseButton::Left);
    Check(targetView->FindNode(102u) == another && another->Parent == nullptr,
        "blank area appends another root");
}

void TestTreeViewDragAutoScroll()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 96.0f));
    tree->SetNodeDragDropEnabled(true);
    tree->SetDragAutoScrollStep(24.0f);
    tree->AddRoot(1u, "First");
    for (std::uint64_t id = 2u; id <= 12u; ++id)
    {
        tree->AddRoot(id, "Row");
    }
    context.GetRootElement().AddChild(std::move(tree));
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 106.0f));
    Check(view->GetScrollOffset() == 24.0f, "drag bottom edge scrolls down");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 106.0f));
    Check(view->GetScrollOffset() == 48.0f, "drag edge scrolls on subsequent move");
    context.TickDrag(0.1f);
    Check(view->GetScrollOffset() == 72.0f, "stationary drag scrolls by elapsed time");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 24.0f));
    Check(view->GetScrollOffset() == 48.0f, "drag top edge scrolls up");
    view->SetDragAutoScrollEnabled(false);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 106.0f));
    Check(view->GetScrollOffset() == 48.0f, "disabled drag auto scroll keeps offset");
    context.TickDrag(0.1f);
    Check(view->GetScrollOffset() == 48.0f, "disabled stationary drag keeps offset");
    context.CancelDrag();
}

void TestTreeViewNoOpDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    tree->SetNodeDragDropEnabled(true);
    Raven::UITreeNode* first = tree->AddRoot(1u, "First");
    Raven::UITreeNode* second = tree->AddRoot(2u, "Second");
    Raven::UITreeNode* last = tree->AddRoot(3u, "Last");
    int drops = 0;
    tree->SetOnNodePlaced([&](std::uint64_t, std::uint64_t, Raven::UITreeView::DropPlacement)
    {
        ++drops;
    });
    context.GetRootElement().AddChild(std::move(tree));

    // FirstをSecondの直前へDropしても既存順序は変わりません。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 45.0f));
    Check(context.GetDropTarget() == nullptr, "before adjacent node is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 45.0f), Raven::UIMouseButton::Left);
    // SecondをFirstの直後へDropしても既存順序は変わりません。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 38.0f));
    Check(context.GetDropTarget() == nullptr, "after adjacent node is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 38.0f), Raven::UIMouseButton::Left);
    // 最後のRootを空白へDropしても無変更です。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 110.0f));
    Check(context.GetDropTarget() == nullptr, "last root to root end is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 110.0f), Raven::UIMouseButton::Left);
    Check(drops == 0 && view->FindNode(1u) == first &&
        view->FindNode(2u) == second && view->FindNode(3u) == last,
        "no-op drops preserve identity without callbacks");
}

void TestTreeViewLastChildNoOpDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 150.0f));
    tree->SetNodeDragDropEnabled(true);
    Raven::UITreeNode* parent = tree->AddRoot(1u, "Parent");
    Raven::UITreeNode* first = tree->AddNode(parent, 2u, "First");
    Raven::UITreeNode* last = tree->AddNode(parent, 3u, "Last");
    int drops = 0;
    tree->SetOnNodePlaced([&](std::uint64_t, std::uint64_t, Raven::UITreeView::DropPlacement)
    {
        ++drops;
    });
    context.GetRootElement().AddChild(std::move(tree));

    // 末尾Childを親の中央へDropしても、子リスト末尾のままです。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 32.0f));
    Check(context.GetDropTarget() == nullptr, "last child to parent child position is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    Check(drops == 0 && view->FindNode(2u) == first && view->FindNode(3u) == last &&
        parent->Children.size() == 2u && parent->Children.back().get() == last,
        "last child no-op preserves child order and skips callbacks");

    // 先頭Childを親へDropする場合は実際に末尾へ移動するため受け入れます。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 32.0f));
    Check(context.GetDropTarget() == view, "first child to parent is actual reorder");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    Check(drops == 1 && parent->Children.back().get() == first,
        "first child moves to end and invokes callback");
}

void TestTreeViewDragAutoExpand()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 150.0f));
    tree->SetNodeDragDropEnabled(true);
    tree->SetDragAutoExpandDelay(0.5f);
    Raven::UITreeNode* source = tree->AddRoot(1u, "Source");
    Raven::UITreeNode* target = tree->AddRoot(2u, "Collapsed");
    Raven::UITreeNode* child = tree->AddNode(target, 3u, "Child");
    tree->SetExpanded(target, false);
    int expansions = 0;
    tree->SetOnExpansionChanged([&](std::uint64_t id, bool expanded)
    {
        if (id == 2u && expanded == true)
        {
            ++expansions;
        }
    });
    context.GetRootElement().AddChild(std::move(tree));

    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 56.0f));
    Check(context.GetDropTarget() == view && target->Expanded == false,
        "collapsed node accepts drag without immediate expansion");
    context.TickDrag(0.3f);
    Check(target->Expanded == false, "hover shorter than delay keeps node collapsed");
    context.TickDrag(0.2f);
    Check(target->Expanded == true && expansions == 1 && view->FindNode(3u) == child,
        "stationary drag expands collapsed node once");
    context.TickDrag(0.5f);
    Check(expansions == 1, "expanded node does not repeat expansion callback");
    context.CancelDrag();

    view->SetExpanded(target, false);
    view->SetDragAutoExpandEnabled(false);
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 56.0f));
    context.TickDrag(1.0f);
    Check(target->Expanded == false, "disabled drag auto expand keeps node collapsed");
    context.CancelDrag();
    Check(view->FindNode(1u) == source, "cancelled drag retains source node");
}

void TestDragDropRouting()
{
    Raven::UIContext context;
    auto source = std::make_unique<DragProbe>();
    auto target = std::make_unique<DragProbe>();
    DragProbe* sourcePtr = source.get();
    DragProbe* targetPtr = target.get();
    source->SetPosition(Raven::math::Vec2(0.0f, 0.0f));
    source->SetSize(Raven::math::Vec2(40.0f, 40.0f));
    target->SetPosition(Raven::math::Vec2(60.0f, 0.0f));
    target->SetSize(Raven::math::Vec2(40.0f, 40.0f));
    target->Accept = true;
    context.GetRootElement().AddChild(std::move(source));
    context.GetRootElement().AddChild(std::move(target));
    context.RouteMouseDown(Raven::math::Vec2(10.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(context.BeginDrag(sourcePtr, {"test/item", "payload"}, Raven::math::Vec2(10.0f, 10.0f)), "drag begins pending");
    context.RouteMouseMove(Raven::math::Vec2(12.0f, 10.0f));
    Check(sourcePtr->Begins == 0 && context.IsDragging() == false, "drag threshold");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 10.0f));
    Check(sourcePtr->Begins == 1 && context.GetDropTarget() == targetPtr, "captured drag finds target");
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    context.EndFrame();
    const auto& previewCommands = context.GetDrawList().GetCommands();
    Check(previewCommands.size() >= 2u, "drag preview adds overlay commands");
    Check(previewCommands.back().Type == Raven::UIDrawCommandType::SolidRect,
        "drag preview accent is a rectangle");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(targetPtr->Drops == 1 && targetPtr->LastData == "payload", "payload drop");
    Check(sourcePtr->Ends == 1 && sourcePtr->Ups == 0, "drop suppresses click");
    Check(context.HasMouseCapture() == false && context.HasPendingDrag() == false, "drop clears capture");

    Check(context.BeginDrag(sourcePtr, {"test/item", "cancel"}, Raven::math::Vec2(10.0f, 10.0f)), "second drag");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 10.0f));
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Escape)), "escape consumes drag");
    Check(sourcePtr->Cancels == 1 && context.HasPendingDrag() == false, "escape cancels");
    Check(context.HasMouseCapture() == false, "escape releases capture");
    Check(context.GetDragPreviewText().empty(), "cancel clears preview");
    // Drop callbackがSourceを削除してもEndで解放済みPointerへアクセスしません。
    targetPtr->RemoveSourceOnDrop = true;
    Check(context.BeginDrag(sourcePtr, {"test/item", "remove"}, Raven::math::Vec2(10.0f, 10.0f)), "remove-source drag");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 10.0f));
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(targetPtr->Drops == 2 && context.HasPendingDrag() == false, "drop removes source safely");

    // Over callbackが候補自身を削除しても、削除済み候補のParentを辿りません。
    auto disposable = std::make_unique<DragProbe>();
    DragProbe* disposablePtr = disposable.get();
    disposable->SetPosition(Raven::math::Vec2(0.0f, 50.0f));
    disposable->SetSize(Raven::math::Vec2(40.0f, 40.0f));
    disposable->RemoveSelfOnOver = true;
    context.GetRootElement().AddChild(std::move(disposable));
    Check(context.BeginDrag(targetPtr, {"test/item", "remove-target"}, Raven::math::Vec2(70.0f, 10.0f)), "remove-target drag");
    context.RouteMouseMove(Raven::math::Vec2(10.0f, 60.0f));
    Check(context.GetDropTarget() != disposablePtr, "removed candidate is not drop target");
    context.CancelDrag();
    Check(context.HasMouseCapture() == false, "remove-target cancel releases capture");
}


void TestTabSystem()
{
    Raven::UITabModel model;
    Check(model.AddTab(0u, "invalid") == false, "tab reserved id");
    Check(model.AddTab(1u, "Scene"), "tab add first");
    Check(model.AddTab(1u, "duplicate") == false, "tab duplicate");
    Check(model.AddTab(2u, "Game", false), "tab add fixed");
    Check(model.AddTab(3u, "Inspector"), "tab add third");
    Check(model.GetSelectedTabId() == 1u, "tab first auto selected");
    Check(model.CloseTab(2u) == false, "tab fixed cannot close");
    Check(model.MoveTab(1u, 2u), "tab move last");
    Check(model.GetTabs()[0u].Id == 2u && model.GetTabs()[1u].Id == 3u &&
        model.GetTabs()[2u].Id == 1u, "tab stable reorder");
    Check(model.GetSelectedTabId() == 1u, "tab selection survives move");
    Check(model.CloseTab(1u), "tab close selected");
    Check(model.GetSelectedTabId() == 3u, "tab select left neighbor");
    Check(model.RemoveTab(2u), "tab force remove fixed");
    Check(model.CloseTab(3u), "tab close final");
    Check(model.GetSelectedTabId() == 0u, "tab empty selection");

    auto view = std::make_unique<Raven::UITabView>();
    Raven::UITabView* viewPtr = view.get();
    auto scene = std::make_unique<Raven::UIElement>();
    Raven::UIElement* scenePtr = scene.get();
    auto game = std::make_unique<Raven::UIElement>();
    Raven::UIElement* gamePtr = game.get();
    Check(view->AddTab(10u, "Scene", std::move(scene)), "tab view add scene");
    Check(view->AddTab(20u, "Game", std::move(game), false), "tab view add game");
    Check(scenePtr->IsVisible() && gamePtr->IsVisible() == false,
        "tab view first content visible");
    Check(view->SelectTab(20u), "tab view select game");
    Check(scenePtr->IsVisible() == false && gamePtr->IsVisible(),
        "tab view switches content");
    Check(view->CloseTab(20u) == false, "tab view fixed close rejected");
    Check(view->RemoveTab(20u), "tab view force remove");
    Check(view->GetTabContent(20u) == nullptr && scenePtr->IsVisible(),
        "tab view removes content and restores selection");

    Raven::UIContext context;
    context.GetRootElement().AddChild(std::move(view));
    context.BeginFrame(Raven::math::Vec2(300.0f, 300.0f));
    context.EndFrame();
    Raven::UITabBar* bar = viewPtr->GetTabBar();
    bar->SetTabWidth(100.0f);
    Check(viewPtr->AddTab(30u, "Material", std::make_unique<Raven::UIElement>()),
        "tab view add material");
    Check(viewPtr->AddTab(40u, "Animation", std::make_unique<Raven::UIElement>()),
        "tab view add animation");
    Check(viewPtr->AddTab(50u, "Timeline", std::make_unique<Raven::UIElement>()),
        "tab view add timeline");
    context.BeginFrame(Raven::math::Vec2(300.0f, 300.0f));
    context.EndFrame();
    CheckNear("tab max scroll", bar->GetMaxScrollOffset(), 100.0f);
    viewPtr->SelectTab(50u);
    CheckNear("tab selected auto scroll", bar->GetScrollOffset(), 100.0f);
    bar->SetScrollOffset(1000.0f);
    CheckNear("tab scroll clamp", bar->GetScrollOffset(), 100.0f);
    bar->SetScrollOffset(0.0f);
    context.RouteMouseDown(Raven::math::Vec2(50.0f, 10.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(250.0f, 10.0f));
    context.RouteMouseUp(Raven::math::Vec2(250.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(viewPtr->GetModel().GetTabs()[2u].Id == 10u, "tab drag reorder");
    Check(viewPtr->GetModel().GetSelectedTabId() == 50u, "tab drag retains selection");
}


class TextLayoutFixtureTexture final : public Raven::Texture
{
public:
    void Bind(unsigned int slot = 0u) const override { static_cast<void>(slot); }
    void Unbind() const override {}
    void SetData(const void* data, std::size_t dataSize) override
    {
        static_cast<void>(data);
        static_cast<void>(dataSize);
    }
    unsigned int GetID() const override { return 1u; }
    int GetWidth() const override { return 64; }
    int GetHeight() const override { return 64; }
    const Raven::TextureSpecification& GetSpecification() const override { return m_Specification; }

private:
    Raven::TextureSpecification m_Specification = []()
    {
        Raven::TextureSpecification specification{};
        specification.Width = 64u;
        specification.Height = 64u;
        return specification;
    }();
};

Raven::Ref<Raven::UIFontAtlas> CreateTextLayoutFixture()
{
    // 実GPU Textureを生成せず、Atlasの寸法・Advanceだけを固定してLayoutを検証します。
    // このTextureはRendererへ渡さず、CPU上の測定とDrawList生成にだけ使用します。
    const auto texture = Raven::CreateRef<TextLayoutFixtureTexture>();
    const auto asset = Raven::CreateRef<Raven::TextureAsset>("ui-text-layout-fixture", texture);
    auto atlas = Raven::CreateRef<Raven::UIFontAtlas>();
    Check(atlas->Initialize(asset, 64u, 64u), "layout fixture atlas initialized");
    atlas->SetVerticalMetrics(8.0f, -2.0f, 0.0f);

    Raven::UIGlyphMetrics glyph{};
    glyph.AtlasRect.Min = Raven::math::Vec2(0.0f, 0.0f);
    glyph.AtlasRect.Max = Raven::math::Vec2(8.0f, 10.0f);
    glyph.Advance = 10.0f;
    for (std::uint32_t codepoint : { static_cast<std::uint32_t>('A'),
        static_cast<std::uint32_t>('B'), static_cast<std::uint32_t>('C'),
        static_cast<std::uint32_t>('D'), static_cast<std::uint32_t>('?'),
        static_cast<std::uint32_t>(0xFFFDu), static_cast<std::uint32_t>(0x3042u) })
    {
        Check(atlas->AddGlyph(codepoint, glyph), "layout fixture glyph added");
    }
    glyph.AtlasRect.Max = Raven::math::Vec2(0.0f, 0.0f);
    glyph.Advance = 5.0f;
    Check(atlas->AddGlyph(static_cast<std::uint32_t>(' '), glyph),
        "layout fixture space added");
    return atlas;
}



void TestUILabelGlyphClipTransform()
{
    const auto font = CreateTextLayoutFixture();
    Raven::UIElement root;
    root.SetLayoutMode(Raven::UILayoutMode::Vertical);
    root.SetPreferredSize(Raven::math::Vec2(60.0f, 30.0f));
    root.SetPadding(5.0f);
    root.SetClipChildren(true);
    root.SetTransformPivot(Raven::math::Vec2(0.0f, 0.0f));

    auto label = std::make_unique<Raven::UILabel>();
    label->SetFont(font);
    label->SetText("A");
    label->SetLineHeight(12.0f);
    label->SetBaselineOffset(0.0f);
    label->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    label->SetClipChildren(true);
    label->SetClipSelf(true);
    label->SetTransformPivot(Raven::math::Vec2(0.0f, 0.0f));
    Raven::UILabel* labelPtr = label.get();
    root.AddChild(std::move(label));

    Raven::UIDrawList drawList;
    root.BuildDrawList(drawList);
    Check(drawList.GetCommandCount() == 1u, "clip fixture glyph command");
    const Raven::UIDrawCommand& initial = drawList.GetCommands()[0u];
    Check(initial.Type == Raven::UIDrawCommandType::Image, "glyph uses image command");
    Check(initial.Texture == font->GetTexture(), "glyph retains atlas texture asset");
    CheckNear("glyph uv max x", initial.UVMax.x, 0.125f);
    CheckNear("glyph uv max y", initial.UVMax.y, 0.15625f);
    CheckNear("label self clip min x", initial.Clip.Rect.Min.x, 5.0f);
    CheckNear("label self clip min y", initial.Clip.Rect.Min.y, 5.0f);
    CheckNear("label self clip max x", initial.Clip.Rect.Max.x, 55.0f);
    CheckNear("label self clip max y", initial.Clip.Rect.Max.y, 17.0f);

    // GlyphのRectはLayout座標のまま保持し、World Transformとscreen-space Clipは別に伝播します。
    // 親の非一様Scaleと子のScaleを合成し、子Clipが親Clipの外へ出る部分を交差で除外します。
    root.SetScale(Raven::math::Vec2(2.0f, 2.0f));
    labelPtr->SetScale(Raven::math::Vec2(1.5f, 1.5f));
    drawList.Clear();
    root.BuildDrawList(drawList);
    Check(drawList.GetCommandCount() == 1u, "transformed glyph command count");
    const Raven::UIDrawCommand& transformed = drawList.GetCommands()[0u];
    CheckNear("glyph local rect x unchanged", transformed.Rect.Min.x, 5.0f);
    CheckNear("glyph local rect y unchanged", transformed.Rect.Min.y, 5.0f);
    CheckNear("glyph combined transform x", transformed.Transform.M00, 3.0f);
    CheckNear("glyph combined transform y", transformed.Transform.M11, 3.0f);
    CheckNear("glyph transformed x", transformed.Transform.TransformPoint(transformed.Rect.Min).x, 10.0f);
    CheckNear("glyph transformed y", transformed.Transform.TransformPoint(transformed.Rect.Min).y, 10.0f);
    Check(transformed.Clip.Enabled, "transformed glyph clip enabled");
    CheckNear("transformed glyph clip min x", transformed.Clip.Rect.Min.x, 10.0f);
    CheckNear("transformed glyph clip min y", transformed.Clip.Rect.Min.y, 10.0f);
    CheckNear("transformed glyph clip max x intersected", transformed.Clip.Rect.Max.x, 120.0f);
    CheckNear("transformed glyph clip max y", transformed.Clip.Rect.Max.y, 46.0f);

    // ClipSelfだけを無効化すると、子ElementのClipではなく親のClipがGlyphへ適用されます。
    labelPtr->SetClipSelf(false);
    drawList.Clear();
    root.BuildDrawList(drawList);
    CheckNear("ancestor only clip min x", drawList.GetCommands()[0u].Clip.Rect.Min.x, 0.0f);
    CheckNear("ancestor only clip min y", drawList.GetCommands()[0u].Clip.Rect.Min.y, 0.0f);
    CheckNear("ancestor only clip max x", drawList.GetCommands()[0u].Clip.Rect.Max.x, 120.0f);
    CheckNear("ancestor only clip max y", drawList.GetCommands()[0u].Clip.Rect.Max.y, 60.0f);
}

void TestUILabelTextReflow()
{
    const auto font = CreateTextLayoutFixture();
    Raven::UIElement root;
    root.SetLayoutMode(Raven::UILayoutMode::Vertical);
    root.SetPreferredSize(Raven::math::Vec2(60.0f, 0.0f));
    root.SetPadding(5.0f);
    root.SetSpacing(3.0f);

    auto label = std::make_unique<Raven::UILabel>();
    label->SetFont(font);
    label->SetText("ABCD");
    label->SetLineHeight(12.0f);
    label->SetBaselineOffset(0.0f);
    label->SetWrapMode(Raven::UITextWrapMode::Character);
    label->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    Raven::UILabel* labelPtr = label.get();
    root.AddChild(std::move(label));

    auto sibling = std::make_unique<Raven::UIElement>();
    sibling->SetPreferredSize(Raven::math::Vec2(10.0f, 7.0f));
    Raven::UIElement* siblingPtr = sibling.get();
    root.AddChild(std::move(sibling));

    Raven::UIDrawList drawList;
    root.BuildDrawList(drawList);
    CheckNear("label wide height", labelPtr->GetDesiredSize().y, 12.0f);
    CheckNear("label wide arranged width", labelPtr->GetSize().x, 50.0f);
    CheckNear("label wide sibling y", siblingPtr->GetPosition().y, 20.0f);
    CheckNear("label wide root height", root.GetDesiredSize().y, 32.0f);
    Check(drawList.GetCommandCount() == 4u, "label wide glyph command count");

    // 同じUILabelを再利用し、親幅の変更が文字数ではなく行数と兄弟位置へ伝播することを確認します。
    root.SetPreferredSize(Raven::math::Vec2(35.0f, 0.0f));
    drawList.Clear();
    root.BuildDrawList(drawList);
    CheckNear("label narrow height", labelPtr->GetDesiredSize().y, 24.0f);
    CheckNear("label narrow arranged width", labelPtr->GetSize().x, 25.0f);
    CheckNear("label narrow sibling y", siblingPtr->GetPosition().y, 32.0f);
    CheckNear("label narrow root height", root.GetDesiredSize().y, 44.0f);
    Check(drawList.GetCommandCount() == 4u, "label narrow glyph command count");
    CheckNear("label narrow wrapped glyph x", drawList.GetCommands()[2u].Rect.Min.x, 5.0f);
    CheckNear("label narrow wrapped glyph y", drawList.GetCommands()[2u].Rect.Min.y, 17.0f);

    root.SetPreferredSize(Raven::math::Vec2(25.0f, 0.0f));
    drawList.Clear();
    root.BuildDrawList(drawList);
    CheckNear("label smallest height", labelPtr->GetDesiredSize().y, 48.0f);
    CheckNear("label smallest sibling y", siblingPtr->GetPosition().y, 56.0f);
    CheckNear("label smallest root height", root.GetDesiredSize().y, 68.0f);

    labelPtr->SetText("AB");
    drawList.Clear();
    root.BuildDrawList(drawList);
    CheckNear("label text change height", labelPtr->GetDesiredSize().y, 24.0f);
    CheckNear("label text change sibling y", siblingPtr->GetPosition().y, 32.0f);
    Check(drawList.GetCommandCount() == 2u, "label text change glyph count");

    root.SetPreferredSize(Raven::math::Vec2(60.0f, 0.0f));
    root.SetClipChildren(true);
    labelPtr->SetText("ABCD");
    labelPtr->SetTextAlignment(Raven::UITextHorizontalAlignment::Right);
    drawList.Clear();
    root.BuildDrawList(drawList);
    CheckNear("right aligned label glyph x", drawList.GetCommands()[0u].Rect.Min.x, 15.0f);
    Check(drawList.GetCommands()[0u].Clip.Enabled, "label glyph inherits parent clip");
    CheckNear("label glyph clip min x", drawList.GetCommands()[0u].Clip.Rect.Min.x, 0.0f);
    CheckNear("label glyph clip max x", drawList.GetCommands()[0u].Clip.Rect.Max.x, 60.0f);
    CheckNear("label glyph clip max y", drawList.GetCommands()[0u].Clip.Rect.Max.y, 32.0f);

    labelPtr->SetVisible(false);
    drawList.Clear();
    root.BuildDrawList(drawList);
    Check(drawList.GetCommandCount() == 0u, "hidden label emits no glyph commands");
    CheckNear("hidden label sibling y", siblingPtr->GetPosition().y, 5.0f);
}

void TestTextLayoutMeasurement()
{
    const auto font = CreateTextLayoutFixture();
    const Raven::UITextLayoutResult empty = Raven::UITextLayout::Build(*font, "", 12.0f);
    Check(empty.Metrics.LineCount == 0u, "empty text has no lines");
    Check(empty.Glyphs.empty(), "empty text has no glyphs");

    const Raven::UITextLayoutResult lines = Raven::UITextLayout::Build(*font, "AB\r\nC\n", 12.0f);
    Check(lines.Metrics.LineCount == 3u, "CRLF and trailing newline line count");
    CheckNear("multiline width", lines.Metrics.Width, 20.0f);
    CheckNear("multiline height", lines.Metrics.Height, 36.0f);
    CheckNear("multiline ascent", lines.Metrics.Ascent, 8.0f);
    CheckNear("multiline descent", lines.Metrics.Descent, -2.0f);
    CheckNear("trailing empty line baseline", lines.Lines[2u].BaselineY, 24.0f);
    CheckNear("trailing empty line pen x", lines.FinalPen.x, 0.0f);
    CheckNear("trailing empty line pen y", lines.FinalPen.y, 24.0f);

    const Raven::UITextMetrics measured = Raven::UITextMeasurement::Measure(*font, "AB\r\nC\n", 12.0f);
    CheckNear("measurement width matches layout", measured.Width, lines.Metrics.Width);
    CheckNear("measurement height matches layout", measured.Height, lines.Metrics.Height);
    Check(measured.LineCount == lines.Metrics.LineCount, "measurement line count matches layout");

    Raven::UITextLayoutOptions options{};
    options.LineHeight = 12.0f;
    options.MaxWidth = 20.0f;
    options.Wrap = Raven::UITextWrapMode::Character;
    const Raven::UITextLayoutResult character = Raven::UITextLayout::Build(*font, "ABC", options);
    Check(character.Metrics.LineCount == 2u, "character wrap at width boundary");
    CheckNear("character wrap first line", character.Lines[0u].Width, 20.0f);
    CheckNear("character wrap second baseline", character.Glyphs[2u].Pen.y, 12.0f);
    options.MaxWidth = 9.0f;
    const Raven::UITextLayoutResult oversized = Raven::UITextLayout::Build(*font, "AB", options);
    Check(oversized.Metrics.LineCount == 2u, "oversized glyph does not create empty line");
    CheckNear("oversized glyph width", oversized.Metrics.Width, 10.0f);

    options.MaxWidth = 30.0f;
    options.Wrap = Raven::UITextWrapMode::Word;
    const Raven::UITextLayoutResult word = Raven::UITextLayout::Build(*font, "A BC", options);
    Check(word.Metrics.LineCount == 2u, "word wraps before whole word");
    CheckNear("word trailing space trimmed", word.Lines[0u].Width, 10.0f);
    CheckNear("word second line width", word.Lines[1u].Width, 20.0f);
    CheckNear("word second line first glyph", word.Glyphs[1u].Pen.x, 0.0f);
    const Raven::UITextLayoutResult longWord = Raven::UITextLayout::Build(*font, "ABCD", options);
    Check(longWord.Metrics.LineCount == 2u, "long word falls back to character wrap");

    options.MaxWidth = 40.0f;
    options.Wrap = Raven::UITextWrapMode::None;
    options.Alignment = Raven::UITextHorizontalAlignment::Center;
    const Raven::UITextLayoutResult centered = Raven::UITextLayout::Build(*font, "AB\nC", options);
    CheckNear("center first line x", centered.Glyphs[0u].Pen.x, 10.0f);
    CheckNear("center second line x", centered.Glyphs[2u].Pen.x, 15.0f);
    CheckNear("center final pen x", centered.FinalPen.x, 25.0f);
    options.Alignment = Raven::UITextHorizontalAlignment::Right;
    const Raven::UITextLayoutResult right = Raven::UITextLayout::Build(*font, "AB\nC", options);
    CheckNear("right first line x", right.Glyphs[0u].Pen.x, 20.0f);
    CheckNear("right second line x", right.Glyphs[2u].Pen.x, 30.0f);

    options.MaxWidth = 0.0f;
    options.Alignment = Raven::UITextHorizontalAlignment::Left;
    options.GlyphScale = Raven::math::Vec2(2.0f, 1.5f);
    const Raven::UITextLayoutResult scaled = Raven::UITextLayout::Build(*font, "AB", options);
    CheckNear("scaled advance", scaled.Metrics.Width, 40.0f);
    CheckNear("scaled ascent", scaled.Metrics.Ascent, 12.0f);
    CheckNear("scaled descent", scaled.Metrics.Descent, -3.0f);

    options.GlyphScale = Raven::math::Vec2(1.0f, 1.0f);
    const Raven::UITextLayoutResult fallback = Raven::UITextLayout::Build(*font, "A\xE3\x81\x82" "Z", options);
    Check(fallback.Glyphs.size() == 3u, "utf8 and fallback glyph count");
    Check(fallback.Glyphs[1u].Codepoint == 0x3042u, "utf8 Japanese codepoint decoded");
    Check(fallback.Glyphs[2u].Codepoint == 0xFFFDu, "missing glyph uses replacement");
}

} // namespace

int main()
{
    TestTextLayoutMeasurement();
    TestUILabelTextReflow();
    TestUILabelGlyphClipTransform();
    TestWindowFramebufferMetrics();
    TestTabSystem();
    TestDragDropRouting();
    TestTreeViewDragDrop();
    TestTreeViewCrossDrop();
    TestTreeViewEmptyAreaDrop();
    TestTreeViewDragAutoScroll();
    TestTreeViewNoOpDrop();
    TestTreeViewLastChildNoOpDrop();
    TestTreeViewDragAutoExpand();
    TestTextEditBuffer();
    TestInputNumber();
    TestInputEventRouting();
    TestPopupRouting();
    TestComboBox();
    TestTooltip();
    TestPopupOutsideClickConsumption();
    TestComboBoxKeyboardBoundaries();
    TestTooltipDelayAndReset();
    TestTreeView();
    TestTable();
    // 共通Scrollbar幾何: HeaderなしTreeとHeaderありTableでTrack原点だけが異なります。
    Raven::UIScrollBarMetrics metrics{ 48.0f, 72.0f, 0.0f };
    Check(metrics.IsVisible(), "scrollbar metrics visible");
    CheckNear("scrollbar metrics max", metrics.MaxOffset(), 24.0f);
    CheckNear("scrollbar metrics thumb", metrics.ThumbLength(), 32.0f);
    CheckNear("scrollbar metrics start", metrics.ThumbStart(), 0.0f);
    CheckNear("scrollbar metrics drag end", metrics.OffsetFromThumbStart(16.0f), 24.0f);
    metrics.Offset = 12.0f;
    CheckNear("scrollbar metrics middle", metrics.ThumbStart(), 8.0f);
    metrics.Content = 24.0f;
    Check(metrics.IsVisible() == false, "scrollbar metrics hidden");
    CheckNear("scrollbar metrics no scroll", metrics.OffsetFromThumbStart(16.0f), 0.0f);

    // Widget個別ClipはWorld Transform後に親Clipと交差することを検証します。
    Raven::UIDrawList clipDrawList;
    clipDrawList.AddRect(Raven::math::Vec2(0.0f, 0.0f),
        Raven::math::Vec2(20.0f, 20.0f),
        Raven::math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    Raven::UIRect localClip;
    localClip.Min = Raven::math::Vec2(2.0f, 3.0f);
    localClip.Max = Raven::math::Vec2(12.0f, 13.0f);
    clipDrawList.ApplyClip(0u, Raven::UIClipRect::FromRect(localClip));
    Raven::UITransform2D clipTransform = Raven::UITransform2D::Identity();
    clipTransform.Translation = Raven::math::Vec2(10.0f, 20.0f);
    clipDrawList.ApplyTransform(0u, clipTransform);
    Raven::UIRect ancestorClip;
    ancestorClip.Min = Raven::math::Vec2(15.0f, 20.0f);
    ancestorClip.Max = Raven::math::Vec2(40.0f, 40.0f);
    clipDrawList.ApplyClip(0u, Raven::UIClipRect::FromRect(ancestorClip));
    CheckNear("transformed cell clip min x", clipDrawList.GetCommands()[0u].Clip.Rect.Min.x, 15.0f);
    CheckNear("transformed cell clip min y", clipDrawList.GetCommands()[0u].Clip.Rect.Min.y, 23.0f);
    CheckNear("transformed cell clip max x", clipDrawList.GetCommands()[0u].Clip.Rect.Max.x, 22.0f);
    CheckNear("transformed cell clip max y", clipDrawList.GetCommands()[0u].Clip.Rect.Max.y, 33.0f);

    Raven::UIDrawList drawList;
    Raven::UIElement root;
    root.SetLayoutMode(Raven::UILayoutMode::Vertical);
    root.SetPreferredSize(Raven::math::Vec2(60.0f, 0.0f));
    root.SetPadding(5.0f);
    root.SetSpacing(3.0f);

    auto container = std::make_unique<Raven::UIElement>();
    container->SetLayoutMode(Raven::UILayoutMode::Vertical);
    container->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    container->SetPadding(2.0f);

    auto wrapping = std::make_unique<WrappingElement>();
    wrapping->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    WrappingElement* wrappingPtr = wrapping.get();
    container->AddChild(std::move(wrapping));

    auto sibling = std::make_unique<Raven::UIElement>();
    sibling->SetPreferredSize(Raven::math::Vec2(10.0f, 7.0f));
    Raven::UIElement* siblingPtr = sibling.get();
    container->AddChild(std::move(sibling));

    Raven::UIElement* containerPtr = root.AddChild(std::move(container));
    root.BuildDrawList(drawList);

    // Root content幅50、Container content幅46なので仮想テキストは3行になります。
    CheckNear("wrappingPtr->GetDesiredSize().y", wrappingPtr->GetDesiredSize().y, 30.0f);
    CheckNear("containerPtr->GetDesiredSize().y", containerPtr->GetDesiredSize().y, 41.0f);
    CheckNear("root.GetDesiredSize().y", root.GetDesiredSize().y, 51.0f);
    CheckNear("siblingPtr->GetPosition().y", siblingPtr->GetPosition().y, 32.0f);

    // さらに幅を縮めた場合も、古い高さを流用せず5行へ増加することを確認します。
    root.SetPreferredSize(Raven::math::Vec2(35.0f, 0.0f));
    root.BuildDrawList(drawList);
    CheckNear("narrow wrapping height", wrappingPtr->GetDesiredSize().y, 50.0f);
    CheckNear("narrow container height", containerPtr->GetDesiredSize().y, 61.0f);
    CheckNear("narrow root height", root.GetDesiredSize().y, 71.0f);
    CheckNear("narrow sibling position", siblingPtr->GetPosition().y, 52.0f);

    // Rootの幅変更による再Measureで、子と親の高さ・Sibling位置が揃って更新されます。
    root.SetPreferredSize(Raven::math::Vec2(120.0f, 0.0f));
    root.BuildDrawList(drawList);
    CheckNear("wrappingPtr->GetDesiredSize().y", wrappingPtr->GetDesiredSize().y, 10.0f);
    CheckNear("containerPtr->GetDesiredSize().y", containerPtr->GetDesiredSize().y, 21.0f);
    CheckNear("root.GetDesiredSize().y", root.GetDesiredSize().y, 31.0f);
    CheckNear("siblingPtr->GetPosition().y", siblingPtr->GetPosition().y, 12.0f);

    // Measure対象外の子もArrangeには参加します。親の高さだけが減ることを検証します。
    siblingPtr->SetAffectsParentMeasure(false);
    root.BuildDrawList(drawList);
    CheckNear("excluded container height", containerPtr->GetDesiredSize().y, 14.0f);
    CheckNear("excluded root height", root.GetDesiredSize().y, 24.0f);
    CheckNear("excluded sibling position", siblingPtr->GetPosition().y, 12.0f);

    siblingPtr->SetAffectsParentMeasure(true);
    root.BuildDrawList(drawList);
    CheckNear("restored container height", containerPtr->GetDesiredSize().y, 21.0f);
    CheckNear("restored root height", root.GetDesiredSize().y, 31.0f);

    // Marginは外側の必要高さにだけ加算し、折り返し自体の行数には影響しません。
    wrappingPtr->SetMargin(Raven::UIThickness(1.0f, 2.0f));
    root.BuildDrawList(drawList);
    CheckNear("margin wrapping height", wrappingPtr->GetDesiredSize().y, 10.0f);
    CheckNear("margin container height", containerPtr->GetDesiredSize().y, 25.0f);
    CheckNear("margin root height", root.GetDesiredSize().y, 35.0f);
    CheckNear("margin sibling position", siblingPtr->GetPosition().y, 16.0f);

    // 非表示の子はMeasure集約とArrangeの双方から除外されます。
    siblingPtr->SetVisible(false);
    root.BuildDrawList(drawList);
    CheckNear("hidden container height", containerPtr->GetDesiredSize().y, 18.0f);
    CheckNear("hidden root height", root.GetDesiredSize().y, 28.0f);
    TestUIImmediateContext();
    TestUITheme();
    TestDPIContextCoordinates();
    TestDPILayoutMetrics();
    TestDPISizeConstraints();
    TestDPIAbsolutePosition();
    TestDPILabelTypographyMetrics();
    TestDPIGlyphScale();
    TestDPIAtlasLabelBinding();
    TestDPIFontAutoRebind();
    TestDPIFontBatchRefresh();
    TestDPIFontBuildFailureDetails();
    TestDPIFontDataAndCapacityFailure();
    TestTextureDeviceUnavailableDiagnostic();
    TestDockLayout();
    TestDockSpace();
    TestDockTabMultipleWindowRestore();
    TestDockTabMissingSourceFallback();
    TestDockTabWindowExtraction();
    TestDockTabView();
    TestDockTabTransfer();
    TestDockCollapse();
    TestDockStructureSnapshot();
    TestDockFullSnapshot();
    TestDockSnapshotJson();
    TestDockSnapshotFileRecovery();
    TestDockRestoreFailurePreservesStructure();
    return 0;
}
