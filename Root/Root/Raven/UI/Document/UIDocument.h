#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Document/VectorDocument.h"

namespace Raven
{

// Import元のファイル形式や表現種別に依存しない、Raven UIの共通Documentです。
// Vector以外のImage/Text等を将来追加する場合も、ImporterとRuntimeの境界はこの型で維持します。
// 個々の表現データは専用Documentへ分離し、この型自体を巨大な万能データ構造にしない方針です。
struct UIDocument
{
    math::Vec2 ViewportSize{};
    VectorDocument Vector;
    AnimationClip Animation;
    bool LoopAnimation = false;
};

} // namespace Raven
