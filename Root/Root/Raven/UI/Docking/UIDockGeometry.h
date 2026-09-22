#pragma once

#include "Raven/UI/Docking/UIDockLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Raven
{

// Dock座標はUIElementと同じ論理pixelです。Rectは右端・下端を含まない領域です。
struct UIDockRect
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

struct UIDockPlacement
{
    std::uint64_t NodeId = 0u;
    UIDockRect Bounds;
    UIDockRect Splitter; // Tabs Leafでは空Rectです。
    bool IsSplit = false;
};

// Dock Treeの配置計算だけを担当し、UIElementのMeasure/Arrangeと分離します。
// 後続のUIDockSpaceは結果をUITabViewとUISplitterへ適用できます。
class UIDockGeometry final
{
public:
    static std::vector<UIDockPlacement> Calculate(const UIDockLayout& layout,
        const UIDockRect& bounds, float splitterThickness = 5.0f,
        float minimumPaneExtent = 32.0f)
    {
        std::vector<UIDockPlacement> result;
        if (ValidRect(bounds) == false || std::isfinite(splitterThickness) == false ||
            std::isfinite(minimumPaneExtent) == false ||
            splitterThickness < 0.0f || minimumPaneExtent < 0.0f)
        {
            return result;
        }
        CalculateNode(layout.GetRoot(), bounds, splitterThickness, minimumPaneExtent, result);
        return result;
    }

    // Mouse Dragの論理pixel差分をSplit比率へ変換します。
    // 子Paneを両方minimumPaneExtent以上にできない場合は比率を変更しません。
    static bool Resize(UIDockNode& node, const UIDockRect& bounds, float delta,
        float splitterThickness = 5.0f, float minimumPaneExtent = 32.0f)
    {
        if (node.GetKind() != UIDockNodeKind::Split || ValidRect(bounds) == false ||
            std::isfinite(delta) == false || std::isfinite(splitterThickness) == false ||
            std::isfinite(minimumPaneExtent) == false ||
            splitterThickness < 0.0f || minimumPaneExtent < 0.0f)
        {
            return false;
        }
        const float extent = node.GetAxis() == UIDockSplitAxis::Horizontal ?
            bounds.Width : bounds.Height;
        const float available = extent - splitterThickness;
        if (available <= 0.0f || available < 2.0f * minimumPaneExtent)
        {
            return false;
        }
        const float lower = std::max(minimumPaneExtent / available, 0.000001f);
        const float upper = std::min(1.0f - minimumPaneExtent / available, 0.999999f);
        if (lower > upper)
        {
            return false;
        }
        return node.SetSplitRatio(std::clamp(node.GetSplitRatio() + delta / available, lower, upper));
    }

private:
    static bool ValidRect(const UIDockRect& rect)
    {
        return std::isfinite(rect.X) && std::isfinite(rect.Y) &&
            std::isfinite(rect.Width) && std::isfinite(rect.Height) &&
            rect.Width >= 0.0f && rect.Height >= 0.0f;
    }

    static void CalculateNode(const UIDockNode* node, const UIDockRect& bounds,
        float thickness, float minimum, std::vector<UIDockPlacement>& out)
    {
        if (node == nullptr)
        {
            return;
        }
        UIDockPlacement placement;
        placement.NodeId = node->GetId();
        placement.Bounds = bounds;
        placement.IsSplit = node->GetKind() == UIDockNodeKind::Split;
        if (placement.IsSplit == false)
        {
            out.push_back(placement);
            return;
        }

        const bool horizontal = node->GetAxis() == UIDockSplitAxis::Horizontal;
        const float extent = horizontal ? bounds.Width : bounds.Height;
        const float handle = std::min(thickness, extent);
        const float available = extent - handle;
        // 狭いViewportでは負のPane寸法を作らず、比率を維持して縮退させます。
        const float minExtent = std::min(minimum, available * 0.5f);
        const float firstExtent = std::clamp(available * node->GetSplitRatio(),
            minExtent, available - minExtent);
        const float secondExtent = available - firstExtent;
        UIDockRect first = bounds;
        UIDockRect second = bounds;
        if (horizontal == true)
        {
            first.Width = firstExtent;
            placement.Splitter = { bounds.X + firstExtent, bounds.Y, handle, bounds.Height };
            second.X = bounds.X + firstExtent + handle;
            second.Width = secondExtent;
        }
        else
        {
            first.Height = firstExtent;
            placement.Splitter = { bounds.X, bounds.Y + firstExtent, bounds.Width, handle };
            second.Y = bounds.Y + firstExtent + handle;
            second.Height = secondExtent;
        }
        out.push_back(placement);
        CalculateNode(node->GetFirst(), first, thickness, minimum, out);
        CalculateNode(node->GetSecond(), second, thickness, minimum, out);
    }
};

} // namespace Raven
