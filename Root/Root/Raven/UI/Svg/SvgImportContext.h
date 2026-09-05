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

    math::Vec2& GetViewportSize()
    {
        return Document.ViewportSize;
    }

    void RegisterAnimation(float duration, bool loop)
    {
        MaxAnimationDuration = std::max(MaxAnimationDuration, duration);
        Document.LoopAnimation = Document.LoopAnimation || loop;
    }

    // 旧Shape ParserがVectorDocumentへ保持している共通状態を一箇所でUIDocumentへ昇格します。
    // UIDocumentへ移した後はVector側の互換フィールドを明示的に初期化し、Runtimeへ重複した状態を残しません。
    // これによりParser内部の移行期間中でも、正規化後のDocumentでは共通状態の所有者をUIDocumentへ一本化できます。
    void TakeLegacyVectorDocument(VectorDocument document)
    {
        Document.ViewportSize = document.ViewportSize;
        Document.Animation = std::move(document.Animation);
        Document.LoopAnimation = document.LoopAnimation;
        MaxAnimationDuration = Document.Animation.GetDuration();

        document.ViewportSize = {};
        document.Animation = AnimationClip{};
        document.LoopAnimation = false;
        Document.Vector = std::move(document);
    }

    void Finalize()
    {
        Document.Animation.SetDuration(MaxAnimationDuration);
    }
};

} // namespace Raven
