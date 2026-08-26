#include <cassert>
#include <cstddef>
#include <vector>

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Core/Memory/FrameVector.h"
#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

namespace Raven::ph::tests
{

// ============================================================================
// Physics Frame Allocator Self Tests
// ============================================================================
// BroadPhasePairを実際にFrameAllocatorから確保し、PhysicsWorldへ接続する前提となる
// 「可変長Pair列」「Alignment」「統計」「Reset後の一括再利用」を確認します。
//
// このテストではResetFrame()後の古いPairを意図的に参照しません。
// FrameAllocatorの契約上、Reset後のdata()は次のAllocate()で上書きされる可能性があり、
// Debug Snapshotなどフレームを跨いで必要なデータは通常の永続vectorへコピーする必要があります。
void RunPhysicsFrameAllocatorSelfTests()
{
    constexpr std::size_t Capacity = 4u * 1024u;
    FrameAllocator allocator(Capacity);

    using PairVector = FrameVector<BroadPhasePair>;
    PairVector pairs{ STLAllocatorAdapter<BroadPhasePair>(allocator) };

    assert(allocator.GetCapacity() == Capacity);
    assert(allocator.GetUsedMemory() == 0u);
    assert(allocator.GetAllocationCount() == 0u);

    // vectorのgrowによって複数回Allocateされても、すべて同じFrameAllocatorから
    // 確保されることを確認します。Linear系Allocatorでは古いbufferは個別解放せず、
    // 最後にResetFrame()でまとめて再利用します。
    for (uint32_t i = 0u; i < 32u; ++i)
    {
        BroadPhasePair pair{};
        pairs.push_back(pair);
    }

    assert(pairs.size() == 32u);
    assert(allocator.GetUsedMemory() > 0u);
    assert(allocator.GetAllocationCount() > 0u);

    const std::size_t usedBeforeReset = allocator.GetUsedMemory();
    const std::size_t peakBeforeReset = allocator.GetPeakUsedMemory();
    const std::size_t allocationsBeforeReset = allocator.GetAllocationCount();

    assert(peakBeforeReset >= usedBeforeReset);
    assert(allocationsBeforeReset > 0u);

    // FrameVectorのdestructorは要素破棄を行いますが、FrameAllocatorのDeallocateはno-opです。
    // そのためResetFrame()はvectorが寿命を終えた後に行うのが最も分かりやすく安全です。
    pairs.clear();
    pairs.shrink_to_fit();

    allocator.ResetFrame();

    assert(allocator.GetUsedMemory() == 0u);
    assert(allocator.GetAllocationCount() == 0u);

    // Peakは容量チューニングのためReset後も保持します。
    assert(allocator.GetPeakUsedMemory() == peakBeforeReset);

    // ========================================================================
    // Solver Temporary Allocator: Phase ② Before計測
    // ========================================================================
    // Backing Allocatorを指定しない場合は通常Heapを使用します。
    // この状態がFrameAllocator適用前のBefore計測条件です。
    SolverTemporaryAllocationStatistics heapStatistics{};
    using TemporaryIntAllocator = SolverTemporaryAllocator<int>;

    {
        std::vector<int, TemporaryIntAllocator> temporaryValues{
            TemporaryIntAllocator(&heapStatistics)
        };

        temporaryValues.reserve(64u);
        for (int value = 0; value < 64; ++value)
        {
            temporaryValues.push_back(value);
        }

        assert(temporaryValues.size() == 64u);
        assert(heapStatistics.AllocationCount > 0u);
        assert(heapStatistics.AllocationBytes >= 64u * sizeof(int));
        assert(heapStatistics.ActiveBytes > 0u);
        assert(heapStatistics.PeakActiveBytes >= heapStatistics.ActiveBytes);
        assert(heapStatistics.GetBackingAllocator() == nullptr);
        assert(heapStatistics.GetBackingUsedMemory() == 0u);
        assert(heapStatistics.GetRecommendedBackingCapacity() == 0u);
    }

    // std::allocator経路ではvector破棄時に個別解放されるため、scope終了後のActiveBytesは0になります。
    assert(heapStatistics.DeallocationCount > 0u);
    assert(heapStatistics.ActiveBytes == 0u);
    assert(heapStatistics.AllocationBytes == heapStatistics.DeallocationBytes);

    // ========================================================================
    // Solver Candidate Vector: SpatialHashからCounter付きvectorへ直接出力
    // ========================================================================
    // 中間std::vectorへ一度生成すると、そのHeap allocationがCounterから漏れます。
    // ここでは実際のSoftBodySpatialHashGridからSolverTemporaryAllocator付きvectorへ直接pushし、
    // Candidate生成経路そのものがCounterへ接続されていることを確認します。
    SolverTemporaryAllocationStatistics particleCandidateStatistics{};
    using ParticleCandidateAllocator = SolverTemporaryAllocator<SoftBodySpatialHashPair>;
    using ParticleCandidateVector = std::vector<SoftBodySpatialHashPair, ParticleCandidateAllocator>;

    {
        std::vector<SoftBodyParticle> particles(2u);
        particles[0].Position = math::Vec3{ 0.0f, 0.0f, 0.0f };
        particles[1].Position = math::Vec3{ 0.01f, 0.0f, 0.0f };

        SoftBodySpatialHashGrid spatialHash(0.05f);
        spatialHash.Build(particles);

        ParticleCandidateVector candidatePairs{
            ParticleCandidateAllocator(&particleCandidateStatistics) };
        spatialHash.GenerateCandidatePairs(candidatePairs);

        assert(candidatePairs.size() == 1u);
        assert(candidatePairs[0].ParticleA == 0u);
        assert(candidatePairs[0].ParticleB == 1u);
        assert(particleCandidateStatistics.AllocationCount > 0u);
        assert(particleCandidateStatistics.AllocationBytes >= sizeof(SoftBodySpatialHashPair));
    }

    assert(particleCandidateStatistics.ActiveBytes == 0u);
    assert(
        particleCandidateStatistics.AllocationBytes
        == particleCandidateStatistics.DeallocationBytes);

    // Particle-Triangle側も同じAllocator型Overloadがリンクされることを確認します。
    // 4番目のParticleをTriangle面の近傍へ置き、Topology除外されない候補を1つ以上生成します。
    SolverTemporaryAllocationStatistics triangleCandidateStatistics{};
    using TriangleCandidateAllocator = SolverTemporaryAllocator<SoftBodyParticleTrianglePair>;
    using TriangleCandidateVector =
        std::vector<SoftBodyParticleTrianglePair, TriangleCandidateAllocator>;

    {
        std::vector<SoftBodyParticle> particles(4u);
        particles[0].Position = math::Vec3{ 0.0f, 0.0f, 0.0f };
        particles[1].Position = math::Vec3{ 0.04f, 0.0f, 0.0f };
        particles[2].Position = math::Vec3{ 0.0f, 0.04f, 0.0f };
        particles[3].Position = math::Vec3{ 0.01f, 0.01f, 0.005f };

        std::vector<SoftBodyTriangle> triangles(1u);
        triangles[0] = SoftBodyTriangle{ 0u, 1u, 2u };

        SoftBodyTriangleSpatialHashGrid triangleSpatialHash(0.05f);
        triangleSpatialHash.BuildTriangles(particles, triangles, 0.01f);

        TriangleCandidateVector candidatePairs{
            TriangleCandidateAllocator(&triangleCandidateStatistics) };
        triangleSpatialHash.GenerateParticleTriangleCandidates(particles, candidatePairs);

        assert(candidatePairs.empty() == false);
        assert(triangleCandidateStatistics.AllocationCount > 0u);
        assert(
            triangleCandidateStatistics.AllocationBytes
            >= sizeof(SoftBodyParticleTrianglePair));
    }

    assert(triangleCandidateStatistics.ActiveBytes == 0u);
    assert(
        triangleCandidateStatistics.AllocationBytes
        == triangleCandidateStatistics.DeallocationBytes);

    // ========================================================================
    // Solver Temporary Allocator: Phase ③ FrameAllocator経路
    // ========================================================================
    // STL側のAllocator型とCounterはBeforeと同一です。
    // Phase ③ではBackingを各STL Containerへ個別に渡すのではなく、Solver所有Statisticsへ1回登録し、
    // unordered_mapのrebindを含む全SolverTemporaryAllocatorが同じArenaを継承する構成です。
    FrameAllocator solverFrameAllocator(Capacity);
    SolverTemporaryAllocationStatistics frameStatistics{ &solverFrameAllocator };

    {
        std::vector<int, TemporaryIntAllocator> temporaryValues{
            TemporaryIntAllocator(&frameStatistics)
        };

        temporaryValues.reserve(64u);
        for (int value = 0; value < 64; ++value)
        {
            temporaryValues.push_back(value);
        }

        assert(temporaryValues.size() == 64u);
        assert(frameStatistics.AllocationCount > 0u);
        assert(frameStatistics.AllocationBytes >= 64u * sizeof(int));
        assert(frameStatistics.GetBackingAllocator() == &solverFrameAllocator);
        assert(frameStatistics.GetBackingCapacity() == Capacity);
        assert(frameStatistics.GetBackingAllocationCount() > 0u);
        assert(frameStatistics.GetBackingUsedMemory() > 0u);
        assert(frameStatistics.GetBackingPeakUsedMemory() >= frameStatistics.GetBackingUsedMemory());
    }

    // FrameAllocator::Deallocate()はno-opですが、STL Adapterから見たLifetime終了はCounterへ記録します。
    // したがってCounterのActiveBytesは0になり、FrameAllocator内部のUsedMemoryだけがResetまで残ります。
    assert(frameStatistics.DeallocationCount > 0u);
    assert(frameStatistics.ActiveBytes == 0u);
    assert(frameStatistics.GetBackingUsedMemory() > 0u);

    // 推奨容量は実測Lifetime Peakへ25%のHeadroomを加え、4 KiB境界へ切り上げます。
    // この小さいテストでは結果がちょうど1 pageになるため、計算規則そのものを明確に固定できます。
    const std::size_t solverFramePeak = frameStatistics.GetBackingPeakUsedMemory();
    const std::size_t recommendedCapacity = frameStatistics.GetRecommendedBackingCapacity();
    assert(recommendedCapacity >= solverFramePeak);
    assert(recommendedCapacity % (4u * 1024u) == 0u);
    assert(recommendedCapacity == Capacity);

    // Statistics::Reset()がStep境界のArena Resetも担当します。
    // Containerがscopeを抜けた後に呼ぶことが重要です。生存中のContainerが指す領域をResetすると、
    // 次のAllocate()で同じ領域が上書きされるため、必ず寿命境界を守ります。
    frameStatistics.Reset();

    assert(frameStatistics.AllocationCount == 0u);
    assert(frameStatistics.AllocationBytes == 0u);
    assert(frameStatistics.GetBackingUsedMemory() == 0u);
    assert(frameStatistics.GetBackingAllocationCount() == 0u);

    // PeakはArena容量のチューニングに使用するため、Frame Reset後も保持します。
    assert(frameStatistics.GetBackingPeakUsedMemory() == solverFramePeak);
    assert(frameStatistics.GetRecommendedBackingCapacity() == recommendedCapacity);

    // ========================================================================
    // SoftBody Solver Temporary Allocator Mode切替
    // ========================================================================
    // Before / After計測では同じSolver InstanceのままModeだけを切り替えます。
    // Settings値だけが変わりBackingが同期されない状態を防ぐため、SetSettings()経由で
    // Heapならnullptr、FrameAllocatorならSolver所有Arenaへ確実に切り替わることを確認します。
    SoftBodySolver softBodySolver;
    SoftBodySolverSettings solverSettings = softBodySolver.GetSettings();

    assert(solverSettings.TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::FrameAllocator);
    assert(softBodySolver.GetTemporaryAllocationStatistics().GetBackingAllocator() != nullptr);

    solverSettings.TemporaryAllocatorMode = SoftBodyTemporaryAllocatorMode::Heap;
    softBodySolver.SetSettings(solverSettings);

    assert(softBodySolver.GetSettings().TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::Heap);
    assert(softBodySolver.GetTemporaryAllocationStatistics().GetBackingAllocator() == nullptr);

    solverSettings.TemporaryAllocatorMode = SoftBodyTemporaryAllocatorMode::FrameAllocator;
    softBodySolver.SetSettings(solverSettings);

    const SolverTemporaryAllocationStatistics& solverStatistics =
        softBodySolver.GetTemporaryAllocationStatistics();
    assert(softBodySolver.GetSettings().TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::FrameAllocator);
    assert(solverStatistics.GetBackingAllocator() != nullptr);
    assert(solverStatistics.GetBackingCapacity() > 0u);
    assert(solverStatistics.GetBackingUsedMemory() == 0u);

    // Arena容量を最終調整する際はLifetime Peakだけでなく、実際のCapacityに対して
    // どれだけ余裕が残っているかを見る必要があります。初期状態ではまだ未使用なのでPeakは0です。
    assert(solverStatistics.GetBackingPeakUsedMemory() == 0u);
    assert(solverStatistics.GetRecommendedBackingCapacity() == 0u);
}

} // namespace Raven::ph::tests
