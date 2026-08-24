#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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

struct SoftBodySolverSettings
{
    // Position Constraintを1Step内で繰り返し解く回数です。
    // XPBDでは反復回数を増やすほどConstraint誤差が小さくなります。
    // StepWithSelfCollisions()ではInternal Constraint・自己衝突・外部Collisionの全てが
    // この共通反復回数の中で解かれます。
    uint32_t SolverIterations = 8u;

    // ParticleをCollider表面から僅かに離して保持する共通厚みです。
    // Clothは数学的には厚み0の面なので、完全にCollider表面へ配置すると浮動小数点誤差により
    // 次の反復で再び内部判定されやすくなります。小さな余白を持たせて接触を安定させます。
    float CollisionThickness = 0.005f;

    // Sphere Collision ConstraintのComplianceです。
    // 0.0fなら硬い片側Constraintとして働き、値を大きくするとCollider表面が柔らかくなります。
    // Distance Constraintと同じく alphaTilde = Compliance / dt^2 としてXPBD式へ入ります。
    float SphereCollisionCompliance = 0.0f;

    // Plane Collision ConstraintのComplianceです。
    // Sphereと同じXPBD式へ統一し、0.0fなら硬い床、値を増やすと沈み込みを許す柔らかい床になります。
    float PlaneCollisionCompliance = 0.0f;
};

// ============================================================================
// Particle-Triangle Self Collision Statistics
// ============================================================================
// Particle-Triangle Broad Phase / Narrow Phaseの負荷を1 Step単位で比較するための計測値です。
// StepWithSelfCollisions()開始時にReset()され、全Solver Iterationの値を合算します。
//
// CandidateCount:
//   Spatial Hashが生成したParticle-Triangle候補ペア総数です。
//   Topology除外などのcheap reject前の値なので、Cell Size変更によるBroad Phase候補数の差を
//   直接比較できます。
//
// NarrowPhaseCount:
//   Index検証と「Particle自身を含むTriangle」のTopology除外を通過し、
//   ComputeClosestPointOnTriangle()を実際に実行する直前まで到達した回数です。
//   CandidateCountとの差を見ることで、Broad Phase候補のうちNarrow Phaseへ流入した割合を確認できます。
//
// DistanceCount:
//   Closest Pointを求め、Particle-Triangle距離とCollision法線の構築まで完了した回数です。
//   NarrowPhaseCountとの差と、次のConstraintCountまでの減少量を比較することで、
//   Distance計算後の早期Rejectがどの程度効くかを確認できます。
//
// ConstraintCount:
//   thickness外かつLambdaが残っていない候補を除外し、XPBD Constraint計算へ進んだ回数です。
//   DistanceCountとの差は「距離・法線まで求めたがConstraint不要だった候補数」を表します。
//
// DenominatorRejectCount:
//   Constraintへ進んだものの、逆質量とComplianceから作るdenominatorがEpsilon以下となり、
//   DeltaLambda計算へ進めなかった回数です。固定Particleだけで構成された接触などを切り分けます。
//
// DeltaLambdaCount:
//   有効なdenominatorを得て、XPBDのDeltaLambda計算まで実行した回数です。
//   ConstraintCount - DenominatorRejectCount と一致するため、後半funnelの整合性確認にも使えます。
//
// DeltaLambdaRejectCount:
//   DeltaLambda計算後、実際に適用するappliedDeltaLambdaがEpsilon以下だった回数です。
//   この値が大きければ、Constraint後半まで計算してもPosition更新へ寄与しない候補が多いと判断できます。
//
// PositionCorrectionCount:
//   有効なdenominator / DeltaLambdaを得た後、少なくとも1 Particleへ実際に位置補正した回数です。
//   DeltaLambdaCountとの差をDeltaLambdaRejectCountと比較することで、Position更新直前の脱落理由を確認できます。
struct SoftBodyParticleTriangleCollisionStatistics
{
    uint64_t CandidateCount = 0u;
    uint64_t NarrowPhaseCount = 0u;
    uint64_t DistanceCount = 0u;
    uint64_t ConstraintCount = 0u;
    uint64_t DenominatorRejectCount = 0u;
    uint64_t DeltaLambdaCount = 0u;
    uint64_t DeltaLambdaRejectCount = 0u;
    uint64_t PositionCorrectionCount = 0u;

    void Reset()
    {
        CandidateCount = 0u;
        NarrowPhaseCount = 0u;
        DistanceCount = 0u;
        ConstraintCount = 0u;
        DenominatorRejectCount = 0u;
        DeltaLambdaCount = 0u;
        DeltaLambdaRejectCount = 0u;
        PositionCorrectionCount = 0u;
    }
};

// ============================================================================
// Static / Kinematic Sphere Collision Constraint
// ============================================================================
// SoftBody側で扱うSphere Colliderです。
// Solver自身はSphereを移動させず、外部側がSetSphereCollider()でCenter/Radiusを更新します。
// そのため静的Sphereだけでなく、RigidBody Transformを毎フレーム同期したKinematicな境界としても使えます。
//
// ParticleLambdasは「Particle × Sphere」の片側XPBD Constraintが同一Step内で蓄積するLambdaです。
// Distance Constraintと同じくStep開始時に0へ戻し、Solver iteration間だけ保持します。
// Collisionは C(x) >= 0 の不等式なのでLambdaは常に0以上へclampし、接触が離れる方向へ動いた場合は
// Lambdaを減少させて拘束を解放できるようにします。
//
// AccumulatedReactionImpulse / ContactPointSum / ContactCount は1Stepだけ有効なTransient情報です。
// XPBDで得たDeltaLambdaから
//
//   reaction impulse ~= -normal * DeltaLambda / dt
//
// としてSphere側の反作用を蓄積します。以前の「位置補正量からImpulseを逆算する方式」と異なり、
// Constraint Solverが実際に適用したLambda増減を直接利用するため、反復回数による過大評価を抑えられます。
struct SoftBodySphereCollider
{
    math::Vec3 Center{};
    float Radius = 0.5f;

    std::vector<float> ParticleLambdas;

    math::Vec3 AccumulatedReactionImpulse{};
    math::Vec3 ContactPointSum{};
    uint32_t ContactCount = 0u;

    void ResetStepConstraintState(std::size_t particleCount)
    {
        ParticleLambdas.assign(particleCount, 0.0f);
        ResetStepFeedback();
    }

    void ResetStepFeedback()
    {
        AccumulatedReactionImpulse = math::Vec3{};
        ContactPointSum = math::Vec3{};
        ContactCount = 0u;
    }

    math::Vec3 GetAverageContactPoint() const
    {
        if (ContactCount == 0u)
        {
            return Center;
        }

        return ContactPointSum / static_cast<float>(ContactCount);
    }
};

// ============================================================================
// Static Plane Collision Constraint
// ============================================================================
// Plane式は dot(Normal, x) - Offset = 0 とします。
// 実際のCollision Constraintは共通Thicknessを加味して
//
//   C(x) = dot(Normal, x) - Offset - CollisionThickness >= 0
//
// として扱います。Sphereと同じく片側XPBD ConstraintとしてLambdaを0以上へclampするため、
// PlaneはParticleをNormal方向へ押せますが反対側へ引っ張りません。
// NormalはAdd/Set時に正規化し、ゼロベクトルが渡された場合は+Yを安全なfallbackとして使用します。
struct SoftBodyPlaneCollider
{
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Offset = 0.0f;

    // Particleごとの片側Constraint Lambdaです。
    // 現段階ではStepを跨ぐWarm Startは行わず、各Step開始時に0へ戻します。
    std::vector<float> ParticleLambdas;

    void ResetStepConstraintState(std::size_t particleCount)
    {
        ParticleLambdas.assign(particleCount, 0.0f);
    }
};

// ============================================================================
// Soft Body Solver
// ============================================================================
// RigidBodyのContactSolverとは分離した、Particle + Constraint用のXPBD Solverです。
//
// 通常Stepの基本順序:
//   1. 重力を積分してParticleの予測位置を作る
//   2. Distance / Dihedral / Sphere / PlaneのLambdaをStep用に初期化する
//   3. XPBD Distance Constraintを反復解決する
//   4. Dihedral Angle Bending Constraintを反復解決する
//   5. 同じ反復内でSphere / Plane Collisionを片側XPBD Constraintとして解決する
//   6. 最終PositionとStep開始時PositionからVelocityを再構築する
//
// Cloth用統合Stepでは上記のPosition反復へParticle-Particle / Particle-Triangle Self Collisionを追加し、
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
    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings) { m_Settings = settings; }
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

    // 直近のStepWithSelfCollisions()でStepローカルSTLコンテナが要求したTemporary allocation統計です。
    // Phase ②では通常Heapへの要求を計測し、Phase ③では同じCounterをFrameAllocator経由へ切り替えて
    // Before / Afterを同一指標で比較します。
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
    math::Vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<XPBDDihedralConstraint> m_DihedralConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
    SoftBodyParticleTriangleCollisionStatistics m_ParticleTriangleCollisionStatistics{};
    SolverTemporaryAllocationStatistics m_TemporaryAllocationStatistics{};

    // Particle-Triangle Flat HashはBucketごとにvector capacityを持つため、Stepローカルにすると
    // 毎frame最初のSolver iterationで巨大なBucket配列を再確保してしまいます。
    // Solver寿命まで保持し、frame間でもBucket / Triangle scratch capacityを再利用します。
    SoftBodyTriangleSpatialHashGrid m_ParticleTriangleSpatialHash{ 0.05f };
};

} // namespace ph
} // namespace Raven