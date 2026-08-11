// Raven/Animation/Debug/AnimationStateMachineGraphLayout.h
#pragma once

#include <cstddef>
#include <vector>

namespace Raven
{

// ============================================================================
// AnimationStateMachineGraphLayout
// ============================================================================
// Runtime SnapshotのState順序を画面上の2列Graphへ変換するための軽量レイアウト情報です。
// Dear ImGui等のEditor UIへ移行した後も、Node配置アルゴリズムの検証用として再利用できます。
struct AnimationStateMachineGraphNodeLayout
{
    std::size_t NodeIndex = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

// Node数に応じて2列Gridへ安定配置します。
// State名やAnimation種類には依存せず、Snapshot順序だけから決定するため、
// Runtime/EditorのState追加で描画コードへ個別分岐を増やさずに済みます。
std::vector<AnimationStateMachineGraphNodeLayout> BuildAnimationStateMachineGraphLayout(
    std::size_t nodeCount,
    float originX,
    float originY,
    float nodeWidth,
    float nodeHeight,
    float horizontalGap,
    float verticalGap);

} // namespace Raven
