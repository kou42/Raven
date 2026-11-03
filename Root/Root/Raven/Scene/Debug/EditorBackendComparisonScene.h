#pragma once

#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneViewportRenderer.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"

#include <iosfwd>

namespace Raven
{

// Editor比較専用の固定Sceneです。通常GameのAsset/Demo設定をRuntimeへ持ち込みません。
class EditorBackendComparisonScene final : public Scene, public SceneViewportRenderer
{
public:
    // DeferredはExplicit BackendとCPU検証用。GPU生成は既存PrepareRHIMeshesへ委譲します。
    explicit EditorBackendComparisonScene(LegacyMeshResourceCreation legacyCreation);

    // Legacy MaterialとExplicit起動設定が同じRaster/Depth/Blend条件を使うための比較Profileです。
    static PipelineSpecification GetPipelineSpecification();

    void OnCreate() override;
    void OnDestroy() override;
    void OnRender() override;
    void OnEvent(Event& event) override;
    void RenderWithCamera(const Camera& camera) override;

    bool IsReady() const;
    // 起動時のEntity/Geometry/Material/Camera条件をログへ残し、Backend間で照合します。
    void WriteManifest(std::ostream& stream);

protected:
    bool ShouldUpdateSimulation() const override;

private:
    LegacyMeshResourceCreation m_LegacyCreation;
    bool m_Ready = false;
    float m_ViewportWidth = 1920.0f;
    float m_ViewportHeight = 1080.0f;
};

} // namespace Raven
