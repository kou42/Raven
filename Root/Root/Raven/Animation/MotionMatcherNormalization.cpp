#include "Raven/Animation/MotionMatcher.h"

namespace Raven
{

bool MotionMatcher::SetNormalizedSearchWeights(const MotionSearchWeights& semanticWeights)
{
    if (m_Database == nullptr)
    {
        return false;
    }

    MotionSearchWeights normalizedWeights{};
    if (m_Database->MakeNormalizedSearchWeights(semanticWeights, normalizedWeights) == false)
    {
        return false;
    }

    // SearchとContinuation再評価はどちらもConfigの同じWeightを使用します。
    // ここで一度だけ変換しておくことで、Global候補だけ正規化されてStay判定が別Scaleになる事故を防ぎます。
    m_Config.SearchWeights = normalizedWeights;
    return true;
}

} // namespace Raven
