#include "Raven/Physics/SoftBody/SoftBodySolver.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Raven
{
namespace ph
{
namespace
{
// ============================================================================
// Dihedral Angle Evaluation
// ============================================================================
// p2-p3を共有Edge、p0/p1をそれぞれ隣接Triangleの反対側頂点として二面角を計算します。
//
// GradientはPosition Based Dynamicsで一般的に使われるDihedral Constraintの微分形です。
// raw normalを|n|^2で割った量からd0..d3を構築し、現在のfold方向に応じて符号を合わせます。
// 退化Triangleや長さ0のEdgeではGradientを定義できないためfalseを返します。
bool EvaluateDihedralConstraint(
    const math::Vec3& p0,
    const math::Vec3& p1,
    const math::Vec3& p2,
    const math::Vec3& p3,
    float& outAngle,
    math::Vec3& outGradient0,
    math::Vec3& outGradient1,
    math::Vec3& outGradient2,
    math::Vec3& outGradient3)
{
    const math::Vec3 edge = p3 - p2;
    const float edgeLength = edge.Length();
    if (edgeLength <= math::Epsilon)
    {
        return false;
    }

    const math::Vec3 rawNormal0 = math::Vec3::Cross(p2 - p0, p3 - p0);
    const math::Vec3 rawNormal1 = math::Vec3::Cross(p3 - p1, p2 - p1);
    const float normalLengthSq0 = rawNormal0.LengthSq();
    const float normalLengthSq1 = rawNormal1.LengthSq();

    if (normalLengthSq0 <= math::Epsilon * math::Epsilon
        || normalLengthSq1 <= math::Epsilon * math::Epsilon)
    {
        return false;
    }

    const float inverseEdgeLength = 1.0f / edgeLength;

    // このscaled normalは単位法線ではありません。
    // Dihedral Angleの位置微分に必要な1/area相当のスケールを含みます。
    const math::Vec3 scaledNormal0 = rawNormal0 / normalLengthSq0;
    const math::Vec3 scaledNormal1 = rawNormal1 / normalLengthSq1;

    math::Vec3 gradient0 = scaledNormal0 * edgeLength;
    math::Vec3 gradient1 = scaledNormal1 * edgeLength;
    math::Vec3 gradient2 =
        scaledNormal0 * (math::Vec3::Dot(p0 - p3, edge) * inverseEdgeLength)
        + scaledNormal1 * (math::Vec3::Dot(p1 - p3, edge) * inverseEdgeLength);
    math::Vec3 gradient3 =
        scaledNormal0 * (math::Vec3::Dot(p2 - p0, edge) * inverseEdgeLength)
        + scaledNormal1 * (math::Vec3::Dot(p2 - p1, edge) * inverseEdgeLength);

    // 退化判定で求めた長さの二乗を再利用し、Normalize内でのLengthSq再計算を省きます。
    // sqrtの丸めで長さがちょうどEpsilonになる境界もあるため、従来のfallbackは維持します。
    // 成分ごとの除算も従来どおりにし、acosへ渡す値の丸めを変えないようにします。
    const float normalLength0 = std::sqrt(normalLengthSq0);
    const float normalLength1 = std::sqrt(normalLengthSq1);
    const math::Vec3 normal0 = normalLength0 <= math::Epsilon
        ? math::Vec3{} : rawNormal0 / normalLength0;
    const math::Vec3 normal1 = normalLength1 <= math::Epsilon
        ? math::Vec3{} : rawNormal1 / normalLength1;

    const float normalDot = std::clamp(
        math::Vec3::Dot(normal0, normal1),
        -1.0f,
        1.0f);
    outAngle = std::acos(normalDot);

    // acos(n0 dot n1)は角度の大きさだけを返します。
    // Gradient側へfold方向の符号を与えることで、どちら側へ折れていてもRestAngleへ戻る補正になります。
    const float orientationSign =
        math::Vec3::Dot(math::Vec3::Cross(normal0, normal1), edge) > 0.0f
        ? -1.0f
        : 1.0f;

    gradient0 *= orientationSign;
    gradient1 *= orientationSign;
    gradient2 *= orientationSign;
    gradient3 *= orientationSign;

    outGradient0 = gradient0;
    outGradient1 = gradient1;
    outGradient2 = gradient2;
    outGradient3 = gradient3;
    return true;
}

bool ComputeDihedralAngle(
    const math::Vec3& p0,
    const math::Vec3& p1,
    const math::Vec3& p2,
    const math::Vec3& p3,
    float& outAngle)
{
    math::Vec3 gradient0{};
    math::Vec3 gradient1{};
    math::Vec3 gradient2{};
    math::Vec3 gradient3{};

    return EvaluateDihedralConstraint(
        p0,
        p1,
        p2,
        p3,
        outAngle,
        gradient0,
        gradient1,
        gradient2,
        gradient3);
}
} // namespace

SoftBodySolver::SoftBodySolver() = default;

// SoftBodySolver クラスのメンバ変数である m_TemporaryFrameAllocator (FrameAllocator) は、
// メモリバッファの所有権を持つためコピーコンストラクタおよびコピー代入演算子が = delete（削除）指定されてコピー禁止となっています。
//そのため、SoftBodySolver の暗黙のコピーコンストラクタも削除された扱いとなり、SoftBodySolver のオブジェクトをコピーしようとした際にコンパイルエラーが発生していました。

// SoftBodySolver.cppにカスタムコピー処理を実装しました。
// コピー時にはコピー不可な FrameAllocator を新規キャパシティで初期化し、各種設定・状態（m_Particles, m_DistanceConstraints, m_Settings 等）を複製した上で、
// バックアロケータの参照関係を再設定するように調整を行うよう対応を正しく再設定するようにしています。
SoftBodySolver::SoftBodySolver(const SoftBodySolver& other)
    : m_Gravity(other.m_Gravity)
    , m_Settings(other.m_Settings)
    , m_Particles(other.m_Particles)
    , m_DistanceConstraints(other.m_DistanceConstraints)
    , m_DihedralConstraints(other.m_DihedralConstraints)
    , m_SphereColliders(other.m_SphereColliders)
    , m_PlaneColliders(other.m_PlaneColliders)
    , m_ParticleTriangleCollisionStatistics(other.m_ParticleTriangleCollisionStatistics)
    , m_TemporaryFrameAllocator(SoftBodyTemporaryFrameAllocatorCapacity)
    , m_TemporaryAllocationStatistics(other.m_TemporaryAllocationStatistics)
    , m_SelfCollisionTriangles(other.m_SelfCollisionTriangles)
    , m_SelfCollisionTriangleRows(other.m_SelfCollisionTriangleRows)
    , m_SelfCollisionTriangleColumns(other.m_SelfCollisionTriangleColumns)
    , m_SelfCollisionExcludedParticlePairs(other.m_SelfCollisionExcludedParticlePairs)
    , m_SelfCollisionExcludedParticlePairsDirty(other.m_SelfCollisionExcludedParticlePairsDirty)
    , m_ParticleSpatialHash(other.m_ParticleSpatialHash)
    , m_ParticleTriangleSpatialHash(other.m_ParticleTriangleSpatialHash)
{
    // Candidate内容は物理状態ではないためコピーせず、再利用容量だけを引き継ぎます。
    // Benchmark snapshotでも定常実行と同じcapacity条件を作り、初回growを計測へ混ぜません。
    m_ParticleCandidatePairs.reserve(other.m_ParticleCandidatePairs.capacity());
    m_ParticleTriangleCandidatePairs.reserve(other.m_ParticleTriangleCandidatePairs.capacity());

    if (m_Settings.TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::FrameAllocator)
    {
        m_TemporaryAllocationStatistics.SetBackingAllocator(&m_TemporaryFrameAllocator);
    }
    else
    {
        m_TemporaryAllocationStatistics.SetBackingAllocator(nullptr);
    }
}

SoftBodySolver& SoftBodySolver::operator=(const SoftBodySolver& other)
{
    if (this != &other)
    {
        m_Gravity = other.m_Gravity;
        m_Settings = other.m_Settings;
        m_Particles = other.m_Particles;
        m_DistanceConstraints = other.m_DistanceConstraints;
        m_DihedralConstraints = other.m_DihedralConstraints;
        m_SphereColliders = other.m_SphereColliders;
        m_PlaneColliders = other.m_PlaneColliders;
        m_ParticleTriangleCollisionStatistics = other.m_ParticleTriangleCollisionStatistics;

        m_TemporaryFrameAllocator.Reset();
        m_TemporaryAllocationStatistics = other.m_TemporaryAllocationStatistics;
        if (m_Settings.TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::FrameAllocator)
        {
            m_TemporaryAllocationStatistics.SetBackingAllocator(&m_TemporaryFrameAllocator);
        }
        else
        {
            m_TemporaryAllocationStatistics.SetBackingAllocator(nullptr);
        }

        m_SelfCollisionTriangles = other.m_SelfCollisionTriangles;
        m_SelfCollisionTriangleRows = other.m_SelfCollisionTriangleRows;
        m_SelfCollisionTriangleColumns = other.m_SelfCollisionTriangleColumns;
        m_SelfCollisionExcludedParticlePairs = other.m_SelfCollisionExcludedParticlePairs;
        m_SelfCollisionExcludedParticlePairsDirty =
            other.m_SelfCollisionExcludedParticlePairsDirty;
        m_ParticleSpatialHash = other.m_ParticleSpatialHash;
        m_ParticleTriangleSpatialHash = other.m_ParticleTriangleSpatialHash;

        // Candidate列は直前iterationの一時結果であり、コピー先の物理状態には不要です。
        // 特にCell Size BenchmarkはSolverを複製するため、大きな候補列までコピーすると
        // 計測開始前に不要なO(candidate count)の仕事が発生します。次Stepで再生成する空状態にします。
        m_ParticleCandidatePairs.clear();
        m_ParticleTriangleCandidatePairs.clear();
        m_ParticleCandidatePairs.reserve(other.m_ParticleCandidatePairs.capacity());
        m_ParticleTriangleCandidatePairs.reserve(
            other.m_ParticleTriangleCandidatePairs.capacity());
    }

    return *this;
}

uint32_t SoftBodySolver::AddParticle(const math::Vec3& position, float inverseMass)
{
    SoftBodyParticle particle{};
    particle.Position = position;
    particle.PreviousPosition = position;
    particle.Velocity = math::Vec3{};

    // 負の逆質量には物理的な意味がないため0へclampします。
    // InverseMass == 0.0f は固定Particleとして扱われます。
    particle.InverseMass = std::max(0.0f, inverseMass);

    m_Particles.push_back(particle);
    return static_cast<uint32_t>(m_Particles.size() - 1u);
}

uint32_t SoftBodySolver::AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance)
{
    assert(particleA < m_Particles.size());
    assert(particleB < m_Particles.size());
    assert(particleA != particleB);

    XPBDDistanceConstraint constraint{};
    constraint.ParticleA = particleA;
    constraint.ParticleB = particleB;

    // Constraint登録時の距離を自然長として保存します。
    // Cloth Builderは初期形状を作った直後にConstraintを張るため、この値が未変形時の長さになります。
    constraint.RestLength = (m_Particles[particleB].Position - m_Particles[particleA].Position).Length();
    constraint.Compliance = std::max(0.0f, compliance);
    constraint.Lambda = 0.0f;

    m_DistanceConstraints.push_back(constraint);
    // 次の自己衝突Stepで除外Pair Cacheを1回だけ再構築します。
    m_SelfCollisionExcludedParticlePairsDirty = true;
    return static_cast<uint32_t>(m_DistanceConstraints.size() - 1u);
}

uint32_t SoftBodySolver::AddDihedralConstraint(
    uint32_t oppositeA,
    uint32_t oppositeB,
    uint32_t edgeA,
    uint32_t edgeB,
    float compliance)
{
    assert(oppositeA < m_Particles.size());
    assert(oppositeB < m_Particles.size());
    assert(edgeA < m_Particles.size());
    assert(edgeB < m_Particles.size());

    assert(oppositeA != oppositeB);
    assert(oppositeA != edgeA);
    assert(oppositeA != edgeB);
    assert(oppositeB != edgeA);
    assert(oppositeB != edgeB);
    assert(edgeA != edgeB);

    XPBDDihedralConstraint constraint{};
    constraint.OppositeA = oppositeA;
    constraint.OppositeB = oppositeB;
    constraint.EdgeA = edgeA;
    constraint.EdgeB = edgeB;
    constraint.Compliance = std::max(0.0f, compliance);
    constraint.Lambda = 0.0f;

    // 初期Topologyの角度をRestAngleにします。
    // 退化Triangleの場合は0を保持し、Solver側でもGradient評価時に安全にスキップします。
    float restAngle = 0.0f;
    if (ComputeDihedralAngle(
            m_Particles[oppositeA].Position,
            m_Particles[oppositeB].Position,
            m_Particles[edgeA].Position,
            m_Particles[edgeB].Position,
            restAngle))
    {
        constraint.RestAngle = restAngle;
    }

    m_DihedralConstraints.push_back(constraint);
    return static_cast<uint32_t>(m_DihedralConstraints.size() - 1u);
}

uint32_t SoftBodySolver::AddSphereCollider(const math::Vec3& center, float radius)
{
    SoftBodySphereCollider collider{};
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);

    // Collider追加時にも現在のParticle数へ合わせてLambda配列を初期化しておきます。
    // Step冒頭で再度Resetするため、途中でParticle数が増えた場合も次Stepから整合します。
    collider.ResetStepConstraintState(m_Particles.size());

    m_SphereColliders.push_back(std::move(collider));
    return static_cast<uint32_t>(m_SphereColliders.size() - 1u);
}

void SoftBodySolver::SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius)
{
    if (colliderIndex >= m_SphereColliders.size())
    {
        return;
    }

    // Position/Radiusだけを更新し、直前StepのFeedbackは次のStep冒頭まで残します。
    // 外部側がStep直後にFeedbackを読む時間を確保するため、Set時には消さないことが重要です。
    SoftBodySphereCollider& collider = m_SphereColliders[colliderIndex];
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);
}

void SoftBodySolver::ClearSphereColliders()
{
    m_SphereColliders.clear();
}

uint32_t SoftBodySolver::AddPlaneCollider(const math::Vec3& normal, float offset)
{
    SoftBodyPlaneCollider collider{};
    collider.Normal = normal;
    collider.Offset = offset;

    // Planeのsigned distance計算はNormalが単位ベクトルであることを前提にします。
    // ゼロベクトルの場合は正規化できないため、+Y Planeを安全なfallbackとして使用します。
    if (collider.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        collider.Normal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        collider.Normal.Normalize();
    }

    // Sphereと同様にParticleごとの片側Constraint Lambdaを持ちます。
    // Step冒頭で毎回初期化しますが、追加直後も配列サイズを揃えておきます。
    collider.ResetStepConstraintState(m_Particles.size());

    m_PlaneColliders.push_back(std::move(collider));
    return static_cast<uint32_t>(m_PlaneColliders.size() - 1u);
}

void SoftBodySolver::SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset)
{
    if (colliderIndex >= m_PlaneColliders.size())
    {
        return;
    }

    SoftBodyPlaneCollider& collider = m_PlaneColliders[colliderIndex];
    collider.Normal = normal;
    collider.Offset = offset;

    if (collider.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        collider.Normal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        collider.Normal.Normalize();
    }
}

void SoftBodySolver::ClearPlaneColliders()
{
    m_PlaneColliders.clear();
}

void SoftBodySolver::Clear()
{
    // SolverのTopologyを完全に作り直すためのClearです。
    // Colliderも同時に消えるため、Deformer側はCloth再構築後に保持している設定を再登録します。
    m_Particles.clear();
    m_DistanceConstraints.clear();
    m_DihedralConstraints.clear();
    m_SphereColliders.clear();
    m_PlaneColliders.clear();

    // Cloth自己衝突TriangleはRows / Columnsだけで再利用可否を判定する永続Topology Cacheです。
    // SolverをClearしてParticle/Constraintを作り直した後に古いIndex列を再利用すると、
    // 同じRows / Columnsでも異なるParticle Topologyを参照する可能性があります。
    // Clear()をSolver全体の再初期化境界として扱い、Cache本体と識別値を必ず同時に無効化します。
    m_SelfCollisionTriangles.clear();
    m_SelfCollisionTriangleRows = 0u;
    m_SelfCollisionTriangleColumns = 0u;
    m_SelfCollisionExcludedParticlePairs.clear();
    m_SelfCollisionExcludedParticlePairsDirty = true;
    m_ParticleSpatialHash.Clear();
    m_ParticleCandidatePairs.clear();
    m_ParticleTriangleCandidatePairs.clear();

    // Debug / Profiler側へ再構築前の自己衝突件数やTemporary allocation値を残しません。
    // Temporary Statistics::Reset()はFrameAllocator ModeではArenaも巻き戻すため、
    // 次に構築されるSoftBodyが以前のStep-local memory使用量を引き継がないことも保証します。
    m_ParticleTriangleCollisionStatistics.Reset();
    m_TemporaryAllocationStatistics.Reset();
}

void SoftBodySolver::Step(float deltaTime)
{
    // XPBDでは alphaTilde = compliance / dt^2 を使用するため、dt <= 0では計算できません。
    // Pauseや初期化時に0秒更新が来ても安全に終了します。
    if (deltaTime <= 0.0f)
    {
        return;
    }

    PredictPositions(deltaTime);
    ResetConstraintLambdas();
    ResetCollisionConstraintState();

    // Position Based Dynamicsでは、予測位置に対してConstraintを複数回反復して収束させます。
    // Internal ConstraintとCollisionを同じ反復へ含めることが重要です。
    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // Stretch/Shearを解いた後にBendingを解き、最後にCollisionで外部形状から押し戻します。
        // 次iterationではこの結果を再度全Constraintが見るため、互いの補正が収束していきます。
        SolveDistanceConstraints(deltaTime);
        SolveDihedralConstraints(deltaTime);
        SolveSphereCollisions(deltaTime);
        SolvePlaneCollisions(deltaTime);
    }

    UpdateVelocities(deltaTime);
}

void SoftBodySolver::PredictPositions(float deltaTime)
{
    for (SoftBodyParticle& particle : m_Particles)
    {
        // PreviousPositionには「今回のStep開始時のPosition」を保存します。
        // Constraint補正後の最終Positionとの差からVelocityを再構築することで、Constraintによって
        // 生じた移動量も次フレームの運動へ反映できます。
        particle.PreviousPosition = particle.Position;

        if (particle.IsFixed())
        {
            // 固定点は外力でも移動させず、Velocityも残さないよう0へ戻します。
            particle.Velocity = math::Vec3{};
            continue;
        }

        // Semi-implicit Eulerに近い順序で、先にVelocityへ重力を加えてからPositionを予測します。
        particle.Velocity += m_Gravity * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

void SoftBodySolver::ResetConstraintLambdas()
{
    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        // Lambdaは同一Step内のiteration間では蓄積しますが、現在はStepを跨ぐWarm Startを行いません。
        // そのため各Step開始時に0へ戻します。
        constraint.Lambda = 0.0f;
    }

    for (XPBDDihedralConstraint& constraint : m_DihedralConstraints)
    {
        // Dihedral Bendingも同じXPBDルールで、Lambdaは同一Step内だけ蓄積します。
        constraint.Lambda = 0.0f;
    }
}

void SoftBodySolver::ResetCollisionConstraintState()
{
    for (SoftBodySphereCollider& collider : m_SphereColliders)
    {
        // Sphere CollisionもDistance Constraintと同様にLambdaを同一Step内だけ保持します。
        // Particle数へ合わせて毎Step初期化することで、Cloth再構築やParticle追加後もIndex対応を保証します。
        // Feedbackも同時にResetし、Step間でReaction Impulseが累積しないようにします。
        collider.ResetStepConstraintState(m_Particles.size());
    }

    for (SoftBodyPlaneCollider& collider : m_PlaneColliders)
    {
        // PlaneもSphereと同じ片側XPBD Constraintへ統一します。
        // 現段階ではWarm Startしないため、各Step開始時にParticle別Lambdaを0へ戻します。
        collider.ResetStepConstraintState(m_Particles.size());
    }
}

void SoftBodySolver::SolveDistanceConstraints(float deltaTime)
{
    const float deltaTimeSq = deltaTime * deltaTime;
    const float inverseDeltaTimeSq = 1.0f / deltaTimeSq;

    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        assert(constraint.ParticleA < m_Particles.size());
        assert(constraint.ParticleB < m_Particles.size());

        SoftBodyParticle& particleA = m_Particles[constraint.ParticleA];
        SoftBodyParticle& particleB = m_Particles[constraint.ParticleB];

        const float inverseMassSum = particleA.InverseMass + particleB.InverseMass;
        if (inverseMassSum <= 0.0f)
        {
            // 両端が固定点ならPositionを補正できないため、このConstraintは解く必要がありません。
            continue;
        }

        const math::Vec3 delta = particleB.Position - particleA.Position;
        const float distance = delta.Length();
        if (distance <= math::Epsilon)
        {
            // 2点がほぼ同位置の場合はConstraint方向を決められないためNaN防止でスキップします。
            continue;
        }

        const math::Vec3 normal = delta / distance;

        // C(x) = |xB - xA| - RestLength
        // C=0が制約を満たす状態です。正なら伸び、負なら縮みを表します。
        const float constraintValue = distance - constraint.RestLength;

        // XPBDの中心式:
        //   alphaTilde = compliance / dt^2
        //   deltaLambda = (-C - alphaTilde * lambda)
        //                 / (wA + wB + alphaTilde)
        //
        // compliance=0なら硬いPBD距離制約となり、値を大きくすると柔らかくなります。
        // deltaTimeはiteration中不変です。全Constraintで同じ除算を繰り返さず、
        // 反復冒頭で求めた逆数との乗算にしてHot loopの除算を削減します。
        const float alphaTilde = constraint.Compliance * inverseDeltaTimeSq;
        const float denominator = inverseMassSum + alphaTilde;

        if (denominator <= math::Epsilon)
        {
            continue;
        }

        const float deltaLambda =
            (-constraintValue - alphaTilde * constraint.Lambda) / denominator;
        constraint.Lambda += deltaLambda;

        // C = |xB-xA|-L のgradientは A側=-n, B側=+n です。
        // deltaX = inverseMass * gradient(C) * deltaLambda を各Particleへ適用します。
        if (particleA.IsFixed() == false)
        {
            particleA.Position -= normal * (particleA.InverseMass * deltaLambda);
        }

        if (particleB.IsFixed() == false)
        {
            particleB.Position += normal * (particleB.InverseMass * deltaLambda);
        }
    }
}

void SoftBodySolver::SolveDihedralConstraints(float deltaTime)
{
    const float deltaTimeSq = deltaTime * deltaTime;
    const float inverseDeltaTimeSq = 1.0f / deltaTimeSq;

    for (XPBDDihedralConstraint& constraint : m_DihedralConstraints)
    {
        assert(constraint.OppositeA < m_Particles.size());
        assert(constraint.OppositeB < m_Particles.size());
        assert(constraint.EdgeA < m_Particles.size());
        assert(constraint.EdgeB < m_Particles.size());

        SoftBodyParticle& particle0 = m_Particles[constraint.OppositeA];
        SoftBodyParticle& particle1 = m_Particles[constraint.OppositeB];
        SoftBodyParticle& particle2 = m_Particles[constraint.EdgeA];
        SoftBodyParticle& particle3 = m_Particles[constraint.EdgeB];

        float angle = 0.0f;
        math::Vec3 gradient0{};
        math::Vec3 gradient1{};
        math::Vec3 gradient2{};
        math::Vec3 gradient3{};

        if (EvaluateDihedralConstraint(
                particle0.Position,
                particle1.Position,
                particle2.Position,
                particle3.Position,
                angle,
                gradient0,
                gradient1,
                gradient2,
                gradient3) == false)
        {
            continue;
        }

        // ====================================================================
        // XPBD Dihedral Angle Constraint
        // ====================================================================
        // C(x) = currentAngle - RestAngle = 0
        //
        // Distance Constraintと同じXPBD式ですが、Gradientが4Particleへ分配されます。
        // denominatorには各Particleの inverseMass * |gradient|^2 を足し合わせます。
        const float constraintValue = angle - constraint.RestAngle;
        const float alphaTilde =
            std::max(0.0f, constraint.Compliance) * inverseDeltaTimeSq;

        const float denominator =
            particle0.InverseMass * gradient0.LengthSq()
            + particle1.InverseMass * gradient1.LengthSq()
            + particle2.InverseMass * gradient2.LengthSq()
            + particle3.InverseMass * gradient3.LengthSq()
            + alphaTilde;

        if (denominator <= math::Epsilon)
        {
            continue;
        }

        const float deltaLambda =
            (-constraintValue - alphaTilde * constraint.Lambda) / denominator;
        constraint.Lambda += deltaLambda;

        if (particle0.IsFixed() == false)
        {
            particle0.Position += gradient0 * (particle0.InverseMass * deltaLambda);
        }

        if (particle1.IsFixed() == false)
        {
            particle1.Position += gradient1 * (particle1.InverseMass * deltaLambda);
        }

        if (particle2.IsFixed() == false)
        {
            particle2.Position += gradient2 * (particle2.InverseMass * deltaLambda);
        }

        if (particle3.IsFixed() == false)
        {
            particle3.Position += gradient3 * (particle3.InverseMass * deltaLambda);
        }
    }
}

void SoftBodySolver::SolveSphereCollisions(float deltaTime)
{
    if (m_SphereColliders.empty())
    {
        return;
    }

    const float collisionThickness = std::max(0.0f, m_Settings.CollisionThickness);
    const float collisionCompliance = std::max(0.0f, m_Settings.SphereCollisionCompliance);
    const float alphaTilde = collisionCompliance / (deltaTime * deltaTime);

    // Particle IndexはCollider側ParticleLambdasのIndexとしても使用するため、range-forではなく
    // 明示的なIndexループにしています。
    for (std::size_t particleIndex = 0u; particleIndex < m_Particles.size(); ++particleIndex)
    {
        SoftBodyParticle& particle = m_Particles[particleIndex];
        if (particle.IsFixed())
        {
            continue;
        }

        for (SoftBodySphereCollider& collider : m_SphereColliders)
        {
            if (particleIndex >= collider.ParticleLambdas.size())
            {
                // 通常はStep冒頭のResetCollisionConstraintState()で必ず一致します。
                // 不整合時は範囲外アクセスを避け、このColliderだけスキップします。
                continue;
            }

            // Clothの厚み分だけSphereを膨らませた半径を接触面として扱います。
            const float targetRadius = collider.Radius + collisionThickness;
            if (targetRadius <= 0.0f)
            {
                continue;
            }

            const math::Vec3 centerToParticle = particle.Position - collider.Center;
            const float distanceSq = centerToParticle.LengthSq();

            // Sphere中心とParticleが完全一致するとgradient方向を決められません。
            // その場合だけWorld Upをfallbackにし、0除算とNaNを防ぎます。
            float distance = 0.0f;
            math::Vec3 normal{ 0.0f, 1.0f, 0.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                distance = std::sqrt(distanceSq);
                normal = centerToParticle / distance;
            }

            // ====================================================================
            // Unilateral XPBD Sphere Constraint
            // ====================================================================
            // Collision Constraintは等式ではなく片側不等式です。
            //
            //   C(x) = |x - center| - targetRadius >= 0
            //
            // Sphere外側ではC>=0なので拘束力は不要、内部ではC<0なので外向き補正が必要です。
            // XPBD式そのものはDistance Constraintと同じですが、Collision Lambdaは押す方向だけを
            // 許可するため newLambda >= 0 にclampします。
            //
            // 既にLambda>0のParticleが他ConstraintによってSphere外へ移動した場合も式を評価し、
            // Lambdaを減らして接触拘束を解放できることが重要です。
            const float constraintValue = distance - targetRadius;
            float& lambda = collider.ParticleLambdas[particleIndex];

            if (constraintValue >= 0.0f && lambda <= 0.0f)
            {
                continue;
            }

            const float denominator = particle.InverseMass + alphaTilde;
            if (denominator <= math::Epsilon)
            {
                continue;
            }

            const float unconstrainedDeltaLambda =
                (-constraintValue - alphaTilde * lambda) / denominator;

            const float oldLambda = lambda;
            const float newLambda = std::max(0.0f, oldLambda + unconstrainedDeltaLambda);
            const float appliedDeltaLambda = newLambda - oldLambda;
            lambda = newLambda;

            if (std::abs(appliedDeltaLambda) <= math::Epsilon)
            {
                continue;
            }

            // gradient C = normal なので、Particle側Position補正は
            //   deltaX = inverseMass * normal * DeltaLambda
            // です。硬いConstraint(compliance=0)なら初回反復でほぼSphere表面まで戻りますが、
            // Lambdaとして解くことでCompliance導入と反作用計算を同じ式へ統一できます。
            particle.Position +=
                normal * (particle.InverseMass * appliedDeltaLambda);

            // ====================================================================
            // XPBD Lambda -> Soft/Rigid reaction impulse
            // ====================================================================
            // XPBD LambdaはPosition Constraintで蓄積される量で、Force相当は lambda / dt^2、
            // 1Step分Impulse相当は lambda / dt とみなせます。
            // ここではiterationごとのDeltaLambdaを積み上げることで、最終Lambdaに対応した
            // 反作用ImpulseをSphere側へ返します。
            //
            // Particleへは +normal 方向のConstraint Impulseが作用するため、Sphereへの反作用は
            // -normal方向です。以前の「correction * mass / dt」方式よりSolverが実際に適用した
            // Constraint量へ直接結び付いており、iteration回数による二重評価を抑えられます。
            collider.AccumulatedReactionImpulse -=
                normal * (appliedDeltaLambda / deltaTime);

            if (newLambda > 0.0f)
            {
                // Thicknessを除いた実Sphere表面をContact Pointとして使用します。
                // 後でRigidBodyへAddImpulseAtPoint()すると、中心から外れた接触は回転にも寄与します。
                collider.ContactPointSum += collider.Center + normal * collider.Radius;
                ++collider.ContactCount;
            }
        }
    }
}

void SoftBodySolver::SolvePlaneCollisions(float deltaTime)
{
    if (m_PlaneColliders.empty())
    {
        return;
    }

    const float collisionThickness = std::max(0.0f, m_Settings.CollisionThickness);
    const float collisionCompliance = std::max(0.0f, m_Settings.PlaneCollisionCompliance);
    const float alphaTilde = collisionCompliance / (deltaTime * deltaTime);

    for (std::size_t particleIndex = 0u; particleIndex < m_Particles.size(); ++particleIndex)
    {
        SoftBodyParticle& particle = m_Particles[particleIndex];
        if (particle.IsFixed())
        {
            continue;
        }

        for (SoftBodyPlaneCollider& collider : m_PlaneColliders)
        {
            if (particleIndex >= collider.ParticleLambdas.size())
            {
                continue;
            }

            // ====================================================================
            // Unilateral XPBD Plane Constraint
            // ====================================================================
            // Planeの許容側条件をConstraintとして書くと
            //
            //   C(x) = dot(n, x) - offset - thickness >= 0
            //
            // です。NormalはAdd/Set時に正規化済みなのでgradient C = n、|gradient|^2 = 1です。
            // Sphereと同じくLambdaを0以上へclampし、床から離れたParticleを引き戻さないようにします。
            const float constraintValue =
                math::Vec3::Dot(collider.Normal, particle.Position)
                - collider.Offset
                - collisionThickness;

            float& lambda = collider.ParticleLambdas[particleIndex];

            if (constraintValue >= 0.0f && lambda <= 0.0f)
            {
                continue;
            }

            const float denominator = particle.InverseMass + alphaTilde;
            if (denominator <= math::Epsilon)
            {
                continue;
            }

            const float unconstrainedDeltaLambda =
                (-constraintValue - alphaTilde * lambda) / denominator;

            const float oldLambda = lambda;
            const float newLambda = std::max(0.0f, oldLambda + unconstrainedDeltaLambda);
            const float appliedDeltaLambda = newLambda - oldLambda;
            lambda = newLambda;

            if (std::abs(appliedDeltaLambda) <= math::Epsilon)
            {
                continue;
            }

            // gradient C = Normalなので、Sphereと同じ形式でPosition補正できます。
            // compliance=0なら硬い床、値を増やすとConstraintが柔らかくなり沈み込みを許します。
            particle.Position +=
                collider.Normal * (particle.InverseMass * appliedDeltaLambda);
        }
    }
}

void SoftBodySolver::UpdateVelocities(float deltaTime)
{
    const float inverseDeltaTime = 1.0f / deltaTime;

    for (SoftBodyParticle& particle : m_Particles)
    {
        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        // XPBDではConstraintがPositionを直接補正します。
        // 最終位置とStep開始時位置の差からVelocityを再構築することで、Constraint補正による移動も
        // 次Stepへ正しく引き継ぎます。
        particle.Velocity = (particle.Position - particle.PreviousPosition) * inverseDeltaTime;
    }
}

} // namespace ph
} // namespace Raven
