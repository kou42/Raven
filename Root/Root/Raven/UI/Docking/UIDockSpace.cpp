#include "Raven/UI/Docking/UIDockSpace.h"
#include "Raven/Core/JsonParser.h"
#include "Raven/Core/JsonWriter.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Raven
{
namespace
{
// 最前面の装飾専用Element。Hit Testを無効にしてDrop先の探索を妨げません。
class UIDockPreview final : public UIElement
{
protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
    {
        drawList.AddRect(position,
            math::Vec2(position.x + GetSize().x, position.y + GetSize().y),
            ApplyVisualColor(math::Vec4(0.30f, 0.58f, 0.95f, 0.26f)));
    }
};
} // namespace


namespace
{
bool DockIOError(std::string* error, const std::string& message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

const Core::JsonValue* DockField(const Core::JsonValue& object,
    const char* name, Core::JsonValue::Type type)
{
    const Core::JsonValue* value = object.Find(name);
    return value != nullptr && value->GetType() == type ? value : nullptr;
}

bool DockReadId(const Core::JsonValue& object, const char* name,
    std::uint64_t& id)
{
    const Core::JsonValue* value = DockField(object, name,
        Core::JsonValue::Type::String);
    if (value == nullptr || value->GetString().empty() == true)
    {
        return false;
    }
    const std::string& text = value->GetString();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool DockReadIndex(const Core::JsonValue& object, const char* name,
    std::uint32_t& index)
{
    const Core::JsonValue* value = DockField(object, name,
        Core::JsonValue::Type::Number);
    if (value == nullptr)
    {
        return false;
    }
    const double number = value->GetNumber();
    if (std::isfinite(number) == false || number < 0.0 ||
        number > static_cast<double>(UINT32_MAX) ||
        std::floor(number) != number)
    {
        return false;
    }
    index = static_cast<std::uint32_t>(number);
    return true;
}

bool DockValidate(const UIDockSpaceSnapshot& snapshot)
{
    UIDockLayout tree;
    if (tree.RestoreStructure(snapshot.Structure) == false)
    {
        return false;
    }
    std::unordered_map<std::uint64_t, UITabModel> models;
    for (const UIDockLayoutRecord& record : snapshot.Structure)
    {
        if (record.Kind == UIDockNodeKind::Tabs)
        {
            models.emplace(record.Id, UITabModel{});
        }
    }
    if (snapshot.Selections.size() != models.size())
    {
        return false;
    }
    for (const UIDockTabRecord& tab : snapshot.Tabs)
    {
        auto it = models.find(tab.LeafId);
        if (it == models.end() ||
            it->second.AddTab(tab.Tab.Id, tab.Tab.Title, tab.Tab.Closable) == false)
        {
            return false;
        }
    }
    std::unordered_set<std::uint64_t> seen;
    for (const auto& selection : snapshot.Selections)
    {
        auto it = models.find(selection.first);
        if (it == models.end() ||
            seen.insert(selection.first).second == false ||
            it->second.SelectTab(selection.second) == false)
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool SerializeDockSnapshot(const UIDockSpaceSnapshot& snapshot,
    std::string& outText, std::string* errorMessage)
{
    if (DockValidate(snapshot) == false)
    {
        return DockIOError(errorMessage, "Dock Snapshotの構造またはTab状態が不正です");
    }
    Core::JsonValue::Array nodes;
    for (const UIDockLayoutRecord& record : snapshot.Structure)
    {
        Core::JsonValue::Object node;
        node.emplace("id", Core::JsonValue(std::to_string(record.Id)));
        node.emplace("kind", Core::JsonValue(std::string(
            record.Kind == UIDockNodeKind::Split ? "split" : "tabs")));
        node.emplace("axis", Core::JsonValue(std::string(
            record.Axis == UIDockSplitAxis::Horizontal ? "horizontal" : "vertical")));
        node.emplace("ratio", Core::JsonValue(static_cast<double>(record.Ratio)));
        node.emplace("depth", Core::JsonValue(static_cast<double>(record.Depth)));
        nodes.emplace_back(std::move(node));
    }
    Core::JsonValue::Array tabs;
    for (const UIDockTabRecord& record : snapshot.Tabs)
    {
        Core::JsonValue::Object tab;
        tab.emplace("leafId", Core::JsonValue(std::to_string(record.LeafId)));
        tab.emplace("id", Core::JsonValue(std::to_string(record.Tab.Id)));
        tab.emplace("title", Core::JsonValue(record.Tab.Title));
        tab.emplace("closable", Core::JsonValue(record.Tab.Closable));
        tabs.emplace_back(std::move(tab));
    }
    Core::JsonValue::Array selections;
    for (const auto& selection : snapshot.Selections)
    {
        Core::JsonValue::Object value;
        value.emplace("leafId", Core::JsonValue(std::to_string(selection.first)));
        value.emplace("tabId", Core::JsonValue(std::to_string(selection.second)));
        selections.emplace_back(std::move(value));
    }
    Core::JsonValue::Object root;
    root.emplace("type", Core::JsonValue(std::string("RavenDockSnapshot")));
    root.emplace("version", Core::JsonValue(1.0));
    root.emplace("structure", Core::JsonValue(std::move(nodes)));
    root.emplace("tabs", Core::JsonValue(std::move(tabs)));
    root.emplace("selections", Core::JsonValue(std::move(selections)));
    return Core::JsonWriter::Write(Core::JsonValue(std::move(root)),
        outText, errorMessage);
}

bool DeserializeDockSnapshot(const std::string& text,
    UIDockSpaceSnapshot& outSnapshot, std::string* errorMessage)
{
    Core::JsonValue root;
    if (Core::JsonParser::Parse(text, root, errorMessage) == false)
    {
        return false;
    }
    const auto* type = DockField(root, "type", Core::JsonValue::Type::String);
    const auto* version = DockField(root, "version", Core::JsonValue::Type::Number);
    const auto* nodes = DockField(root, "structure", Core::JsonValue::Type::Array);
    const auto* tabs = DockField(root, "tabs", Core::JsonValue::Type::Array);
    const auto* selections = DockField(root, "selections", Core::JsonValue::Type::Array);
    if (type == nullptr || version == nullptr || nodes == nullptr ||
        tabs == nullptr || selections == nullptr ||
        type->GetString() != "RavenDockSnapshot" || version->GetNumber() != 1.0 ||
        nodes->GetArray().size() > 4096u || tabs->GetArray().size() > 65536u)
    {
        return DockIOError(errorMessage, "Dock Snapshotのtype/version/配列が不正です");
    }
    UIDockSpaceSnapshot parsed;
    for (const Core::JsonValue& value : nodes->GetArray())
    {
        UIDockLayoutRecord record;
        const auto* kind = DockField(value, "kind", Core::JsonValue::Type::String);
        const auto* axis = DockField(value, "axis", Core::JsonValue::Type::String);
        const auto* ratio = DockField(value, "ratio", Core::JsonValue::Type::Number);
        if (DockReadId(value, "id", record.Id) == false ||
            DockReadIndex(value, "depth", record.Depth) == false ||
            kind == nullptr || axis == nullptr || ratio == nullptr ||
            (kind->GetString() != "split" && kind->GetString() != "tabs") ||
            (axis->GetString() != "horizontal" && axis->GetString() != "vertical"))
        {
            return DockIOError(errorMessage, "Dock SnapshotのNodeが不正です");
        }
        record.Kind = kind->GetString() == "split" ?
            UIDockNodeKind::Split : UIDockNodeKind::Tabs;
        record.Axis = axis->GetString() == "horizontal" ?
            UIDockSplitAxis::Horizontal : UIDockSplitAxis::Vertical;
        const double number = ratio->GetNumber();
        record.Ratio = static_cast<float>(number);
        if (std::isfinite(number) == false ||
            std::isfinite(record.Ratio) == false ||
            record.Ratio <= 0.0f || record.Ratio >= 1.0f)
        {
            return DockIOError(errorMessage, "Dock Snapshotの分割比率が不正です");
        }
        parsed.Structure.push_back(record);
    }
    for (const Core::JsonValue& value : tabs->GetArray())
    {
        UIDockTabRecord record;
        const auto* title = DockField(value, "title", Core::JsonValue::Type::String);
        const auto* closable = DockField(value, "closable", Core::JsonValue::Type::Boolean);
        if (DockReadId(value, "leafId", record.LeafId) == false ||
            DockReadId(value, "id", record.Tab.Id) == false ||
            title == nullptr || closable == nullptr)
        {
            return DockIOError(errorMessage, "Dock SnapshotのTabが不正です");
        }
        record.Tab.Title = title->GetString();
        record.Tab.Closable = closable->GetBoolean();
        parsed.Tabs.push_back(std::move(record));
    }
    for (const Core::JsonValue& value : selections->GetArray())
    {
        std::uint64_t leaf = 0u;
        std::uint64_t selected = 0u;
        if (DockReadId(value, "leafId", leaf) == false ||
            DockReadId(value, "tabId", selected) == false)
        {
            return DockIOError(errorMessage, "Dock Snapshotの選択状態が不正です");
        }
        parsed.Selections.emplace_back(leaf, selected);
    }
    if (DockValidate(parsed) == false)
    {
        return DockIOError(errorMessage, "Dock Snapshotの参照関係が不正です");
    }
    // 成功時だけ呼び出し元を更新し、破損ファイルで現行設定を失わないようにします。
    outSnapshot = std::move(parsed);
    return true;
}

namespace
{
// 異常終了時に既存の保存ファイルを残せるよう、同じディレクトリに一時ファイルを作ります。
constexpr std::uintmax_t kMaximumDockSnapshotBytes = 8u * 1024u * 1024u;

bool ReadDockSnapshotFile(const std::filesystem::path& path,
    UIDockSpaceSnapshot& outSnapshot, std::string* errorMessage)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumDockSnapshotBytes)
    {
        return DockIOError(errorMessage, "Dock Snapshotのサイズ取得失敗または上限超過: " +
            path.string());
    }
    std::ifstream file(path, std::ios::binary);
    if (file.is_open() == false)
    {
        return DockIOError(errorMessage, "Dock Snapshotを開けません: " + path.string());
    }
    std::string text((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (file.bad() == true)
    {
        return DockIOError(errorMessage, "Dock Snapshotを読み込めません: " + path.string());
    }
    return DeserializeDockSnapshot(text, outSnapshot, errorMessage);
}
} // namespace

bool SaveDockSnapshot(const std::string& filePath,
    const UIDockSpaceSnapshot& snapshot, std::string* errorMessage)
{
    std::string text;
    if (SerializeDockSnapshot(snapshot, text, errorMessage) == false)
    {
        return false;
    }
    if (filePath.empty() == true || text.size() > kMaximumDockSnapshotBytes)
    {
        return DockIOError(errorMessage, "Dock Snapshotの保存先またはサイズが不正です");
    }

    const std::filesystem::path destination(filePath);
    std::filesystem::path temporary = destination;
    // 同時実行や前回クラッシュで残った一時ファイルを上書きしないよう固有名を使用します。
    static std::atomic<std::uint64_t> serial{ 0u };
    temporary += ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) +
        "." + std::to_string(serial.fetch_add(1u));
    std::filesystem::path backup = destination;
    backup += ".bak";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (file.is_open() == false)
        {
            return DockIOError(errorMessage, "Dock Snapshotの一時ファイルを開けません: " +
                temporary.string());
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (file.fail() == true)
        {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            return DockIOError(errorMessage, "Dock Snapshotの一時ファイルを書き込めません: " +
                temporary.string());
        }
    }

    std::error_code error;
    const bool hadDestination = std::filesystem::exists(destination, error);
    if (error)
    {
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        return DockIOError(errorMessage, "Dock Snapshotの保存先を確認できません: " +
            error.message());
    }
    if (hadDestination == true)
    {
        // Windowsではrenameが既存の宛先を置換できないため、旧版を退避してから交換します。
        std::filesystem::remove(backup, error);
        if (error)
        {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            return DockIOError(errorMessage, "Dock Snapshotの旧Backupを削除できません: " +
                error.message());
        }
        std::filesystem::rename(destination, backup, error);
        if (error)
        {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            return DockIOError(errorMessage, "Dock Snapshotの旧版を退避できません: " +
                error.message());
        }
    }

    std::filesystem::rename(temporary, destination, error);
    if (error)
    {
        const std::string reason = error.message();
        std::error_code cleanup;
        if (hadDestination == true)
        {
            // 新版への交換失敗時は旧版を元の名前へ戻します。
            std::filesystem::rename(backup, destination, cleanup);
        }
        std::error_code tempCleanup;
        std::filesystem::remove(temporary, tempCleanup);
        return DockIOError(errorMessage, "Dock Snapshotの置換失敗: " + reason +
            (cleanup ? " / 旧版の復帰失敗: " + cleanup.message() : ""));
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

bool LoadDockSnapshot(const std::string& filePath,
    UIDockSpaceSnapshot& outSnapshot, std::string* errorMessage)
{
    if (filePath.empty() == true)
    {
        return DockIOError(errorMessage, "Dock Snapshotの読み込み先が空です");
    }
    const std::filesystem::path destination(filePath);
    std::error_code error;
    const bool exists = std::filesystem::exists(destination, error);
    if (error)
    {
        return DockIOError(errorMessage, "Dock Snapshotの保存先を確認できません: " +
            error.message());
    }
    if (exists == true)
    {
        return ReadDockSnapshotFile(destination, outSnapshot, errorMessage);
    }
    // 旧版退避後に異常終了した場合のみBackupを復旧候補として読み込みます。
    std::filesystem::path backup = destination;
    backup += ".bak";
    if (std::filesystem::exists(backup, error) == true && error == false)
    {
        return ReadDockSnapshotFile(backup, outSnapshot, errorMessage);
    }
    return DockIOError(errorMessage, "Dock Snapshotが見つかりません: " + filePath);
}

UIDockSpace::UIDockSpace()
{
    SetLayoutMode(UILayoutMode::Absolute);
    SetClipChildren(true);
    auto preview = std::make_unique<UIDockPreview>();
    preview->SetHitTestVisible(false);
    preview->SetAffectsParentMeasure(false);
    preview->SetVisible(false);
    m_Preview = AddChild(std::move(preview));
}

bool UIDockSpace::SetPane(std::uint64_t leafId, Scope<UIElement> pane)
{
    const UIDockNode* node = m_Layout.FindNode(leafId);
    if (node == nullptr || node->GetKind() != UIDockNodeKind::Tabs ||
        pane == nullptr || pane->GetParent() != nullptr || pane->GetContext() != nullptr ||
        m_Panes.find(leafId) != m_Panes.end())
    {
        return false;
    }
    pane->SetAffectsParentMeasure(false);
    UIElement* raw = AddChild(std::move(pane));
    if (raw == nullptr)
    {
        return false;
    }
    m_Panes.emplace(leafId, raw);
    BringChildToFront(m_Preview);
    RefreshLayout();
    return true;
}

UIElement* UIDockSpace::GetPane(std::uint64_t leafId) const
{
    const auto it = m_Panes.find(leafId);
    return it == m_Panes.end() ? nullptr : it->second;
}

UITabView* UIDockSpace::CreateTabView(std::uint64_t leafId)
{
    UIDockNode* node = m_Layout.FindNode(leafId);
    if (node == nullptr || node->GetKind() != UIDockNodeKind::Tabs ||
        node->GetTabs()->GetTabCount() != 0u ||
        m_Panes.find(leafId) != m_Panes.end())
    {
        return nullptr;
    }
    auto view = std::make_unique<UITabView>();
    UITabView* raw = view.get();
    // UITabViewが表示とContentの所有権を担当し、Dock Nodeは順序・選択だけを保持します。
    // Close時はViewの内部Content削除が済んでから通知されるため、論理Modelを安全に更新できます。
    raw->SetOnSelectionChanged([this, leafId](std::uint64_t id)
    {
        UIDockNode* current = m_Layout.FindNode(leafId);
        if (current != nullptr && current->GetTabs() != nullptr)
        {
            current->GetTabs()->SelectTab(id);
        }
    });
    raw->SetOnClosed([this, leafId](std::uint64_t id)
    {
        UIDockNode* current = m_Layout.FindNode(leafId);
        if (current != nullptr && current->GetTabs() != nullptr)
        {
            current->GetTabs()->RemoveTab(id);
        }
    });
    raw->SetOnMoved([this, leafId](std::uint64_t id, std::size_t from, std::size_t to)
    {
        (void)from;
        UIDockNode* current = m_Layout.FindNode(leafId);
        if (current != nullptr && current->GetTabs() != nullptr)
        {
            current->GetTabs()->MoveTab(id, to);
        }
    });
    if (SetPane(leafId, std::move(view)) == false)
    {
        return nullptr;
    }
    m_TabViews.emplace(leafId, raw);
    return raw;
}

UITabView* UIDockSpace::GetTabView(std::uint64_t leafId) const
{
    const auto it = m_TabViews.find(leafId);
    return it == m_TabViews.end() ? nullptr : it->second;
}

bool UIDockSpace::AddTab(std::uint64_t leafId, std::uint64_t tabId,
    std::string title, Scope<UIElement> content, bool closable)
{
    UITabView* view = GetTabView(leafId);
    UIDockNode* node = m_Layout.FindNode(leafId);
    if (view == nullptr || node == nullptr || node->GetTabs() == nullptr ||
        content == nullptr || content->GetParent() != nullptr ||
        content->GetContext() != nullptr || tabId == 0u ||
        view->GetModel().FindTab(tabId) != nullptr ||
        node->GetTabs()->FindTab(tabId) != nullptr)
    {
        return false;
    }
    // View追加中に初回選択Callbackが走るため、論理Modelを先に登録します。
    if (node->GetTabs()->AddTab(tabId, title, closable) == false)
    {
        return false;
    }
    if (view->AddTab(tabId, std::move(title), std::move(content), closable) == false)
    {
        node->GetTabs()->RemoveTab(tabId);
        return false;
    }
    return true;
}

bool UIDockSpace::SelectTab(std::uint64_t leafId, std::uint64_t tabId)
{
    UITabView* view = GetTabView(leafId);
    return view != nullptr && view->SelectTab(tabId);
}

bool UIDockSpace::CloseTab(std::uint64_t leafId, std::uint64_t tabId)
{
    UITabView* view = GetTabView(leafId);
    return view != nullptr && view->CloseTab(tabId);
}

bool UIDockSpace::MoveTab(std::uint64_t leafId, std::uint64_t tabId, std::size_t index)
{
    UITabView* view = GetTabView(leafId);
    return view != nullptr && view->MoveTab(tabId, index);
}

UISplitter* UIDockSpace::GetSplitter(std::uint64_t splitId) const
{
    const auto it = m_Splitters.find(splitId);
    return it == m_Splitters.end() ? nullptr : it->second;
}

UIDockNode* UIDockSpace::Split(std::uint64_t leafId, UIDockSplitAxis axis,
    float ratio, bool newLeafFirst)
{
    UIDockNode* fresh = m_Layout.Split(leafId, axis, ratio, newLeafFirst);
    if (fresh != nullptr)
    {
        RefreshLayout();
    }
    return fresh;
}

void UIDockSpace::SetSplitterThickness(float value)
{
    if (std::isfinite(value) == true && value >= 0.0f)
    {
        m_SplitterThickness = value;
        RefreshLayout();
    }
}

void UIDockSpace::SetMinimumPaneExtent(float value)
{
    if (std::isfinite(value) == true && value >= 0.0f)
    {
        m_MinimumPaneExtent = value;
        RefreshLayout();
    }
}

void UIDockSpace::SyncWidgets()
{
    const UIDockRect bounds{ 0.0f, 0.0f, GetSize().x, GetSize().y };
    const auto placements = UIDockGeometry::Calculate(
        m_Layout, bounds, m_SplitterThickness, m_MinimumPaneExtent);
    std::unordered_set<std::uint64_t> active;
    for (const UIDockPlacement& placement : placements)
    {
        if (placement.IsSplit == false)
        {
            continue;
        }
        active.insert(placement.NodeId);
        if (m_Splitters.find(placement.NodeId) != m_Splitters.end())
        {
            continue;
        }
        const std::uint64_t id = placement.NodeId;
        auto splitter = std::make_unique<UISplitter>();
        splitter->SetAffectsParentMeasure(false);
        splitter->SetName("DockSplitter_" + std::to_string(id));
        splitter->SetOnDragDelta([this, id](float delta)
        {
            OnSplitterDrag(id, delta);
        });
        UISplitter* raw = static_cast<UISplitter*>(AddChild(std::move(splitter)));
        if (raw != nullptr)
        {
            m_Splitters.emplace(id, raw);
        }
    }
    // 将来のTree再構築にも備え、消えたSplitの入力CaptureをRemoveChild経由で解除します。
    for (auto it = m_Splitters.begin(); it != m_Splitters.end();)
    {
        if (active.find(it->first) == active.end())
        {
            RemoveChild(it->second);
            it = m_Splitters.erase(it);
        }
        else
        {
            ++it;
        }
    }
    BringChildToFront(m_Preview);
}

void UIDockSpace::ApplyRect(UIElement& element, const UIDockRect& rect)
{
    if (element.GetPosition().x != rect.X || element.GetPosition().y != rect.Y)
    {
        element.SetPosition(math::Vec2(rect.X, rect.Y));
    }
    if (element.GetPreferredSize().x != rect.Width ||
        element.GetPreferredSize().y != rect.Height)
    {
        element.SetSize(math::Vec2(rect.Width, rect.Height));
    }
}

void UIDockSpace::ApplyLayout()
{
    const UIDockRect bounds{ 0.0f, 0.0f, GetSize().x, GetSize().y };
    const auto placements = UIDockGeometry::Calculate(
        m_Layout, bounds, m_SplitterThickness, m_MinimumPaneExtent);
    for (const UIDockPlacement& placement : placements)
    {
        if (placement.IsSplit == true)
        {
            UISplitter* splitter = GetSplitter(placement.NodeId);
            const UIDockNode* node = m_Layout.FindNode(placement.NodeId);
            if (splitter != nullptr && node != nullptr)
            {
                splitter->SetOrientation(node->GetAxis() == UIDockSplitAxis::Horizontal ?
                    UISplitterOrientation::Vertical : UISplitterOrientation::Horizontal);
                ApplyRect(*splitter, placement.Splitter);
            }
        }
        else
        {
            UIElement* pane = GetPane(placement.NodeId);
            if (pane != nullptr)
            {
                ApplyRect(*pane, placement.Bounds);
            }
        }
    }
    if (m_PreviewLeaf != 0u && m_Preview != nullptr)
    {
        for (const UIDockPlacement& placement : placements)
        {
            if (placement.NodeId == m_PreviewLeaf && placement.IsSplit == false)
            {
                ApplyRect(*m_Preview, placement.Bounds);
                break;
            }
        }
    }
}

UIDockSpaceSnapshot UIDockSpace::SaveSnapshot() const
{
    UIDockSpaceSnapshot snapshot;
    snapshot.Structure = m_Layout.SaveStructure();
    for (const UIDockLayoutRecord& record : snapshot.Structure)
    {
        if (record.Kind != UIDockNodeKind::Tabs)
        {
            continue;
        }
        const UIDockNode* leaf = m_Layout.FindNode(record.Id);
        const UITabModel* model = leaf->GetTabs();
        for (const UITabItem& tab : model->GetTabs())
        {
            snapshot.Tabs.push_back({ record.Id, tab });
        }
        snapshot.Selections.emplace_back(record.Id, model->GetSelectedTabId());
    }
    return snapshot;
}

bool UIDockSpace::RestoreSnapshot(const UIDockSpaceSnapshot& snapshot,
    const ContentFactory& factory)
{
    if (m_Panes.empty() == false || m_TabViews.empty() == false ||
        static_cast<bool>(factory) == false)
    {
        return false;
    }
    // 既存の論理Tabを持つ空Paneも復元対象にしません。失敗時の元状態を保持します。
    const UIDockSpaceSnapshot previous = SaveSnapshot();
    if (previous.Tabs.empty() == false)
    {
        return false;
    }
    UIDockLayout candidate;
    if (candidate.RestoreStructure(snapshot.Structure) == false)
    {
        return false;
    }
    // 先にTabメタデータと選択状態を検証します。失敗時は既存Treeを触りません。
    std::unordered_map<std::uint64_t, UITabModel> models;
    for (const UIDockLayoutRecord& record : snapshot.Structure)
    {
        if (record.Kind == UIDockNodeKind::Tabs)
        {
            models.emplace(record.Id, UITabModel{});
        }
    }
    if (snapshot.Selections.size() != models.size())
    {
        return false;
    }
    std::unordered_set<std::uint64_t> selectedLeaves;
    for (const UIDockTabRecord& record : snapshot.Tabs)
    {
        const auto it = models.find(record.LeafId);
        if (it == models.end() ||
            it->second.AddTab(record.Tab.Id, record.Tab.Title, record.Tab.Closable) == false)
        {
            rollback();
            return false;
        }
    }
    for (const auto& selection : snapshot.Selections)
    {
        const auto it = models.find(selection.first);
        if (it == models.end() ||
            selectedLeaves.insert(selection.first).second == false ||
            it->second.SelectTab(selection.second) == false)
        {
            return false;
        }
    }
    // Factoryはコミット前に全件実行。nullや親付きContentがあれば何も変更しません。
    std::vector<Scope<UIElement>> contents;
    contents.reserve(snapshot.Tabs.size());
    std::unordered_set<UIElement*> uniqueContents;
    for (const UIDockTabRecord& record : snapshot.Tabs)
    {
        Scope<UIElement> content = factory(record.LeafId, record.Tab);
        if (content == nullptr || content->GetParent() != nullptr ||
            content->GetContext() != nullptr ||
            uniqueContents.insert(content.get()).second == false)
        {
            return false;
        }
        contents.push_back(std::move(content));
    }
    // Widget追加後の予期しない失敗でも部分復元を残さないよう、
    // 元TreeのSnapshotを保持して作成済みPaneとSplitterを巻き戻します。
    const auto rollback = [this, &previous]()
    {
        for (const auto& pane : m_Panes)
        {
            RemoveChild(pane.second);
        }
        m_TabViews.clear();
        m_Panes.clear();
        // TabView破棄後なら、論理Modelは外部Contentを参照しません。
        m_Layout = UIDockLayout{};
        m_Layout.RestoreStructure(previous.Structure);
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        RefreshLayout();
    };
    if (RestoreStructure(snapshot.Structure) == false)
    {
        return false;
    }
    for (const UIDockLayoutRecord& record : snapshot.Structure)
    {
        if (record.Kind == UIDockNodeKind::Tabs)
        {
            if (CreateTabView(record.Id) == nullptr)
            {
                rollback();
                return false;
            }
        }
    }
    for (std::size_t index = 0u; index < snapshot.Tabs.size(); ++index)
    {
        const UIDockTabRecord& record = snapshot.Tabs[index];
        if (AddTab(record.LeafId, record.Tab.Id, record.Tab.Title,
            std::move(contents[index]), record.Tab.Closable) == false)
        {
            return false;
        }
    }
    for (const auto& selection : snapshot.Selections)
    {
        if (selection.second != 0u)
        {
            if (SelectTab(selection.first, selection.second) == false)
            {
                rollback();
                return false;
            }
        }
    }
    return true;
}

bool UIDockSpace::RestoreStructure(const std::vector<UIDockLayoutRecord>& records)
{
    if (m_Panes.empty() == false || m_TabViews.empty() == false)
    {
        return false;
    }
    UIDockLayout restored;
    if (restored.RestoreStructure(records) == false)
    {
        return false;
    }
    // 検証済みTreeだけを採用。RefreshLayoutが古いSplitterをRemoveChildし、
    // 新しいNode IDへ対応するSplitterを生成します。
    m_Layout = std::move(restored);
    m_PreviewLeaf = 0u;
    if (m_Preview != nullptr)
    {
        m_Preview->SetVisible(false);
    }
    RefreshLayout();
    return true;
}

bool UIDockSpace::CloseEmptyPane(std::uint64_t leafId)
{
    const UIDockNode* leaf = m_Layout.FindNode(leafId);
    if (leaf == nullptr || leaf->GetTabs() == nullptr ||
        leaf->GetTabs()->GetTabCount() != 0u || leaf->GetParent() == nullptr)
    {
        return false;
    }
    UITabView* view = GetTabView(leafId);
    if (view != nullptr && view->GetModel().GetTabCount() != 0u)
    {
        return false;
    }
    if (m_Layout.RemoveEmptyLeaf(leafId) == false)
    {
        return false;
    }
    // Widgetを破棄する前にraw pointerの登録を外します。RemoveChildはContextの
    // Capture/Focusを解除するため、Drag中のPane削除でも入力参照を残しません。
    m_TabViews.erase(leafId);
    const auto pane = m_Panes.find(leafId);
    if (pane != m_Panes.end())
    {
        UIElement* raw = pane->second;
        m_Panes.erase(pane);
        RemoveChild(raw);
    }
    if (m_PreviewLeaf == leafId)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
    }
    RefreshLayout();
    return true;
}

void UIDockSpace::RefreshLayout()
{
    SyncWidgets();
    ApplyLayout();
}

void UIDockSpace::OnSplitterDrag(std::uint64_t splitId, float delta)
{
    UIDockNode* node = m_Layout.FindNode(splitId);
    if (node == nullptr)
    {
        return;
    }
    const auto placements = UIDockGeometry::Calculate(m_Layout,
        UIDockRect{ 0.0f, 0.0f, GetSize().x, GetSize().y },
        m_SplitterThickness, m_MinimumPaneExtent);
    for (const UIDockPlacement& placement : placements)
    {
        if (placement.NodeId == splitId)
        {
            if (UIDockGeometry::Resize(*node, placement.Bounds, delta,
                m_SplitterThickness, m_MinimumPaneExtent) == true)
            {
                ApplyLayout();
            }
            return;
        }
    }
}

bool UIDockSpace::MoveTabToPane(std::uint64_t sourceLeafId,
    std::uint64_t targetLeafId, std::uint64_t tabId)
{
    if (sourceLeafId == targetLeafId)
    {
        return false;
    }
    UITabView* source = GetTabView(sourceLeafId);
    UITabView* target = GetTabView(targetLeafId);
    UIDockNode* sourceNode = m_Layout.FindNode(sourceLeafId);
    UIDockNode* targetNode = m_Layout.FindNode(targetLeafId);
    if (source == nullptr || target == nullptr || sourceNode == nullptr ||
        targetNode == nullptr || sourceNode->GetTabs() == nullptr ||
        targetNode->GetTabs() == nullptr ||
        target->GetModel().FindTab(tabId) != nullptr ||
        targetNode->GetTabs()->FindTab(tabId) != nullptr)
    {
        return false;
    }
    const UITabItem* item = source->GetModel().FindTab(tabId);
    if (item == nullptr || sourceNode->GetTabs()->FindTab(tabId) == nullptr)
    {
        return false;
    }
    const std::string title = item->Title;
    const bool closable = item->Closable;
    Scope<UIElement> content = source->ExtractTab(tabId);
    if (content == nullptr)
    {
        return false;
    }
    // AddTabの事前条件を検査済み。移動先追加失敗時は元のPaneへ所有権を戻す必要があるため、
    // 現状は通常の整合状態でのみ呼び出す内部移動経路とします。
    return AddTab(targetLeafId, tabId, title, std::move(content), closable);
}

std::uint64_t UIDockSpace::FindSourceLeaf(const UIElement* source) const
{
    for (const auto& entry : m_TabViews)
    {
        if (entry.second != nullptr && entry.second->GetTabBar() == source)
        {
            return entry.first;
        }
    }
    return 0u;
}

std::uint64_t UIDockSpace::FindLeafAt(const math::Vec2& local) const
{
    const auto placements = UIDockGeometry::Calculate(m_Layout,
        UIDockRect{ 0.0f, 0.0f, GetSize().x, GetSize().y },
        m_SplitterThickness, m_MinimumPaneExtent);
    for (const UIDockPlacement& placement : placements)
    {
        const UIDockRect& r = placement.Bounds;
        if (placement.IsSplit == false && local.x >= r.X && local.y >= r.Y &&
            local.x < r.X + r.Width && local.y < r.Y + r.Height &&
            GetTabView(placement.NodeId) != nullptr)
        {
            return placement.NodeId;
        }
    }
    return 0u;
}

bool UIDockSpace::OnDragDropEvent(UIDragDropEvent& event)
{
    if (event.Type == UIDragDropEventType::Leave ||
        event.Type == UIDragDropEventType::Cancel ||
        event.Type == UIDragDropEventType::End)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        return false;
    }
    if (event.Payload == nullptr || event.Payload->Type != "Raven/UITab")
    {
        return false;
    }
    const std::uint64_t sourceLeaf = FindSourceLeaf(event.Source);
    if (sourceLeaf == 0u)
    {
        return false;
    }
    std::uint64_t tabId = 0u;
    const std::string& data = event.Payload->Data;
    const auto parsed = std::from_chars(data.data(), data.data() + data.size(), tabId);
    if (parsed.ec != std::errc{} || parsed.ptr != data.data() + data.size() ||
        tabId == 0u)
    {
        return false;
    }
    math::Vec2 local;
    if (TryScreenToLocalPosition(event.ScreenPosition, local) == false)
    {
        return false;
    }
    const std::uint64_t targetLeaf = FindLeafAt(local);
    if (targetLeaf == 0u || targetLeaf == sourceLeaf ||
        GetTabView(sourceLeaf)->GetModel().FindTab(tabId) == nullptr ||
        GetTabView(targetLeaf)->GetModel().FindTab(tabId) != nullptr)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        return false;
    }
    if (event.Type == UIDragDropEventType::Over ||
        event.Type == UIDragDropEventType::Enter)
    {
        m_PreviewLeaf = targetLeaf;
        ApplyLayout();
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(true);
        }
        event.Accepted = true;
        return true;
    }
    if (event.Type == UIDragDropEventType::Drop)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        return MoveTabToPane(sourceLeaf, targetLeaf, tabId);
    }
    return false;
}

void UIDockSpace::OnBuildDrawList(UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    (void)drawList;
    (void)absolutePosition;
    // 親のArrangeで実Sizeが確定するため、外部Resize後の次frameへ配置を反映します。
    // UIElement::BuildDrawListは非virtualなので、描画中のTree追加を避け、
    // Splitter生成はSplit()/RefreshLayout()からのみ実施します。
    const_cast<UIDockSpace*>(this)->ApplyLayout();
}

} // namespace Raven
