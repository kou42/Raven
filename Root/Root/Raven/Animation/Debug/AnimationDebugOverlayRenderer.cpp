// Raven/Animation/Debug/AnimationDebugOverlayRenderer.cpp
#include "Raven/Animation/Debug/AnimationDebugOverlayRenderer.h"

#include "Raven/Animation/AnimationRuntimeDebug.h"
#include "Raven/Animation/Debug/AnimationStateMachineGraphLayout.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Physics/Debug/PhysicsDebugOverlayFont.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <glad/glad.h>

namespace Raven
{
namespace
{
std::string FormatFloat(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string StateNameOrNone(const AnimatorStateRuntimeDebugInfo& state)
{
    return state.HasState ? state.StateName : "-";
}

const char* ConditionOperatorText(AnimatorConditionOperator conditionOperator)
{
    switch (conditionOperator)
    {
    case AnimatorConditionOperator::Equal:        return "==";
    case AnimatorConditionOperator::NotEqual:     return "!=";
    case AnimatorConditionOperator::Greater:      return ">";
    case AnimatorConditionOperator::GreaterEqual: return ">=";
    case AnimatorConditionOperator::Less:         return "<";
    case AnimatorConditionOperator::LessEqual:    return "<=";
    }
    return "?";
}

std::string FormatCondition(const AnimatorConditionRuntimeDebugInfo& condition)
{
    std::string text = condition.ParameterName + " " + ConditionOperatorText(condition.Operator) + " ";
    if (condition.IsFloat)
    {
        text += FormatFloat(condition.ExpectedFloat);
        text += "  ACT=" + FormatFloat(condition.ActualFloat);
    }
    else
    {
        text += condition.ExpectedBool ? "true" : "false";
        text += std::string("  ACT=") + (condition.ActualBool ? "true" : "false");
    }
    text += condition.IsMet ? "  [OK]" : "  [NG]";
    return text;
}

const AnimationStateMachineGraphNodeLayout* FindNodeLayout(
    const std::vector<AnimationStateMachineGraphNodeLayout>& layouts,
    const std::vector<AnimatorStateMachineNodeRuntimeDebugInfo>& nodes,
    const std::string& stateName)
{
    for (const auto& layout : layouts)
    {
        if (layout.NodeIndex < nodes.size() && nodes[layout.NodeIndex].StateName == stateName)
        {
            return &layout;
        }
    }
    return nullptr;
}
} // namespace

AnimationDebugOverlayRenderer::AnimationDebugOverlayRenderer(Scene& scene)
    : m_Scene(&scene)
{
    Registry().push_back(this);
}

AnimationDebugOverlayRenderer::~AnimationDebugOverlayRenderer()
{
    auto& registry = Registry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
}

std::vector<AnimationDebugOverlayRenderer*>& AnimationDebugOverlayRenderer::Registry()
{
    static std::vector<AnimationDebugOverlayRenderer*> registry;
    return registry;
}

void AnimationDebugOverlayRenderer::RenderRegistered()
{
    for (AnimationDebugOverlayRenderer* renderer : Registry())
    {
        if (renderer != nullptr)
        {
            renderer->Render();
        }
    }
}

void AnimationDebugOverlayRenderer::EnsureInitialized()
{
    if (m_Material != nullptr)
    {
        return;
    }

    Ref<Shader> shader = Shader::Create(
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");
    if (shader == nullptr)
    {
        return;
    }

    PipelineSpecification specification{};
    specification.DebugName = "Animation Debug Overlay Pipeline";
    specification.Shader = shader;
    specification.Topology = PrimitiveTopology::Lines;
    specification.Cull = CullMode::None;
    specification.DepthTest = false;
    specification.DepthWrite = false;
    specification.DepthCompare = DepthCompareOperator::LessEqual;
    specification.Blend = true;
    m_Material = CreateRef<Material>(Pipeline::Create(specification));
}

void AnimationDebugOverlayRenderer::UpdateToggleKey()
{
    const bool pressed = Input::IsKeyPressed(Key::Y);
    if (pressed && m_WasToggleKeyPressed == false)
    {
        m_Visible = m_Visible == false;
    }
    m_WasToggleKeyPressed = pressed;
}

void AnimationDebugOverlayRenderer::Render()
{
    UpdateToggleKey();
    if (m_Visible == false || m_Scene == nullptr)
    {
        return;
    }

    const AnimatorStateMachine* stateMachine = nullptr;
    for (auto [entity, animatorComponent] : m_Scene->View<AnimatorComponent>())
    {
        static_cast<void>(entity);
        if (animatorComponent.Enabled && animatorComponent.StateMachine != nullptr)
        {
            stateMachine = animatorComponent.StateMachine.get();
            break;
        }
    }
    if (stateMachine == nullptr)
    {
        return;
    }

    AnimatorStateMachineRuntimeDebugInfo runtime{};
    if (BuildAnimatorStateMachineRuntimeDebugInfo(*stateMachine, runtime) == false)
    {
        return;
    }

    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
    {
        return;
    }

    EnsureInitialized();
    if (m_Material == nullptr)
    {
        return;
    }

    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;

    const math::Vec3 titleColor{ 1.0f, 0.75f, 0.25f };
    const math::Vec3 textColor{ 0.92f, 0.92f, 0.92f };
    const math::Vec3 activeColor{ 0.35f, 1.0f, 0.45f };
    const math::Vec3 pendingColor{ 0.45f, 0.75f, 1.0f };
    const math::Vec3 queuedColor{ 1.0f, 0.55f, 0.25f };
    const math::Vec3 dimColor{ 0.48f, 0.48f, 0.48f };

    const float panelX = std::max(16.0f, float(viewport[2]) - 560.0f);
    float y = 16.0f;

    auto toNdc = [&](float x, float py)
    {
        return math::Vec3{
            (x / float(viewport[2])) * 2.0f - 1.0f,
            1.0f - (py / float(viewport[3])) * 2.0f,
            0.0f
        };
    };

    auto addText = [&](const std::string& text, const math::Vec3& color)
    {
        AddOverlayText(vertices, indices, text, panelX, y, 2.0f,
            viewport[2], viewport[3], color);
        y += 18.0f;
    };

    auto addRect = [&](float x, float py, float width, float height, const math::Vec3& color)
    {
        AddLine(vertices, indices, toNdc(x, py), toNdc(x + width, py), color);
        AddLine(vertices, indices, toNdc(x + width, py), toNdc(x + width, py + height), color);
        AddLine(vertices, indices, toNdc(x + width, py + height), toNdc(x, py + height), color);
        AddLine(vertices, indices, toNdc(x, py + height), toNdc(x, py), color);
    };

    addText("ANIMATION DEBUG", titleColor);
    addText("[Y] HIDE OVERLAY", dimColor);
    y += 4.0f;
    addText("CURRENT: " + StateNameOrNone(runtime.Current), activeColor);
    addText("PENDING: " + StateNameOrNone(runtime.Pending), runtime.Pending.HasState ? pendingColor : dimColor);
    addText("QUEUED: " + (runtime.QueuedStateName.empty() ? std::string("-") : runtime.QueuedStateName), dimColor);
    addText("NORMALIZED: " + FormatFloat(runtime.Current.NormalizedTime), textColor);
    addText("CROSSFADE: " + FormatFloat(runtime.CrossFadeWeight), runtime.IsCrossFading ? pendingColor : dimColor);

    // ========================================================================
    // State Machine Graph
    // ========================================================================
    // Transition線を先に描き、その上へNodeを描くことでNode枠とState名の視認性を保ちます。
    // Graph描画はRuntime Snapshotだけを参照し、StateMachine内部定義へ直接アクセスしません。
    y += 8.0f;
    addText("STATE GRAPH", titleColor);

    const float nodeWidth = 210.0f;
    const float nodeHeight = 48.0f;
    const float horizontalGap = 28.0f;
    const float verticalGap = 22.0f;
    const auto layouts = BuildAnimationStateMachineGraphLayout(
        runtime.Nodes.size(), panelX, y, nodeWidth, nodeHeight, horizontalGap, verticalGap);

    for (const auto& transition : runtime.Transitions)
    {
        const auto* from = FindNodeLayout(layouts, runtime.Nodes, transition.FromState);
        const auto* to = FindNodeLayout(layouts, runtime.Nodes, transition.ToState);
        if (from == nullptr || to == nullptr)
        {
            continue;
        }

        // Graph上ではTransitionの段階を色で追えるようにします。
        // ACTIVE=CrossFade進行中(青)、SELECTED=このFrameに選択される候補(橙)、ELIGIBLE=成立済みだが未選択(緑)です。
        // 複数Transitionが同時成立しても、SELECTEDだけを別色にすることでPriority競合を視覚的に確認できます。
        math::Vec3 lineColor = dimColor;
        if (transition.IsActive)
        {
            lineColor = pendingColor;
        }
        else if (transition.IsSelectedCandidate)
        {
            lineColor = queuedColor;
        }
        else if (transition.IsEligible)
        {
            lineColor = activeColor;
        }

        const float fromX = from->X + from->Width * 0.5f;
        const float fromY = from->Y + from->Height * 0.5f;
        const float toX = to->X + to->Width * 0.5f;
        const float toY = to->Y + to->Height * 0.5f;
        AddLine(vertices, indices, toNdc(fromX, fromY), toNdc(toX, toY), lineColor);

        // 終点直前へ小さなV字を置き、Transition方向を識別できるようにします。
        const float dx = toX - fromX;
        const float dy = toY - fromY;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 1.0f)
        {
            const float ux = dx / length;
            const float uy = dy / length;
            const float px = -uy;
            const float py = ux;
            const float arrowX = toX - ux * 18.0f;
            const float arrowY = toY - uy * 18.0f;
            AddLine(vertices, indices, toNdc(arrowX, arrowY),
                toNdc(arrowX - ux * 8.0f + px * 5.0f, arrowY - uy * 8.0f + py * 5.0f), lineColor);
            AddLine(vertices, indices, toNdc(arrowX, arrowY),
                toNdc(arrowX - ux * 8.0f - px * 5.0f, arrowY - uy * 8.0f - py * 5.0f), lineColor);
        }
    }

    for (const auto& layout : layouts)
    {
        if (layout.NodeIndex >= runtime.Nodes.size())
        {
            continue;
        }

        const auto& node = runtime.Nodes[layout.NodeIndex];
        math::Vec3 nodeColor = dimColor;
        if (node.IsCurrent)
        {
            nodeColor = activeColor;
        }
        else if (node.IsPending)
        {
            nodeColor = pendingColor;
        }
        else if (node.IsQueued)
        {
            nodeColor = queuedColor;
        }

        addRect(layout.X, layout.Y, layout.Width, layout.Height, nodeColor);
        AddOverlayText(vertices, indices, node.StateName,
            layout.X + 8.0f, layout.Y + 8.0f, 1.6f,
            viewport[2], viewport[3], nodeColor);

        if (node.IsBlendTree)
        {
            AddOverlayText(vertices, indices, "1D BLEND TREE",
                layout.X + 8.0f, layout.Y + 28.0f, 1.25f,
                viewport[2], viewport[3], textColor);
        }
    }

    const std::size_t rowCount = (runtime.Nodes.size() + 1) / 2;
    y += static_cast<float>(rowCount) * (nodeHeight + verticalGap) + 8.0f;

    // ========================================================================
    // Transition Conditions
    // ========================================================================
    // Graph全体の全Transitionを常時展開すると情報量が増えすぎるため、Current Stateから出るTransitionだけを表示します。
    // 条件ごとの実値・期待値・成立状態に加え、Exit Timeを独立表示することで「なぜ発火しないか」を追跡できます。
    bool hasCurrentTransition = false;
    for (const auto& transition : runtime.Transitions)
    {
        if (transition.IsSourceCurrent == false)
        {
            continue;
        }

        if (hasCurrentTransition == false)
        {
            addText("TRANSITION CONDITIONS", titleColor);
            hasCurrentTransition = true;
        }

        // 表示上もACTIVE / SELECTED / ELIGIBLEを別状態として扱います。
        // SELECTEDは「条件成立」だけではなくPriority比較まで通過した1本なので、複数候補時の選択理由を確認する中心情報になります。
        math::Vec3 transitionColor = textColor;
        std::string transitionStatus;
        if (transition.IsActive)
        {
            transitionColor = pendingColor;
            transitionStatus = " [ACTIVE]";
        }
        else if (transition.IsSelectedCandidate)
        {
            transitionColor = queuedColor;
            transitionStatus = " [SELECTED]";
        }
        else if (transition.IsEligible)
        {
            transitionColor = activeColor;
            transitionStatus = " [ELIGIBLE]";
        }

        AddOverlayText(vertices, indices,
            transition.FromState + " -> " + transition.ToState + transitionStatus,
            panelX, y, 1.6f, viewport[2], viewport[3], transitionColor);
        y += 17.0f;

        // PriorityとCrossFade DurationはTransition選択・発火後の挙動を理解するための定義値です。
        // 条件が同時成立した場合はPriorityを比較し、選択後はFade時間を確認できるよう同じ診断ブロックに表示します。
        const std::string transitionDefinition = "PRIORITY: " + std::to_string(transition.Priority)
            + "  FADE: " + FormatFloat(transition.CrossFadeDuration);
        AddOverlayText(vertices, indices, transitionDefinition,
            panelX + 12.0f, y, 1.25f, viewport[2], viewport[3], transitionColor);
        y += 15.0f;

        for (const auto& condition : transition.Conditions)
        {
            const math::Vec3 conditionColor = condition.IsMet ? activeColor : dimColor;
            AddOverlayText(vertices, indices, FormatCondition(condition),
                panelX + 12.0f, y, 1.35f, viewport[2], viewport[3], conditionColor);
            y += 15.0f;
        }

        if (transition.HasExitTime)
        {
            const std::string exitText = "EXIT "
                + FormatFloat(transition.SourceNormalizedTime)
                + " / " + FormatFloat(transition.ExitTime)
                + (transition.IsExitTimeMet ? "  [OK]" : "  [NG]");
            const math::Vec3 exitColor = transition.IsExitTimeMet ? activeColor : dimColor;
            AddOverlayText(vertices, indices, exitText,
                panelX + 12.0f, y, 1.35f, viewport[2], viewport[3], exitColor);
            y += 15.0f;
        }

        // Conditionを持たないTransitionも診断可能にするため、AND結果を要約表示します。
        // SELECTEDを独立表示することで、EligibleなのにPriority競合で選ばれなかったTransitionも判別できます。
        const std::string summary = std::string("CONDITIONS: ")
            + (transition.AreConditionsMet ? "OK" : "NG")
            + "  ELIGIBLE: " + (transition.IsEligible ? "YES" : "NO")
            + "  SELECTED: " + (transition.IsSelectedCandidate ? "YES" : "NO");
        AddOverlayText(vertices, indices, summary,
            panelX + 12.0f, y, 1.25f, viewport[2], viewport[3], transitionColor);
        y += 20.0f;
    }

    if (hasCurrentTransition)
    {
        y += 4.0f;
    }

    // CurrentがClipでPendingがBlend Treeの場合も遷移先の詳細を確認できるようにします。
    const AnimatorStateRuntimeDebugInfo* blendState = nullptr;
    if (runtime.Current.IsBlendTree && runtime.Current.BlendChildren.empty() == false)
    {
        blendState = &runtime.Current;
    }
    else if (runtime.Pending.IsBlendTree && runtime.Pending.BlendChildren.empty() == false)
    {
        blendState = &runtime.Pending;
    }

    if (blendState == nullptr)
    {
        SubmitLines(vertices, indices);
        return;
    }

    addText("BLEND TREE 1D", titleColor);
    addText(blendState->BlendParameterName + ": " + FormatFloat(blendState->BlendParameterValue), activeColor);

    const auto& children = blendState->BlendChildren;
    const float minThreshold = children.front().Threshold;
    const float maxThreshold = children.back().Threshold;
    const float thresholdRange = maxThreshold - minThreshold;
    const float axisLeft = panelX;
    const float axisRight = panelX + 450.0f;
    const float axisY = y + 8.0f;

    auto thresholdToX = [&](float threshold)
    {
        if (thresholdRange <= 0.0f)
        {
            return axisLeft;
        }
        const float t = std::clamp((threshold - minThreshold) / thresholdRange, 0.0f, 1.0f);
        return axisLeft + (axisRight - axisLeft) * t;
    };

    AddLine(vertices, indices, toNdc(axisLeft, axisY), toNdc(axisRight, axisY), dimColor);
    for (const auto& child : children)
    {
        const float x = thresholdToX(child.Threshold);
        const math::Vec3 childColor = child.Weight > 0.0f ? activeColor : dimColor;
        AddLine(vertices, indices, toNdc(x, axisY - 7.0f), toNdc(x, axisY + 7.0f), childColor);
        AddOverlayText(vertices, indices, FormatFloat(child.Threshold), x - 18.0f, axisY + 11.0f,
            1.35f, viewport[2], viewport[3], textColor);
    }

    const float parameterX = thresholdToX(blendState->BlendParameterValue);
    AddLine(vertices, indices, toNdc(parameterX, axisY - 14.0f), toNdc(parameterX, axisY + 14.0f), titleColor);
    y = axisY + 38.0f;

    for (const auto& child : children)
    {
        const std::string label = "CHILD " + std::to_string(child.ChildIndex)
            + " T=" + FormatFloat(child.Threshold)
            + " W=" + FormatFloat(child.Weight);
        const math::Vec3 childColor = child.Weight > 0.0f ? activeColor : dimColor;
        AddOverlayText(vertices, indices, label, panelX, y, 1.5f,
            viewport[2], viewport[3], childColor);

        const float barY = y + 14.0f;
        const float barWidth = 180.0f;
        AddLine(vertices, indices, toNdc(panelX, barY), toNdc(panelX + barWidth, barY), dimColor);
        AddLine(vertices, indices, toNdc(panelX, barY - 2.0f),
            toNdc(panelX + barWidth * std::clamp(child.Weight, 0.0f, 1.0f), barY - 2.0f), childColor);
        y += 28.0f;
    }

    SubmitLines(vertices, indices);
}

void AnimationDebugOverlayRenderer::SubmitLines(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices)
{
    if (vertices.empty() || indices.empty() || m_Material == nullptr)
    {
        return;
    }

    Ref<VertexArray> vertexArray = VertexArray::Create();
    Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create(
        reinterpret_cast<float*>(vertices.data()),
        uint32_t(vertices.size() * sizeof(DebugVertex)));
    vertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(), uint32_t(indices.size())));

    Ref<Mesh> mesh = CreateRef<Mesh>(vertexArray, int32_t(indices.size()));
    m_Material->SetUniform("u_View", math::Mat4::Identity());
    m_Material->SetUniform("u_Projection", math::Mat4::Identity());
    m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_Material->SetUniform("u_Alpha", 1.0f);
    Renderer::Draw(mesh, m_Material, math::Mat4::Identity());
}

void AnimationDebugOverlayRenderer::AddLine(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Vec3& start,
    const math::Vec3& end,
    const math::Vec3& color)
{
    const uint32_t base = uint32_t(vertices.size());
    vertices.push_back({ start, color, {} });
    vertices.push_back({ end, color, {} });
    indices.push_back(base);
    indices.push_back(base + 1);
}

void AnimationDebugOverlayRenderer::AddOverlayText(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const std::string& text,
    float px,
    float py,
    float scale,
    int width,
    int height,
    const math::Vec3& color)
{
    if (width <= 0 || height <= 0 || scale <= 0.0f)
    {
        return;
    }

    auto toNdc = [&](float x, float y)
    {
        return math::Vec3{
            (x / float(width)) * 2.0f - 1.0f,
            1.0f - (y / float(height)) * 2.0f,
            0.0f
        };
    };

    float cursor = px;
    for (char ch : text)
    {
        const auto glyph = ph::detail::GetPhysicsDebugGlyph(ch);
        for (int row = 0; row < 7; ++row)
        {
            int col = 0;
            while (col < 5)
            {
                const uint8_t mask = uint8_t(1u << (4 - col));
                if ((glyph[row] & mask) == 0)
                {
                    ++col;
                    continue;
                }

                const int start = col;
                while (col < 5 && (glyph[row] & uint8_t(1u << (4 - col))) != 0)
                {
                    ++col;
                }

                AddLine(vertices, indices,
                    toNdc(cursor + float(start) * scale, py + float(row) * scale),
                    toNdc(cursor + float(col) * scale, py + float(row) * scale),
                    color);
            }
        }
        cursor += 6.0f * scale;
    }
}

} // namespace Raven
