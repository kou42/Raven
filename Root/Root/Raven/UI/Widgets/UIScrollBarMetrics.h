#pragma once

#include <algorithm>
#include <cmath>

namespace Raven
{

// TreeView/Tableで共通の縦Scrollbar幾何計算です。
// 座標はScrollbar Trackの先頭を0としたLocal座標で、描画とHit Testの双方に使用します。
struct UIScrollBarMetrics
{
    float Viewport = 0.0f;
    float Content = 0.0f;
    float Offset = 0.0f;
    float MinimumThumb = 20.0f;

    float MaxOffset() const { return std::max(0.0f, Content - Viewport); }
    bool IsVisible() const { return Viewport > 0.0f && MaxOffset() > 0.0f; }

    float ThumbLength() const
    {
        if (Viewport <= 0.0f || Content <= 0.0f)
        {
            return 0.0f;
        }
        return std::min(Viewport, std::max(MinimumThumb, Viewport * Viewport / Content));
    }

    float ThumbStart() const
    {
        const float maximum = MaxOffset();
        if (maximum <= 0.0f)
        {
            return 0.0f;
        }
        return std::clamp(Offset, 0.0f, maximum) / maximum *
            std::max(0.0f, Viewport - ThumbLength());
    }

    float OffsetFromThumbStart(float start) const
    {
        const float travel = Viewport - ThumbLength();
        if (travel <= 0.0f)
        {
            return 0.0f;
        }
        return std::clamp(start / travel, 0.0f, 1.0f) * MaxOffset();
    }
};

} // namespace Raven
