#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"
#include "Raven/Physics/SoftBody/XPBDDihedralConstraint.h"
#include "Raven/Physics/SoftBody/XPBDDistanceConstraint.h"
#include "Raven/Physics/SoftBody/XPBDVolumeConstraint.h"

namespace Raven
{
namespace ph
{

struct SoftBodyCloth;
struct SoftBodySelfCollisionSettings;
struct SoftBodyParticleTriangleSelfCollisionSettings;

// SolverのStepローカル一時コンテナが使用する確保元です。
// ④のBefore / After計測ではSolver処理を変えず、このModeだけを切り替えて比較します。
enum class SoftBodyTemporaryAllocatorMode : uint8_t
{
    Heap = 0u,
    FrameAllocator
};

struct SoftBodySolverSettings
{
    // Position Constraintを1Step内で繰り返し解く回数です。
    // XPBDでは反復回数を増やすほどConstraint誤差が小さくなります。
    // StepWithSelfCollisions()ではInternal Constraint・自己衝突・外部Collisionの全てが
    // この共通反復回数の中で解かれます。
    uint32_t SolverIterations = 8u;

    // 速度減衰です。0なら減衰なし、1に近いほど急速に停止します。
    float Damping = 0.01f;

    // Step内Temporary STL Containerの確保元です。
    // 通常はHeap互換のままにし、Profiler比較時にFrameAllocatorへ切り替えます。
    SoftBodyTemporaryAllocatorMode TemporaryAllocatorMode = SoftBodyTemporaryAllocatorMode::Heap;
};

// ============================================================================
// Soft Body Collision Collider
// ============================================================================
// Collider自体はSolverローカル空間で保持します。
// World Transformとの同期はSoftBodyをSceneへ統合するレイヤー側で行います。
struct SoftBodySphereCollider
{
    math::Vec3 Center{};
    float Radius = 0.5f;
    float Compliance = 0.0f;

    // ParticleごとのXPBD Lambdaです。
    // 片側Constraintなので0以上にClampし、Step開始時に0へResetします。
    std::vector<float> Lambdas;

    // Soft/Rigid連成用の1Step分Feedbackです。
    // SolverはCloth Particleへ適用したImpulseの反作用をCollider側へ蓄積し、
    // Scene側がRigidBodyへ変換して適用します。
    math::Vec3 AccumulatedReactionImpulse{};
    math::Vec3 AccumulatedContactPoint{};
    uint32_t ContactCount = 0u;

    math::Vec3 GetAverageContactPoint() const
    {
        if (ContactCount == 0u)
        {
            return Center;
        }
        return AccumulatedContactPoint / static_cast<float>(ContactCount);
    }
};

struct SoftBodyPlaneCollider
{
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Offset = 0.0f;
    float Compliance = 0.0f;
    std::vector<float> Lambdas;
};

// ============================================================================
// Particle-Triangle Collision Statistics
// ============================================================================
// Cell Size比較やNarrow Phase最適化の効果を同じ指標で追跡するための1Step統計です。
struct SoftBodyParticleTriangleCollisionStatistics
{
    uint64_t CandidateCount = 0u;
    uint64_t NarrowPhaseCount = 0u;
    uint64_t PositionCorrectionCount = 0u;
};

// ============================================================================
// Soft Body Solver
// ============================================================================
// XPBDベースのParticle Solverです。
//
// Cloth統合Stepでは
//
//   Predict
//     -> [Distance -> Dihedral -> Particle-Particle -> Particle-Triangle -> Sphere -> Plane] x Iterations
//     -> Velocity
//
// の順で解きます。自己衝突をSolver反復の外側へ後処理するのではなく同じPosition反復へ含めることで、
// Internal Constraint・自己衝突・外部Colliderが互いの補正結果を同一Step内で再評価できます。
// BendingもDistance近似とは別Constraintとして持つため、Cloth Topology上の隣接Triangle間角度を直接制御できます。
class SoftBodySolver
{
public:
    SoftBodySolver();
    SoftBodySolver(const SoftBodySolver& other);
    SoftBodySolver& operator=(const SoftBodySolver& other);
    SoftBodySolver(SoftBodySolver&&) = delete;
    SoftBodySolver& operator=(SoftBodySolver&&) = delete;

    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings)
    {
        // Allocator Mode変更時は、前Stepで使用したArenaを次のModeへ持ち越さないよう統計と一緒にResetします。
        // SetSettings()はStep外から呼ぶ前提なので、この時点ではStepローカルContainerは既に破棄済みです。
        if (m_Settings.TemporaryAllocatorMode != settings.TemporaryAllocatorMode)
        {
            m_TemporaryAllocationStatistics.Reset();

            if (settings.TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::FrameAllocator)
            {
                m_TemporaryAllocationStatistics.SetBackingAllocator(&m_TemporaryFrameAllocator);
            }
            else
            {
                m_TemporaryAllocationStatistics.SetBackingAllocator(nullptr);
            }
        }

        m_Settings = settings;
    }

    SoftBodySolverSettings& GetSettings() { return m_Settings; }
    const SoftBodySolverSettings& GetSettings() const { return m_Settings; }

    // Particleを追加し、そのIndexを返します。
    // inverseMass == 0.0f のParticleは固定点として扱われます。
    uint32_t AddParticle(const math::Vec3& position, float inverseMass = 1.0f);

    // 現在のParticle間距離をRestLengthとしてXPBD Distance Constraintを追加します。
    // compliance == 0.0f に近いほど硬く、値を大きくすると柔らかい制約になります。
    uint32_t AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance = 0.0f);

    // 共有Edgeを持つ2TriangleからDihedral Bending Constraintを追加します。
    // oppositeA/oppositeBはそれぞれのTriangleで共有Edgeの反対側にある頂点です。
    // RestAngleは追加時の形状から自動計算します。
    uint32_t AddDihedralConstraint(
        uint32_t oppositeA,
        uint32_t oppositeB,
        uint32_t edgeA,
        uint32_t edgeB,
        float compliance = 0.0f);

    // Sphere Colliderを追加します。
    // 戻り値はCollider Indexで、SetSphereCollider()による後続更新に利用できます。
    uint32_t AddSphereCollider(const math::Vec3& center, float radius);
    void SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius);
    void ClearSphereColliders();

    // 静的Plane Colliderを追加します。
    // offsetは dot(normal, x) = offset を満たすPlane位置を表します。
    uint32_t AddPlaneCollider(const math::Vec3& normal, float offset);
    void SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset);
    void ClearPlaneColliders();

    // Particle / Constraint / Colliderをすべて破棄します。
    // Clothを再構築した場合は、必要なColliderも再登録する必要があります。
    void Clear();

    // XPBDの通常1Simulation Stepを進めます。
    // 自己衝突Topologyを必要としないSoftBody用途はこちらを使用します。
    // deltaTime <= 0.0f の場合は計算を行いません。
    void Step(float deltaTime);

    // ========================================================================
    // Volumetric Soft Body Integrated XPBD Step
    // ========================================================================
    // ゼリーなどの四面体SoftBody向けに、外部所有のVolume Constraint群を通常のInternal Constraintと
    // 同じSolverIterationsへ統合して解きます。
    //
    //   Predict
    //     -> [Distance -> Volume -> Dihedral -> Sphere -> Plane] x Iterations
    //     -> Velocity
    //
    // Volume ConstraintをStep()後の後処理にしないことが重要です。DistanceやCollisionが行った補正を
    // 次iterationのVolume Constraintが再評価できるため、接触しながら潰れるゼリーでも体積保存と
    // 外部Collisionを同時に収束させられます。
    //
    // volumeConstraintsは次段階でSoftBodyJellyが所有する想定です。Solver側へ永続所有させないことで、
    // ClothなどVolumeを必要としないSoftBodyの既存Topologyと責務を分離しています。
    void StepWithVolumeConstraints(
        float deltaTime,
        std::vector<XPBDVolumeConstraint>& volumeConstraints);

    // ========================================================================
    // Cloth Integrated XPBD Step
    // ========================================================================
    // Cloth自己衝突を既存SolverIterationsの「内側」へ組み込むStepです。
    // 各self collision設定のSolverIterationsはここでは使用せず、外側のm_Settings.SolverIterationsへ
    // 統一します。これによりDistance / Dihedral / Self Collision / External Collisionが
    // 1つの反復系列として収束し、Step後の追加Position補正とVelocity再構築が不要になります。
    void StepWithSelfCollisions(
        float deltaTime,
        const SoftBodyCloth& cloth,
        const SoftBodySelfCollisionSettings& particleSettings,
        const SoftBodyParticleTriangleSelfCollisionSettings& particleTriangleSettings);

    std::vector<SoftBodyParticle>& GetParticles() { return m_Particles; }
    const std::vector<SoftBodyParticle>& GetParticles() const { return m_Particles; }
    const std::vector<XPBDDistanceConstraint>& GetDistanceConstraints() const { return m_DistanceConstraints; }
    const std::vector<XPBDDihedralConstraint>& GetDihedralConstraints() const { return m_DihedralConstraints; }
    const std::vector<SoftBodySphereCollider>& GetSphereColliders() const { return m_SphereColliders; }
    const std::vector<SoftBodyPlaneCollider>& GetPlaneColliders() const { return m_PlaneColliders; }

    // 直近のStepWithSelfCollisions()で集計したParticle-Triangle自己衝突の統計です。
    // Cell Size比較時は同じシーン・同じSolverIterationsでこの値を比較します。
    const SoftBodyParticleTriangleCollisionStatistics& GetParticleTriangleCollisionStatistics() const
    {
        return m_ParticleTriangleCollisionStatistics;
    }

    // Browser Debug Viewerなど、低頻度の診断表示から直近Particle-Triangle Hash Buildの
    // Active Cell状態を取得します。Grid本体は公開せず、Debug用値型だけをコピーします。
    void CollectParticleTriangleSpatialHashDebugInfo(
        std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& outCells) const
    {
        m_ParticleTriangleSpatialHash.CollectActiveCellDebugInfo(outCells);
    }

    // 直近のStepWithSelfCollisions()でStepローカルSTLコンテナが要求したTemporary allocation統計です。
    // Phase ②では通常Heapへの要求を計測し、Phase ③では同じCounterをFrameAllocator経由へ切り替えて
    // Before / Afterを同一指標で比較します。
    // BackingUsedMemory / BackingPeakUsedMemoryはFrameAllocatorの実Arena使用量と容量調整に利用します。
    const SolverTemporaryAllocationStatistics& GetTemporaryAllocationStatistics() const
    {
        return m_TemporaryAllocationStatistics;
    }

private:
    // Step開始時の位置をPreviousPositionへ保存し、重力とVelocityから予測位置を作ります。
    void PredictPositions(float deltaTime);

    // Distance / Dihedral ConstraintのLambdaをStep開始時に0へ戻します。
    void ResetConstraintLambdas();

    // Sphere / Plane CollisionのParticle別Lambdaと、Sphereが外部へ返す1Step分Feedbackを初期化します。
    void ResetCollisionConstraintState();

    // Structural / Shear / 比較用Distance Bendingを含む全Distance Constraintを1 iteration分解きます。
    void SolveDistanceConstraints(float deltaTime);

    // Clothの共有Edgeに対するDihedral Angle Constraintを1 iteration分解きます。
    void SolveDihedralConstraints(float deltaTime);

    // Sphereとの片側XPBD Collision Constraintを1 iteration分解きます。
    // 同時にRigidBody側へ返すReaction Impulse / Contact Pointも蓄積します。
    void SolveSphereCollisions(float deltaTime);

    // Planeとの片側XPBD Collision Constraintを1 iteration分解きます。
    void SolvePlaneCollisions(float deltaTime);

    // 最終PositionとPreviousPositionからVelocity = (x_new - x_old) / dt を再構築します。
    void UpdateVelocities(float deltaTime);

private:
    // ========================================================================
    // SoftBody Solver Temporary Frame Allocator
    // ========================================================================
    // ④の実測ではLifetime Peakが約170 KiB、25% Headroom込み推奨値が約212 KiBでした。
    // 4 KiB境界の推奨値よりさらに余裕を持たせ、固定Arena容量は256 KiBを採用します。
    // 容量不足時にHeapへfallbackするとAfter計測が不正確になるためfallbackは行いません。
    static constexpr std::size_t SoftBodyTemporaryFrameAllocatorCapacity = 256u * 1024u;

    math::Vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<XPBDDihedralConstraint> m_DihedralConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
    SoftBodyParticleTriangleCollisionStatistics m_ParticleTriangleCollisionStatistics{};

    // FrameAllocatorはStatisticsより先に構築し、Statisticsへ非所有ポインタとして登録します。
    // member破棄順は宣言の逆順なので、Statisticsが生存している間はBacking Allocatorも必ず生存します。
    FrameAllocator m_TemporaryFrameAllocator{ SoftBodyTemporaryFrameAllocatorCapacity };
    SolverTemporaryAllocationStatistics m_TemporaryAllocationStatistics{ &m_TemporaryFrameAllocator };

    // ClothのTriangle Index列はParticle Positionと異なりTopology変更時しか変化しません。
    // Stepローカルvectorとして毎frame構築するとHeap allocationとTopology再生成が発生するため、
    // Rows / Columnsが変わった時だけ再構築し、それ以外のframeでは同じcapacityと内容を再利用します。
    // これは「Step中だけ必要なTemporary」ではないためFrameAllocatorへ置くより永続Cacheが適切です。
    std::vector<SoftBodyTriangle> m_SelfCollisionTriangles;
    uint32_t m_SelfCollisionTriangleRows = 0u;
    uint32_t m_SelfCollisionTriangleColumns = 0u;

    // Particle-Triangle Flat HashはBucketごとにvector capacityを持つため、Stepローカルにすると
    // 毎frame最初のSolver iterationで巨大なBucket配列を再確保してしまいます。
    // Solver寿命まで保持し、frame間でもBucket / Triangle scratch capacityを再利用します。
    SoftBodyTriangleSpatialHashGrid m_ParticleTriangleSpatialHash{ 0.05f };
};

} // namespace ph
} // namespace Raven
