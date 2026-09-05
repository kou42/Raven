#pragma once

#include "Raven/UI/Document/UIDocument.h"

#include <algorithm>

namespace Raven
{

// SVG解析中に必要な共通Document状態をまとめる内部Contextです。
// Vector形状とViewport/Animationを明示的に分離し、VectorDocumentへ共通メタデータを持たせない設計へ移行します。
struct SvgImportContext
{
    UIDocument Document;
    float MaxAnimationDuration = 0.0f;

    VectorDocument& GetVectorDocument()
    {
        return Document.Vector;
    }

    AnimationClip& GetAnimation()
    {
        return Document.Animation;
    }

    void RegisterAnimation(float duration, bool loop)
    {
        MaxAnimationDuration = std::max(MaxAnimationDuration, duration);
        Document.LoopAnimation = Document.LoopAnimation || loop;
    }

    void Finalize()
    {
        Document.Animation.SetDuration(MaxAnimationDuration);
    }
};

} // namespace Raven
