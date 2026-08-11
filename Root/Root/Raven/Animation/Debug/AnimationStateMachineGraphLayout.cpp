// Raven/Animation/Debug/AnimationStateMachineGraphLayout.cpp
#include "Raven/Animation/Debug/AnimationStateMachineGraphLayout.h"

namespace Raven
{

std::vector<AnimationStateMachineGraphNodeLayout> BuildAnimationStateMachineGraphLayout(
    std::size_t nodeCount,
    float originX,
    float originY,
    float nodeWidth,
    float nodeHeight,
    float horizontalGap,
    float verticalGap)
{
    std::vector<AnimationStateMachineGraphNodeLayout> result;
    result.reserve(nodeCount);

    // Bootstrap Overlayでは複雑なGraph最適化を行わず、2列Gridで安定配置します。
    // State追加時にも既存Nodeが大きく跳びにくく、Transition線とRuntime強調の確認に集中できます。
    constexpr std::size_t columnCount = 2;

    for (std::size_t i = 0; i < nodeCount; ++i)
    {
        const std::size_t column = i % columnCount;
        const std::size_t row = i / columnCount;

        AnimationStateMachineGraphNodeLayout layout{};
        layout.NodeIndex = i;
        layout.X = originX + static_cast<float>(column) * (nodeWidth + horizontalGap);
        layout.Y = originY + static_cast<float>(row) * (nodeHeight + verticalGap);
        layout.Width = nodeWidth;
        layout.Height = nodeHeight;
        result.push_back(layout);
    }

    return result;
}

} // namespace Raven
