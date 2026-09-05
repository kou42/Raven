#pragma once

#include "Raven/UI/Animation/UIAnimationBinding.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Document/UIDocument.h"

#include <string>

namespace Raven
{

// ファイル形式に依存しないUIDocument内のVector表現をRaven UI Treeへ展開するWidgetです。
// Viewport/AnimationはUIDocument、図形データはVectorDocumentから参照し、責務を分離します。
class UIVectorDocument : public UIElement
{
public:
    bool SetDocument(UIDocument document, std::string* outError = nullptr);

    void Play();
    void Pause();
    void Stop();
    void Update(float deltaTime);

    bool IsPlaying() const { return m_Playing; }
    float GetPlaybackTime() const { return m_PlaybackTime; }
    const UIDocument& GetDocument() const { return m_Document; }
    const VectorDocument& GetVectorDocument() const { return m_Document.Vector; }

protected:
    bool BuildRuntimeTree(std::string* outError);
    bool ApplyAnimation();

private:
    UIDocument m_Document;
    UIAnimationBinding m_AnimationBinding;
    float m_PlaybackTime = 0.0f;
    bool m_Playing = false;
};

} // namespace Raven
