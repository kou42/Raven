#pragma once

#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/UI/Text/UIFontAtlas.h"

namespace Raven
{
class Application;
class UILabel;
class UIInputText;
class UIInputNumber;
class UIComboBox;
class UITreeView;
class UITable;
class UITabView;
class UIDockSpace;
class UIElement;

// Raven UIのFont Rasterize -> Atlas -> UILabel -> DrawListを検証するDebug Layerです。
// Font Pathやサンプル文字列は汎用Widgetへ持ち込まず、ここに閉じ込めます。
class UITextDemoLayer final : public Layer
{
public:
    explicit UITextDemoLayer(Application& application);
    void OnAttach() override;
    void OnDetach() override;

private:
    Application& m_Application;
    Ref<UIFontAtlas> m_Atlas;
    UILabel* m_Label = nullptr; // 所有者はUIContextのRoot Treeです。
    UIInputNumber* m_InputNumber = nullptr; // 所有権はRoot Treeです。
    UIComboBox* m_ComboBox = nullptr;
    UITreeView* m_TreeView = nullptr; // Root Treeが所有する階層表示Demoです。
    UITable* m_Table = nullptr; // Root Treeが所有する表表示Demoです。
    UITabView* m_TabView = nullptr; // Root Treeが所有するTab切替Demoです。
    UIDockSpace* m_DockSpace = nullptr; // Root Treeが所有するDocking操作Demoです。
    UIElement* m_PopupTrigger = nullptr;
    UIElement* m_Popup = nullptr;
    UIInputText* m_InputText = nullptr; // 同じFont Atlasを共有する入力Demoです。
};
} // namespace Raven
