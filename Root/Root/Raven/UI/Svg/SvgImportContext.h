#pragma once

#include "Raven/UI/Document/UIDocument.h"

#include <algorithm>

namespace Raven
{

// SVG解析中に必要な共通Document状態をまとめる内部Contextです。
// Vector形状はVectorDocument、Viewport/Animation/LoopはUIDocumentへ直接書き込み、所有者を明確に分離します。
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

    math::Vec2& GetViewportSize()
    {
        return Document.ViewportSize;
    }

    // 個々のanimate要素からduration/loop情報を集約し、最終的なUIDocumentの再生設定へ反映します。
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
