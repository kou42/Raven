#pragma once

#include "Raven/UI/Document/UIDocument.h"

#include <algorithm>
#include <utility>

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

    // 旧Shape ParserがVectorDocumentへ保持している共通状態を一箇所でUIDocumentへ昇格します。
    // Parser移行中だけ必要な互換処理なので、呼び出し側へViewport/Animationの重複構造を漏らしません。
    void TakeLegacyVectorDocument(VectorDocument document)
    {
        Document.ViewportSize = document.ViewportSize;
        Document.Animation = std::move(document.Animation);
        Document.LoopAnimation = document.LoopAnimation;
        Document.Vector = std::move(document);
        MaxAnimationDuration = Document.Animation.GetDuration();
    }

    void Finalize()
    {
        Document.Animation.SetDuration(MaxAnimationDuration);
    }
};

} // namespace Raven
