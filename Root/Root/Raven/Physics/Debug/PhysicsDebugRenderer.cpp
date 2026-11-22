#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "Raven/Core/Input.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/Debug/PhysicsDebugOverlayFont.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"
#include "Raven/Physics/Debug/PhysicsFieldDebugDraw.h"
#include "Raven/Physics/Debug/PhysicsFieldDebugAdapters.h"
#include "Raven/Physics/Field/SparseSDF.h"
#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Thermal/TemperatureFieldRegistry.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
std::string OnOff(bool enabled)
{
    return enabled ? "ON" : "OFF";
}

std::string FormatFloat(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

class TemperatureSliceField final : public ScalarField
{
public:
    TemperatureSliceField(const TemperatureField& field, bool showInfluence)
        : m_Field(field), m_ShowInfluence(showInfluence)
    {
    }

    float Evaluate(const math::Vec3& position) const override
    {
        const float influence = m_Field.EvaluateInfluence(position);
        // 領域外の局所 Field は Registry の合成にも寄与しないため、サンプル表示から除外します。
        if (std::isfinite(influence) == false || influence <= 0.0f)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return m_ShowInfluence ? influence : m_Field.Evaluate(position);
    }

private:
    const TemperatureField& m_Field;
    bool m_ShowInfluence;
};

} // namespace

PhysicsDebugRenderer::PhysicsDebugRenderer(Scene& scene)
    : m_Scene(&scene)
{
    Registry().push_back(this);
}

PhysicsDebugRenderer::~PhysicsDebugRenderer()
{
    auto& registry = Registry();
    registry.erase(
        std::remove(registry.begin(), registry.end(), this),
        registry.end());
}

void PhysicsDebugRenderer::RenderRegistered(const Scene* renderedScene)
{
    for (auto* renderer : Registry())
    {
        if (renderer != nullptr)
        {
            // Field の取得先を現在のカメラを設定した Scene に限定します。
            renderer->Render(renderer->m_Scene == renderedScene);
        }
    }
}

void PhysicsDebugRenderer::SetPointGravityField(const PointGravityField* field)
{
    m_PointGravityField = field;
}

void PhysicsDebugRenderer::SetDenseSDFSources(const DenseSDFSource* signedSource, const DenseSDFSource* unsignedSource)
{
    m_DenseSignedSDF = signedSource;
    m_DenseUnsignedSDF = unsignedSource;
}

void PhysicsDebugRenderer::SetSparseSDFSnapshot(Ref<const SparseSDFSnapshot> snapshot)
{
    m_SparseSDF = std::move(snapshot);
}

void PhysicsDebugRenderer::BindPhysicsWorld(Scene& scene, const PhysicsWorld& world)
{
    for (auto* renderer : Registry())
    {
        if (renderer != nullptr && renderer->m_Scene == &scene)
        {
            renderer->m_PhysicsWorld = &world;
        }
    }
}

std::vector<PhysicsDebugRenderer*>& PhysicsDebugRenderer::Registry()
{
    static std::vector<PhysicsDebugRenderer*> registry;
    return registry;
}

void PhysicsDebugRenderer::EnsureInitialized()
{
    if (m_Material)
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
    specification.DebugName = "Physics Debug Pipeline";
    specification.Shader = shader;
    specification.Topology = PrimitiveTopology::Lines;
    specification.Cull = CullMode::None;
    specification.DepthTest = false;
    specification.DepthWrite = false;
    specification.DepthCompare = DepthCompareOperator::LessEqual;
    specification.Blend = true;

    m_Material = CreateRef<Material>(Pipeline::Create(specification));
}

void PhysicsDebugRenderer::UpdateToggleKeys()
{
    auto toggle = [](int key, bool& value, bool& wasPressed)
    {
        const bool pressed = Input::IsKeyPressed(key);
        if (pressed && wasPressed == false)
        {
            value = !value;
        }
        wasPressed = pressed;
    };

    toggle(Key::H, m_Settings.ShowSolverStatistics, m_WasOverlayKeyPressed);
    toggle(Key::B, m_Settings.ShowAABB, m_WasAABBKeyPressed);
    toggle(Key::O, m_Settings.ShowOBB, m_WasOBBKeyPressed);
    toggle(Key::F, m_Settings.ShowFatAABB, m_WasFatAABBKeyPressed);
    toggle(Key::T, m_Settings.ShowDynamicAABBTree, m_WasTreeKeyPressed);
    toggle(Key::P, m_Settings.ShowBroadPhasePairs, m_WasPairKeyPressed);
    toggle(Key::C, m_Settings.ShowContactPoints, m_WasContactPointKeyPressed);
    toggle(Key::N, m_Settings.ShowContactNormals, m_WasContactNormalKeyPressed);
}

void PhysicsDebugRenderer::Render(bool isRenderedScene)
{
    UpdateToggleKeys();

    if (m_Scene == nullptr)
    {
        return;
    }

    const bool showVectorFields = isRenderedScene && m_Settings.HasVisibleVectorField();
    const bool showTemperatureFields = isRenderedScene && m_Settings.HasVisibleTemperatureField();
    const bool showSDF = isRenderedScene && m_Settings.HasVisibleSDF();
    const bool showWorld = m_Settings.ShowAABB
        || m_Settings.ShowOBB
        || m_Settings.ShowFatAABB
        || m_Settings.ShowDynamicAABBTree
        || m_Settings.ShowBroadPhasePairs
        || m_Settings.ShowContactPoints
        || m_Settings.ShowContactNormals
        || showVectorFields
        || showTemperatureFields
        || showSDF;

    if (showWorld == false && m_Settings.ShowSolverStatistics == false)
    {
        return;
    }

    if (Renderer::IsExplicitSceneMode() == false)
    {
        EnsureInitialized();
        if (m_Material == nullptr)
        {
            return;
        }
    }

    if (showWorld)
    {
        // 3D Debug Shapeは必ず現在のScene Camera Contextを必要とします。
        // BeginScene(Camera)されていないSandbox等では描画せず、前回Cameraの使い回しを防ぎます。
        if (Renderer::GetCameraContext().Valid)
        {
            RenderWorldDebug(showVectorFields, showTemperatureFields, showSDF);
        }
    }

    if (m_Settings.ShowSolverStatistics)
    {
        RenderOverlay();
    }
}

void PhysicsDebugRenderer::RenderWorldDebug(bool showVectorFields, bool showTemperatureFields, bool showSDF)
{
    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;

    if (m_Settings.ShowAABB)
    {
        const math::Vec3 color{ 0.15f, 0.95f, 1.0f };
        for (auto [entity, transform, collider] : m_Scene->View<TransformComponent, ColliderComponent>())
        {
            static_cast<void>(entity);
            AABB bounds{};
            if (ComputeColliderAABB(transform, collider, bounds))
            {
                AddAABB(vertices, indices, bounds, color);
            }
        }
    }

    if (m_Settings.ShowOBB)
    {
        const math::Vec3 color{ 1.0f, 0.25f, 0.85f };
        for (auto [entity, transform, collider] : m_Scene->View<TransformComponent, ColliderComponent>())
        {
            static_cast<void>(entity);
            if (collider.Type != ColliderType::Box)
            {
                continue;
            }

            OBB box{};
            if (ComputeBoxOBB(transform, collider, box))
            {
                AddOBB(vertices, indices, box, color);
            }
        }
    }

    if (m_PhysicsWorld != nullptr)
    {
        if (m_Settings.ShowFatAABB || m_Settings.ShowDynamicAABBTree)
        {
            const DynamicAABBTree& tree = m_PhysicsWorld->GetBroadPhase().GetTree();
            const auto& nodes = tree.GetNodes();
            const bool valid = ValidateDynamicAABBTree(tree).IsValid();

            for (const auto& node : nodes)
            {
                if (node.Height < 0)
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    if (m_Settings.ShowFatAABB)
                    {
                        const math::Vec3 color = valid
                            ? math::Vec3{ 0.25f, 1.0f, 0.25f }
                            : math::Vec3{ 1.0f, 0.15f, 0.15f };
                        AddAABB(vertices, indices, node.Bounds, color);
                    }
                }
                else if (m_Settings.ShowDynamicAABBTree)
                {
                    const float heightScale = std::min(float(node.Height) / 8.0f, 1.0f);
                    const math::Vec3 color = valid
                        ? math::Vec3{ 0.45f + 0.45f * heightScale, 0.25f, 1.0f - 0.35f * heightScale }
                        : math::Vec3{ 1.0f, 0.15f, 0.15f };
                    AddAABB(vertices, indices, node.Bounds, color);
                }
            }
        }

        if (m_Settings.ShowContactPoints || m_Settings.ShowContactNormals)
        {
            for (const auto& manifold : m_PhysicsWorld->GetContactManifolds())
            {
                for (std::size_t i = 0; i < manifold.PointCount; ++i)
                {
                    const auto& point = manifold.Points[i];
                    if (m_Settings.ShowContactPoints)
                    {
                        AddPointMarker(vertices, indices, point.Position, m_Settings.ContactPointRadius, { 1.0f, 0.2f, 0.2f });
                    }
                    if (m_Settings.ShowContactNormals)
                    {
                        AddLine(vertices, indices, point.Position,
                            point.Position + manifold.Normal * m_Settings.ContactNormalLength,
                            { 1.0f, 1.0f, 0.2f });
                    }
                }
            }
        }

        if (m_Settings.ShowBroadPhasePairs)
        {
            for (const auto& pair : m_PhysicsWorld->GetBroadPhasePairs())
            {
                auto* transformA = m_Scene->TryGetComponent<TransformComponent>(pair.A.GetIndex());
                auto* transformB = m_Scene->TryGetComponent<TransformComponent>(pair.B.GetIndex());
                auto* colliderA = m_Scene->TryGetComponent<ColliderComponent>(pair.A.GetIndex());
                auto* colliderB = m_Scene->TryGetComponent<ColliderComponent>(pair.B.GetIndex());

                if (transformA == nullptr || transformB == nullptr || colliderA == nullptr || colliderB == nullptr)
                {
                    continue;
                }

                AABB aabbA{};
                AABB aabbB{};
                if (ComputeColliderAABB(*transformA, *colliderA, aabbA)
                    && ComputeColliderAABB(*transformB, *colliderB, aabbB))
                {
                    AddLine(vertices, indices, aabbA.GetCenter(), aabbB.GetCenter(), { 1.0f, 0.65f, 0.1f });
                }
            }
        }
    }

    if (showVectorFields == true)
    {
        AddVectorFieldDebug(vertices, indices);
    }
    if (showTemperatureFields == true)
    {
        AddTemperatureFieldDebug(vertices, indices);
    }
    if (showSDF == true)
    {
        AddSDFDebug(vertices, indices);
    }

    // Renderer::BeginScene(Camera)で設定されたCamera Contextを利用します。
    // Scene View / Game Viewのどちらでも同じDebug Renderer実装を共有できます。
    const RendererCameraContext& cameraContext = Renderer::GetCameraContext();
    SubmitLines(vertices, indices, cameraContext.View, cameraContext.Projection);
}

void PhysicsDebugRenderer::AddVectorFieldDebug(std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices) const
{
    PhysicsFieldDebugGrid grid;
    const float points = m_Settings.FieldGridPointsPerAxis;
    if (std::isfinite(points))
    {
        grid.PointsPerAxis = static_cast<int>(std::round(std::clamp(
            points, 1.0f, static_cast<float>(PhysicsFieldDebugGrid::MaxPointsPerAxis))));
    }
    grid.Spacing = m_Settings.FieldGridSpacing;
    const float halfExtent = grid.GetHalfExtent();
    const math::Vec3 extent{ halfExtent, halfExtent, halfExtent };
    // 物理単位ごとの基準値で 0.5 m の矢印に対応させ、各色の強度を独立して正規化します。
    constexpr PhysicsFieldDebugArrowStyle gravityStyle{ 9.81f, { 0.25f, 0.65f, 1.0f }, { 1.0f, 0.45f, 0.15f } };
    constexpr PhysicsFieldDebugArrowStyle electricStyle{ 10.0f, { 0.15f, 0.75f, 0.35f }, { 0.75f, 1.0f, 0.2f } };
    constexpr PhysicsFieldDebugArrowStyle magneticStyle{ 50.0e-6f, { 0.6f, 0.35f, 1.0f }, { 1.0f, 0.45f, 0.85f } };
    constexpr float baseArrowLength = 0.5f;
    const float maxArrowLength = 0.8f * grid.GetSpacing();
    const int samplesPerField = grid.PointsPerAxis * grid.PointsPerAxis * grid.PointsPerAxis;
    // 全 Field の合計を最大解像度のデモ 6 個分に制限し、Registry が増えても描画負荷を抑えます。
    constexpr int maxAxis = PhysicsFieldDebugGrid::MaxPointsPerAxis;
    int remainingSamples = 6 * maxAxis * maxAxis * maxAxis;
    auto isFinite = [](const math::Vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    auto drawField = [&](const VectorField& field, const PhysicsFieldDebugArrowStyle& style,
        float scale, const math::Vec3* center = nullptr)
    {
        grid.Center = center != nullptr ? *center : math::Vec3{};
        if (remainingSamples < samplesPerField || isFinite(grid.Center) == false)
        {
            return false;
        }
        remainingSamples -= samplesPerField;
        if (m_Settings.ShowFieldBounds && halfExtent > 0.0f)
        {
            AddAABB(vertices, indices, { grid.Center - extent, grid.Center + extent }, style.WeakColor);
        }
        if (center != nullptr)
        {
            AddPointMarker(vertices, indices, *center, grid.GetSpacing() * 0.15f, style.StrongColor);
        }
        for (const auto& sample : SamplePhysicsVectorField(field, grid))
        {
            std::array<PhysicsFieldDebugLine, 3> lines;
            if (BuildPhysicsFieldDebugArrow(sample, style, baseArrowLength * scale, maxArrowLength, lines))
            {
                for (const auto& line : lines)
                {
                    AddLine(vertices, indices, line.Start, line.End, line.Color);
                }
            }
        }
        return true;
    };

    const Scene& scene = *m_Scene;
    if (m_Settings.ShowVectorFields)
    {
        drawField(scene.GetPhysicsWorld().GetGravityField(), gravityStyle, m_Settings.GravityArrowScale);
    }
    if (m_Settings.ShowPointGravityFields && m_PointGravityField != nullptr)
    {
        drawField(*m_PointGravityField, gravityStyle, m_Settings.GravityArrowScale, &m_PointGravityField->GetCenter());
    }

    // Registry は Scene スレッドの描画中に参照し、Field への参照を次フレームへ持ち越しません。
    const auto& electromagnetism = scene.GetPhysicsSimulationWorld().GetElectromagneticSystem();
    if (m_Settings.ShowElectricFields || m_Settings.ShowPointChargeFields)
    {
        for (const ElectricField* field : electromagnetism.GetRegisteredElectricFields())
        {
            if (field == nullptr || remainingSamples < samplesPerField)
            {
                continue;
            }
            const auto* charge = dynamic_cast<const PointChargeElectricField*>(field);
            if (charge != nullptr ? m_Settings.ShowPointChargeFields : m_Settings.ShowElectricFields)
            {
                drawField(*field, electricStyle, m_Settings.ElectricArrowScale,
                    charge != nullptr ? &charge->GetSourcePosition() : nullptr);
            }
        }
    }
    if (m_Settings.ShowMagneticFields || m_Settings.ShowDipoleFields)
    {
        for (const MagneticField* field : electromagnetism.GetRegisteredMagneticFields())
        {
            if (field == nullptr || remainingSamples < samplesPerField)
            {
                continue;
            }
            const auto* dipole = dynamic_cast<const DipoleMagneticField*>(field);
            if (dipole == nullptr && m_Settings.ShowMagneticFields)
            {
                drawField(*field, magneticStyle, m_Settings.MagneticArrowScale);
            }
            if (dipole != nullptr && m_Settings.ShowDipoleFields
                && drawField(*dipole, magneticStyle, m_Settings.MagneticArrowScale, &dipole->GetCenter()))
            {
                // モーメントの単位や大きさに関係なく、正方向を格子間隔に合わせて示します。
                const auto& moment = dipole->GetDipoleMoment();
                const float maxComponent = std::max({ std::abs(moment.x), std::abs(moment.y), std::abs(moment.z) });
                if (isFinite(moment) && maxComponent > 0.0f)
                {
                    const math::Vec3 scaled{ moment.x / maxComponent, moment.y / maxComponent, moment.z / maxComponent };
                    const auto tip = dipole->GetCenter() + scaled.Normalized() * grid.GetSpacing();
                    AddLine(vertices, indices, dipole->GetCenter(), tip, magneticStyle.StrongColor);
                }
            }
        }
    }
}

void PhysicsDebugRenderer::AddTemperatureFieldDebug(std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices) const
{
    const bool influence = m_Settings.ShowTemperatureInfluence;
    const bool validRange = std::isfinite(m_Settings.TemperatureMinimum) && std::isfinite(m_Settings.TemperatureMaximum)
        && m_Settings.TemperatureMinimum >= 0.0f && m_Settings.TemperatureMinimum < m_Settings.TemperatureMaximum;
    const float minimum = influence ? 0.0f : (validRange ? m_Settings.TemperatureMinimum : PhysicsDebugSettings::DefaultTemperatureMinimum);
    const float maximum = influence ? 1.0f : (validRange ? m_Settings.TemperatureMaximum : PhysicsDebugSettings::DefaultTemperatureMaximum);
    const math::Vec3 lowColor = influence ? math::Vec3{ 0.35f, 0.15f, 0.7f } : math::Vec3{ 0.15f, 0.65f, 1.0f };
    const math::Vec3 highColor = influence ? math::Vec3{ 0.25f, 1.0f, 0.35f } : math::Vec3{ 1.0f, 0.25f, 0.08f };
    constexpr math::Vec3 boundsColor{ 0.9f, 0.9f, 0.9f };

    const Scene& scene = *m_Scene;
    const auto& registry = scene.GetPhysicsSimulationWorld().GetThermalWorld().GetTemperatureFieldRegistry();
    PhysicsFieldDebugSlice slice;
    slice.Plane = m_Settings.TemperatureSlicePlane;
    slice.Coordinate = m_Settings.TemperatureSliceCoordinate;
    // 1 フレームに最大 4 スライスを評価します。登録参照は描画呼び出し中のみ使用します。
    int remainingFields = 4;
    for (const TemperatureField* field : registry.GetRegisteredFields())
    {
        if (remainingFields == 0)
        {
            break;
        }
        const auto* uniform = dynamic_cast<const UniformTemperatureField*>(field);
        const auto* gradient = dynamic_cast<const GradientTemperatureField*>(field);
        const auto* sphere = dynamic_cast<const SphericalTemperatureRegionField*>(field);
        const auto* box = dynamic_cast<const BoxTemperatureRegionField*>(field);
        if (gradient != nullptr && m_Settings.ShowGradientTemperatureField)
        {
            slice.Center = gradient->GetReferencePosition();
        }
        else if (sphere != nullptr && m_Settings.ShowSphericalTemperatureField)
        {
            slice.Center = sphere->GetCenter();
        }
        else if (box != nullptr && m_Settings.ShowBoxTemperatureField)
        {
            slice.Center = box->GetCenter();
        }
        else if (uniform != nullptr && m_Settings.ShowUniformTemperatureField)
        {
            slice.Center = {};
        }
        else
        {
            continue;
        }
        if (std::isfinite(slice.Center.x) == false || std::isfinite(slice.Center.y) == false
            || std::isfinite(slice.Center.z) == false)
        {
            continue;
        }
        --remainingFields;

        const TemperatureSliceField displayField(*field, influence);
        for (const auto& sample : SamplePhysicsScalarField(displayField, slice))
        {
            math::Vec3 color;
            if (TryGetPhysicsFieldDebugScalarColor(sample.Value, minimum, maximum, lowColor, highColor, color))
            {
                AddPointMarker(vertices, indices, sample.Position, PhysicsFieldDebugSlice::Spacing * 0.18f, color);
            }
        }

        if (m_Settings.ShowTemperatureBounds && sphere != nullptr && sphere->GetRadius() > 0.0f)
        {
            // 3 つの大円で球のコアを示します。Falloff の外縁は Influence の色で判別できます。
            constexpr int segments = 32;
            constexpr float angleStep = 2.0f * std::numbers::pi_v<float> / segments;
            for (int normal = 0; normal < 3; ++normal)
            {
                math::Vec3 previous = slice.Center;
                previous[(normal + 1) % 3] += sphere->GetRadius();
                for (int segment = 1; segment <= segments; ++segment)
                {
                    const float angle = segment * angleStep;
                    math::Vec3 point = slice.Center;
                    point[(normal + 1) % 3] += sphere->GetRadius() * std::cos(angle);
                    point[(normal + 2) % 3] += sphere->GetRadius() * std::sin(angle);
                    AddLine(vertices, indices, previous, point, boundsColor);
                    previous = point;
                }
            }
        }
        if (m_Settings.ShowTemperatureBounds && box != nullptr)
        {
            AddAABB(vertices, indices,
                { slice.Center - box->GetHalfExtents(), slice.Center + box->GetHalfExtents() }, boundsColor);
        }
    }
}

void PhysicsDebugRenderer::AddSDFDebug(std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices) const
{
    RAVEN_PROFILE_SCOPE("Physics.Debug.SDF");
    const auto firstIndex = indices.size();
    std::size_t sampleCount = 0;
    PhysicsFieldDebugSlice slice;
    slice.Plane = m_Settings.SDFSlicePlane;
    slice.Coordinate = m_Settings.SDFSliceCoordinate;
    constexpr float spacing = 0.25f;
    slice.SampleSpacing = spacing;
    const float range = std::isfinite(m_Settings.SDFDistanceRange) && m_Settings.SDFDistanceRange > 0.0f
        ? m_Settings.SDFDistanceRange : 0.75f;

    auto drawSlice = [&](const auto& source, bool signedDistance)
    {
        const auto positions = BuildPhysicsFieldDebugSlicePositions(slice);
        sampleCount += positions.size();
        std::vector<PhysicsSDFDebugSample> samples;
        samples.reserve(positions.size());
        for (const auto& position : positions)
        {
            const auto sample = SamplePhysicsDebugSDF(source, position);
            samples.push_back(sample);
            math::Vec3 color;
            switch (sample.Status)
            {
            case PhysicsSDFDebugStatus::Valid:
                if (signedDistance)
                {
                    const math::Vec3 extreme = sample.Distance < 0.0f
                        ? math::Vec3{ 0.15f, 0.35f, 1.0f } : math::Vec3{ 1.0f, 0.23f, 0.12f };
                    TryGetPhysicsFieldDebugScalarColor(std::abs(sample.Distance), 0.0f, range,
                        { 0.85f, 0.85f, 0.85f }, extreme, color);
                }
                else
                {
                    TryGetPhysicsFieldDebugScalarColor(sample.Distance, 0.0f, range,
                        { 0.1f, 0.95f, 0.85f }, { 0.95f, 0.95f, 0.1f }, color);
                }
                break;
            case PhysicsSDFDebugStatus::OutsideGrid: color = { 0.45f, 0.45f, 0.45f }; break;
            case PhysicsSDFDebugStatus::OutsideBand: color = { 1.0f, 0.6f, 0.1f }; break;
            case PhysicsSDFDebugStatus::MissingSupport: color = { 0.65f, 0.25f, 1.0f }; break;
            case PhysicsSDFDebugStatus::Invalid: continue;
            }
            AddPointMarker(vertices, indices, position, spacing * 0.19f, color);
        }
        if (signedDistance)
        {
            for (const auto& line : BuildPhysicsSDFZeroContour(samples))
            {
                AddLine(vertices, indices, line.Start, line.End, line.Color);
            }
        }
    };

    if (m_Settings.ShowDenseSignedSDF && m_DenseSignedSDF != nullptr)
    {
        slice.Center = m_DenseSignedSDF->GetCenter();
        drawSlice(*m_DenseSignedSDF, true);
    }
    if (m_Settings.ShowDenseUnsignedSDF && m_DenseUnsignedSDF != nullptr)
    {
        slice.Center = {};
        drawSlice(*m_DenseUnsignedSDF, false);
    }
    if (m_Settings.ShowSparseSDF && m_SparseSDF != nullptr)
    {
        const auto& grid = m_SparseSDF->GetGrid();
        slice.Center = (grid.GetCellCenter(0, 0, 0)
            + grid.GetCellCenter(grid.Size[0] - 1, grid.Size[1] - 1, grid.Size[2] - 1)) * 0.5f;
        drawSlice(*m_SparseSDF, m_SparseSDF->IsSigned());
    }
    CPUProfiler::Get().AddCounter("Physics.Debug.SDF.Samples", static_cast<double>(sampleCount));
    CPUProfiler::Get().AddCounter("Physics.Debug.SDF.Lines", static_cast<double>((indices.size() - firstIndex) / 2));
}

void PhysicsDebugRenderer::RenderOverlay()
{
    if (m_PhysicsWorld == nullptr)
    {
        return;
    }

    // Viewport取得もRenderCommand経由に統一し、Physics Debug層からOpenGL state参照を排除します。
    // 実際の取得方法はRHI Backend側が担当するため、Debug RendererはGraphics APIに依存しません。
    const RHIViewport viewport = Renderer::IsExplicitSceneMode() == true ?
        Renderer::GetFrameViewport() : RenderCommand::GetViewport();
    if (viewport.Width == 0 || viewport.Height == 0)
    {
        return;
    }

    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;
    const auto& stats = m_PhysicsWorld->GetSolverDebugStatistics();

    const math::Vec3 titleColor{ 0.25f, 1.0f, 0.85f };
    const math::Vec3 textColor{ 0.92f, 0.92f, 0.92f };
    const math::Vec3 onColor{ 0.35f, 1.0f, 0.35f };
    const math::Vec3 offColor{ 0.65f, 0.65f, 0.65f };

    float y = 16.0f;
    auto addLine = [&](const std::string& text, const math::Vec3& color)
    {
        AddOverlayText(
            vertices,
            indices,
            text,
            16.0f,
            y,
            2.0f,
            static_cast<int>(viewport.Width),
            static_cast<int>(viewport.Height),
            color);
        y += 18.0f;
    };

    addLine("PHYSICS DEBUG", titleColor);
    addLine("[H] HIDE OVERLAY", offColor);
    y += 6.0f;
    addLine("SOLVER", titleColor);
    addLine("MANIFOLDS: " + std::to_string(stats.ManifoldCount), textColor);
    addLine("CONTACT POINTS: " + std::to_string(stats.ContactPointCount), textColor);
    addLine("PERSISTENT MANIFOLDS: " + std::to_string(stats.PersistentManifoldCount), textColor);
    addLine("PERSISTENT CONTACTS: " + std::to_string(stats.PersistentContactPointCount), textColor);
    addLine("WARM STARTED: " + std::to_string(stats.WarmStartedConstraintCount), textColor);
    addLine("ITERATIONS: " + std::to_string(stats.VelocityIterations), textColor);
    addLine("MAX PENETRATION: " + FormatFloat(stats.MaxPenetration), textColor);
    addLine("NORMAL IMPULSE: " + FormatFloat(stats.MaxNormalImpulse), textColor);
    addLine("FRICTION IMPULSE: " + FormatFloat(stats.MaxFrictionImpulse), textColor);
    y += 6.0f;
    addLine("VISUALIZATION", titleColor);

    auto addToggleLine = [&](const char* key, const char* label, bool enabled)
    {
        addLine(std::string("[") + key + "] " + label + ": " + OnOff(enabled), enabled ? onColor : offColor);
    };

    addToggleLine("B", "AABB", m_Settings.ShowAABB);
    addToggleLine("O", "OBB", m_Settings.ShowOBB);
    addToggleLine("F", "FAT AABB", m_Settings.ShowFatAABB);
    addToggleLine("T", "DYNAMIC TREE", m_Settings.ShowDynamicAABBTree);
    addToggleLine("P", "BROAD PAIRS", m_Settings.ShowBroadPhasePairs);
    addToggleLine("C", "CONTACT POINT", m_Settings.ShowContactPoints);
    addToggleLine("N", "CONTACT NORMAL", m_Settings.ShowContactNormals);
    addLine("GRAVITY: " + OnOff(m_Settings.ShowVectorFields) + " POINT: " + OnOff(m_Settings.ShowPointGravityFields),
        (m_Settings.ShowVectorFields || m_Settings.ShowPointGravityFields) ? onColor : offColor);
    addLine("ELECTRIC: " + OnOff(m_Settings.ShowElectricFields) + " CHARGE: " + OnOff(m_Settings.ShowPointChargeFields),
        (m_Settings.ShowElectricFields || m_Settings.ShowPointChargeFields) ? onColor : offColor);
    addLine("MAGNETIC: " + OnOff(m_Settings.ShowMagneticFields) + " DIPOLE: " + OnOff(m_Settings.ShowDipoleFields),
        (m_Settings.ShowMagneticFields || m_Settings.ShowDipoleFields) ? onColor : offColor);
    if (m_Settings.HasVisibleTemperatureField())
    {
        addLine(m_Settings.ShowTemperatureInfluence ? "THERMAL: INFLUENCE 0-1" : "THERMAL: TEMPERATURE K", onColor);
    }

    SubmitLines(vertices, indices, math::Mat4::Identity(), math::Mat4::Identity());
}

void PhysicsDebugRenderer::SubmitLines(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Mat4& view,
    const math::Mat4& projection)
{
    if (vertices.empty() || indices.empty())
    {
        return;
    }

    if (Renderer::IsExplicitSceneMode() == true)
    {
        std::vector<Renderer::DebugLineVertex> debugVertices;
        debugVertices.reserve(vertices.size());
        for (const DebugVertex& vertex : vertices)
        {
            debugVertices.push_back({ vertex.Position, vertex.Color, vertex.Texcoord });
        }
        Renderer::SubmitDebugLines(debugVertices, indices, view, projection);
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
    Ref<IndexBuffer> indexBuffer = IndexBuffer::Create(indices.data(), uint32_t(indices.size()));
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(indexBuffer);

    // 線分頂点は world-space（Overlay は NDC）のため、共通 Shader の Model を単位行列にします。
    m_Material->SetUniform("u_Model", math::Mat4::Identity());
    m_Material->SetUniform("u_View", view);
    m_Material->SetUniform("u_Projection", projection);
    m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_Material->SetUniform("u_Alpha", 1.0f);

    // Material bindingとDrawの両方をRHI互換窓口へ統一します。
    // Physics Debug層がRendererAPIやOpenGL primitiveを直接扱わないことで、
    // PipelineのLines topologyを各RHI Backendが解決できる構造にします。
    m_Material->Bind();
    RenderCommand::DrawIndexed(vertexArray, uint32_t(indices.size()));
}

void PhysicsDebugRenderer::AddLine(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const math::Vec3& a, const math::Vec3& b, const math::Vec3& color)
{
    const uint32_t base = uint32_t(vertices.size());
    vertices.push_back({ a, color, {} });
    vertices.push_back({ b, color, {} });
    indices.push_back(base + 0);
    indices.push_back(base + 1);
}

void PhysicsDebugRenderer::AddAABB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const AABB& bounds, const math::Vec3& color)
{
    const math::Vec3& min = bounds.Min;
    const math::Vec3& max = bounds.Max;
    const math::Vec3 p[8] = {
        {min.x,min.y,min.z},{max.x,min.y,min.z},{max.x,max.y,min.z},{min.x,max.y,min.z},
        {min.x,min.y,max.z},{max.x,min.y,max.z},{max.x,max.y,max.z},{min.x,max.y,max.z}
    };
    const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : edges)
    {
        AddLine(vertices, indices, p[edge[0]], p[edge[1]], color);
    }
}

void PhysicsDebugRenderer::AddOBB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const OBB& bounds, const math::Vec3& color)
{
    math::Vec3 corners[8]{};
    bounds.GetCorners(corners);
    const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : edges)
    {
        AddLine(vertices, indices, corners[edge[0]], corners[edge[1]], color);
    }
}

void PhysicsDebugRenderer::AddPointMarker(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const math::Vec3& position, float radius, const math::Vec3& color)
{
    AddLine(vertices, indices, position - math::Vec3{radius,0,0}, position + math::Vec3{radius,0,0}, color);
    AddLine(vertices, indices, position - math::Vec3{0,radius,0}, position + math::Vec3{0,radius,0}, color);
    AddLine(vertices, indices, position - math::Vec3{0,0,radius}, position + math::Vec3{0,0,radius}, color);
}

void PhysicsDebugRenderer::AddOverlayText(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const std::string& text, float pixelX, float pixelY, float pixelScale,
    int viewportWidth, int viewportHeight, const math::Vec3& color)
{
    if (viewportWidth <= 0 || viewportHeight <= 0 || pixelScale <= 0.0f)
    {
        return;
    }

    const float glyphWidth = 5.0f * pixelScale;
    const float glyphHeight = 7.0f * pixelScale;
    const float advance = 6.0f * pixelScale;
    const float lineThickness = std::max(1.0f, pixelScale);

    float cursorX = pixelX;
    for (char c : text)
    {
        if (c == ' ')
        {
            cursorX += advance;
            continue;
        }

        const auto glyph = detail::GetPhysicsDebugGlyph(c);

        for (int row = 0; row < 7; ++row)
        {
            const uint8_t rowBits = glyph[row];
            int column = 0;
            while (column < 5)
            {
                const bool lit = (rowBits & (1u << (4 - column))) != 0;
                if (lit == false)
                {
                    ++column;
                    continue;
                }

                const int startColumn = column;
                while (column + 1 < 5 && (rowBits & (1u << (4 - (column + 1)))) != 0)
                {
                    ++column;
                }

                const float startX = cursorX + float(startColumn) * pixelScale;
                const float endX = cursorX + float(column + 1) * pixelScale;
                const float centerY = pixelY + float(row) * pixelScale + lineThickness * 0.5f;

                const float x0 = (startX / float(viewportWidth)) * 2.0f - 1.0f;
                const float x1 = (endX / float(viewportWidth)) * 2.0f - 1.0f;
                const float y = 1.0f - (centerY / float(viewportHeight)) * 2.0f;
                AddLine(vertices, indices, {x0,y,0.0f}, {x1,y,0.0f}, color);
                ++column;
            }
        }
        cursorX += advance;
    }
}

} // namespace Raven::ph
