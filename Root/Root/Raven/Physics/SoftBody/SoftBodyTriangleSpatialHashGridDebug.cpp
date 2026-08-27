#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

#include <algorithm>
#include <cstdint>

namespace Raven
{
namespace ph
{

void SoftBodyTriangleSpatialHashGrid::CollectActiveCellDebugInfo(
    std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& outCells) const
{
    // ========================================================================
    // Debug Snapshot only - not part of Broad Phase hot path
    // ========================================================================
    // Open Addressingの物理Bucket順は可視化側に意味がないため、現GenerationのActive Cellだけを
    // 値型へコピーします。Generation比較をここへ閉じ込めることで、Debug ViewerがHash内部実装へ
    // 依存せず、将来Bucket配置方式を変更してもSnapshot契約を維持できます。
    //
    // この処理を通常のSpatial Hash実装本体から別Translation Unitへ分離している理由は、
    // Debug Viewer追加によって既存Broad Phaseの計測処理・コメント・Hot Pathを不用意に変更しないためです。
    // Browser Debug Viewerから低頻度で呼ばれる場合だけActive Bucket全体を走査します。
    outCells.clear();
    outCells.reserve(m_ActiveCellCount);

    for (const TriangleCellBucket& bucket : m_Buckets)
    {
        if (bucket.Generation != m_CurrentGeneration)
        {
            continue;
        }

        SoftBodyTriangleSpatialHashCellDebugInfo info{};
        info.X = bucket.Coord.X;
        info.Y = bucket.Coord.Y;
        info.Z = bucket.Coord.Z;
        info.TriangleCount = static_cast<uint32_t>(std::min<std::size_t>(
            bucket.TriangleIndices.Count,
            static_cast<std::size_t>(UINT32_MAX)));
        outCells.push_back(info);
    }
}

} // namespace ph
} // namespace Raven
