#pragma once

#include "Raven/UI/Animation/UIAnimationBinding.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// ファイル形式に依存しないVectorDocumentをRaven UI Treeへ展開するWidgetです。
// ImporterとRuntimeを分離することで、SVG以外の形式も同じ描画・Animation基盤を再利用できます。
class UIVectorDocument : public UIElement
{
public:
    bool SetDocument(VectorDocument document, std::string* outError = nullptr);

    void Play();
    void Pause();
    void Stop();
    void Update(float deltaTime);

    bool IsPlaying() const { return m_Playing; }
    float GetPlaybackTime() const { return m_PlaybackTime; }
    const VectorDocument& GetDocument() const { return m_Document; }

protected:
    bool BuildRuntimeTree(std::string* outError);
    bool ApplyAnimation();

private:
    VectorDocument m_Document;
    UIAnimationBinding m_AnimationBinding;
    float m_PlaybackTime = 0.0f;
    bool m_Playing = false;
};

} // namespace Raven
