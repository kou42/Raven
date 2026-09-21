#include "Raven/UI/Text/Debug/UITextDemoLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Text/UIFontAtlasBuilder.h"
#include "Raven/UI/Text/UIUtf8.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UIInputText.h"
#include "Raven/UI/Widgets/UIInputNumber.h"
#include "Raven/UI/Widgets/UITreeView.h"
#include "Raven/UI/Widgets/UITable.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Raven
{
namespace
{
constexpr std::string_view kDemoText =
    "Raven UI: Hello, World! 0123456789\n"
    "日本語テキスト表示テスト：こんにちは！\n"
    "Fallback test: \xF0\x9F\x9A\x80";

// 使用Fontは開発機ごとに異なるため、環境変数で明示指定できるようにします。
// 日本語を含むFontが見つからなければ、OS標準の候補を順に確認します。
std::string FindDemoFont()
{
    const char* configured = std::getenv("RAVEN_UI_DEMO_FONT");
    if (configured != nullptr && configured[0] != '\0')
    {
        if (std::filesystem::is_regular_file(configured))
        {
            return configured;
        }
        std::cout << "[Raven UI Text] RAVEN_UI_DEMO_FONT not found: "
            << configured << '\n';
        return {};
    }

    constexpr const char* candidates[] =
    {
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    for (const char* candidate : candidates)
    {
        if (std::filesystem::is_regular_file(candidate))
        {
            return candidate;
        }
    }
    return {};
}

std::vector<std::uint32_t> CollectDemoCodepoints()
{
    std::vector<std::uint32_t> result;
    std::unordered_set<std::uint32_t> seen;
    // Missing Glyphの代替表示用にASCIIとReplacement Characterを必ず候補へ含めます。
    for (std::uint32_t cp = 32u; cp < 127u; ++cp)
    {
        result.push_back(cp);
        seen.insert(cp);
    }
    result.push_back(UIUtf8::ReplacementCharacter);
    seen.insert(UIUtf8::ReplacementCharacter);

    std::size_t offset = 0u;
    std::uint32_t codepoint = 0u;
    while (UIUtf8::DecodeNext(kDemoText, offset, codepoint))
    {
        if (seen.insert(codepoint).second == true)
        {
            result.push_back(codepoint);
        }
    }
    return result;
}
} // namespace

UITextDemoLayer::UITextDemoLayer(Application& application)
    : m_Application(application)
{
}

void UITextDemoLayer::OnAttach()
{
    const std::string fontPath = FindDemoFont();
    if (fontPath.empty())
    {
        std::cout << "[Raven UI Text] Font not found. Set RAVEN_UI_DEMO_FONT to a TTF/TTC path.\n";
        return;
    }

    // OnAttachはApplicationがWindow/Rendererを生成した後に呼ばれるため、
    // Font AtlasのGPU Texture生成をここで行います。
    Ref<UIFontAtlas> atlas = CreateRef<UIFontAtlas>();
    UIFontAtlasBuildOptions options{};
    options.PixelHeight = 24.0f;
    options.AtlasWidth = 1024u;
    options.AtlasHeight = 1024u;
    if (UIFontAtlasBuilder::BuildFromFile(
        fontPath, CollectDemoCodepoints(), options, *atlas) == false)
    {
        std::cout << "[Raven UI Text] Atlas build failed: " << fontPath << '\n';
        return;
    }

    auto label = CreateScope<UILabel>();
    label->SetPosition(math::Vec2(24.0f, 80.0f));
    label->SetSize(math::Vec2(800.0f, 100.0f));
    label->SetFont(atlas);
    label->SetText(std::string(kDemoText));
    label->SetBaselineOffset(26.0f);
    label->SetLineHeight(30.0f);
    UIElement* attached = m_Application.GetUIContext().GetRootElement().AddChild(std::move(label));
    if (attached == nullptr)
    {
        std::cout << "[Raven UI Text] Failed to attach UILabel.\n";
        return;
    }
    // Font AtlasはUILabelとInputTextで共有し、入力後もGlyph Textureを保持します。
    auto input = CreateScope<UIInputText>();
    input->SetPosition(math::Vec2(24.0f, 200.0f));
    input->SetSize(math::Vec2(500.0f, 36.0f));
    input->SetFont(atlas);
    input->SetText("Raven UI: edit me!");
    // ClipboardのPlatform依存はDemo側に閉じ込め、Widgetと編集バッファにはGLFWを持ち込みません。
    GLFWwindow* window = static_cast<GLFWwindow*>(m_Application.GetWindow().GetNativeWindow());
    input->SetClipboard(
        [window]() -> std::string
        {
            if (window == nullptr)
            {
                return {};
            }
            const char* text = glfwGetClipboardString(window);
            return text != nullptr ? std::string(text) : std::string{};
        },
        [window](const std::string& text)
        {
            if (window != nullptr)
            {
                glfwSetClipboardString(window, text.c_str());
            }
        });
    input->SetOnChange([](const std::string& text)
        {
            std::cout << "[Raven UI InputText] " << text << '\n';
        });
    UIElement* attachedInput = m_Application.GetUIContext().GetRootElement().AddChild(std::move(input));
    if (attachedInput != nullptr)
    {
        m_InputText = static_cast<UIInputText*>(attachedInput);
    }

    auto number = CreateScope<UIInputNumber>();
    number->SetPosition(math::Vec2(24.0f, 260.0f));
    number->SetSize(math::Vec2(180.0f, 30.0f));
    number->SetFont(atlas);
    number->SetRange(-100.0, 100.0);
    number->SetValue(42.0);
    number->SetClipboard(
        [window]() -> std::string
        {
            if (window == nullptr)
            {
                return {};
            }
            const char* text = glfwGetClipboardString(window);
            return text != nullptr ? std::string(text) : std::string{};
        },
        [window](const std::string& text)
        {
            if (window != nullptr)
            {
                glfwSetClipboardString(window, text.c_str());
            }
        });
    number->SetOnValueChanged([](double value)
        {
            std::cout << "[Raven UI InputNumber] " << value << '\n';
        });
    UIElement* attachedNumber = m_Application.GetUIContext().GetRootElement().AddChild(std::move(number));
    if (attachedNumber != nullptr)
    {
        m_InputNumber = static_cast<UIInputNumber*>(attachedNumber);
    }

    // Popupは通常Rootの後に描画され、外側ClickとEscapeで閉じます。
    auto popup = CreateScope<UIButton>();
    popup->SetPosition(math::Vec2(24.0f, 340.0f));
    popup->SetSize(math::Vec2(220.0f, 80.0f));
    popup->SetOnClick([this]()
        {
            std::cout << "[Raven UI Popup] Item clicked\\n";
            m_Application.GetUIContext().ClosePopup();
        });
    m_Popup = m_Application.GetUIContext().AddPopup(std::move(popup));

    auto trigger = CreateScope<UIButton>();
    trigger->SetPosition(math::Vec2(24.0f, 300.0f));
    trigger->SetSize(math::Vec2(220.0f, 34.0f));
    trigger->SetFocusable(true);
    trigger->SetOnClick([this]()
        {
            UIContext& context = m_Application.GetUIContext();
            if (context.GetOpenPopup() == m_Popup)
            {
                context.ClosePopup();
            }
            else if (m_Popup != nullptr)
            {
                context.OpenPopupAt(m_Popup, m_PopupTrigger);
            }
        });
    m_PopupTrigger = m_Application.GetUIContext().GetRootElement().AddChild(std::move(trigger));
    std::cout << "[Raven UI Popup] Click the button at (24, 300); Escape/outside click closes.\\n";

    auto combo = CreateScope<UIComboBox>();
    combo->SetPosition(math::Vec2(270.0f, 300.0f));
    combo->SetSize(math::Vec2(180.0f, 30.0f));
    combo->SetFont(atlas);
    combo->SetOptions({ "Idle", "Walk", "Run" });
    combo->SetSelectedIndex(0u);
    combo->SetOnSelectionChanged([](std::size_t index, const std::string& text)
        {
            std::cout << "[Raven UI ComboBox] " << index << ": " << text << '\n';
        });
    m_ComboBox = static_cast<UIComboBox*>(
        m_Application.GetUIContext().GetRootElement().AddChild(std::move(combo)));

    // TreeViewはSceneの実体を所有せず、Hierarchy相当の表示ノードだけを保持します。
    // 左端の開閉記号、行クリック、上下左右キーとEnterを確認できます。
    auto tree = CreateScope<UITreeView>();
    tree->SetPosition(math::Vec2(560.0f, 200.0f));
    tree->SetSize(math::Vec2(280.0f, 264.0f));
    tree->SetFont(atlas);
    UITreeNode* scene = tree->AddRoot(1u, "Scene");
    UITreeNode* player = tree->AddNode(scene, 2u, "Player");
    tree->AddNode(player, 3u, "Mesh");
    tree->AddNode(player, 4u, "Collider");
    tree->AddNode(scene, 5u, "Camera");
    tree->AddNode(scene, 6u, "Directional Light");
    UITreeNode* environment = tree->AddRoot(7u, "Environment");
    tree->AddNode(environment, 8u, "Terrain");
    tree->AddNode(environment, 9u, "Sky");
    // Viewportを超える行数にしてWheelとKeyboard選択追従を確認します。
    for (std::uint64_t id = 10u; id < 26u; ++id)
    {
        tree->AddNode(environment, id, "Environment Item " + std::to_string(id));
    }
    tree->SetOnSelectionChanged([](std::uint64_t id)
        {
            std::cout << "[Raven UI TreeView] selected ID: " << id << '\n';
        });
    tree->SetOnExpansionChanged([](std::uint64_t id, bool expanded)
        {
            std::cout << "[Raven UI TreeView] node " << id
                << (expanded ? " expanded" : " collapsed") << '\n';
        });
    m_TreeView = static_cast<UITreeView*>(
        m_Application.GetUIContext().GetRootElement().AddChild(std::move(tree)));

    // TableはHeaderを固定し、Bodyだけをスクロールします。
    auto table = CreateScope<UITable>();
    table->SetPosition(math::Vec2(860.0f, 200.0f));
    table->SetSize(math::Vec2(350.0f, 264.0f));
    table->SetFont(atlas);
    table->AddColumn("Entity", 170.0f);
    table->AddColumn("Type", 160.0f);
    for (std::size_t index = 0u; index < 20u; ++index)
    {
        table->AddRow({ "Entity " + std::to_string(index), index % 2u == 0u ? "Mesh" : "Light" });
    }
    table->SetOnSelectionChanged([](std::size_t index)
        {
            std::cout << "[Raven UI Table] selected row: " << index << '\\n';
        });
    m_Table = static_cast<UITable*>(
        m_Application.GetUIContext().GetRootElement().AddChild(std::move(table)));

    // Tooltipは通常のHover入力を遮らず、Popup表示中は自動的に隠れます。
    UIContext& tooltipContext = m_Application.GetUIContext();
    tooltipContext.SetTooltip(m_PopupTrigger, "Open Popup", atlas);
    tooltipContext.SetTooltip(m_ComboBox, "Select animation", atlas);

    m_Atlas = std::move(atlas);
    m_Label = static_cast<UILabel*>(attached);
    std::cout << "[Raven UI Text] Demo font: " << fontPath << '\n';
}

void UITextDemoLayer::OnDetach()
{
    if (m_Table != nullptr)
    {
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_Table);
        m_Table = nullptr;
    }
    if (m_TreeView != nullptr)
    {
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_TreeView);
        m_TreeView = nullptr;
    }
    if (m_ComboBox != nullptr)
    {
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_ComboBox);
        m_ComboBox = nullptr;
    }
    if (m_PopupTrigger != nullptr)
    {
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_PopupTrigger);
        m_PopupTrigger = nullptr;
    }
    if (m_Popup != nullptr)
    {
        m_Application.GetUIContext().RemovePopup(m_Popup);
        m_Popup = nullptr;
    }
    if (m_InputNumber != nullptr)
    {
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_InputNumber);
        m_InputNumber = nullptr;
    }
    if (m_InputText != nullptr)
    {
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_InputText);
        m_InputText = nullptr;
    }
    if (m_Label != nullptr)
    {
        // Treeの所有するWidgetを先に破棄してから、Font Atlas参照を解放します。
        m_Application.GetUIContext().GetRootElement().RemoveChild(m_Label);
        m_Label = nullptr;
    }
    m_Atlas = nullptr;
}
} // namespace Raven
