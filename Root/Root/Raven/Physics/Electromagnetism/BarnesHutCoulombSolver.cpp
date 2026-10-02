#include "Raven/Physics/Electromagnetism/BarnesHutCoulombSolver.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Raven::ph
{
namespace
{
bool ContainsPosition(
    const CoulombOctreeNode& node,
    const std::array<double, 3>& position)
{
    return std::abs(position[0] - node.Center[0]) <= node.HalfSize
        && std::abs(position[1] - node.Center[1]) <= node.HalfSize
        && std::abs(position[2] - node.Center[2]) <= node.HalfSize;
}

math::Vec3 ToSceneVector(const std::array<double, 3>& position)
{
    return {
        static_cast<float>(position[0]),
        static_cast<float>(position[1]),
        static_cast<float>(position[2])
    };
}

double Distance(
    const std::array<double, 3>& a,
    const std::array<double, 3>& b)
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    const double dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void AccumulateAggregateForce(
    const CoulombOctreeBody& target,
    const CoulombOctreeNode& node,
    const CoulombForceSettings& settings,
    math::Vec3& outForce,
    CoulombBarnesHutStatistics* statistics)
{
    const math::Vec3 targetPosition = ToSceneVector(target.Position);

    // 正負aggregateを別々の仮想点電荷として評価します。
    // net chargeへ相殺しないことでdipole等の遠方場情報を完全には失わない近似にします。
    if (node.PositiveCharge > 0.0)
    {
        const math::Vec3 force = ComputeCoulombForce(
            ToSceneVector(node.PositiveCenter),
            node.PositiveCharge,
            targetPosition,
            target.ChargeCoulombs,
            settings);
        outForce += force;
        if (statistics != nullptr && force.LengthSq() > 0.0f)
        {
            ++statistics->ForceEvaluationCount;
        }
    }

    if (node.NegativeChargeMagnitude > 0.0)
    {
        const math::Vec3 force = ComputeCoulombForce(
            ToSceneVector(node.NegativeCenter),
            -node.NegativeChargeMagnitude,
            targetPosition,
            target.ChargeCoulombs,
            settings);
        outForce += force;
        if (statistics != nullptr && force.LengthSq() > 0.0f)
        {
            ++statistics->ForceEvaluationCount;
        }
    }
}
}

void BarnesHutCoulombSolver::ComputeForces(
    const std::vector<CoulombOctreeBody>& bodies,
    const CoulombForceSettings& settings,
    std::vector<math::Vec3>& outForces,
    CoulombBarnesHutStatistics* statistics) const
{
    outForces.assign(bodies.size(), math::Vec3{});
    if (statistics != nullptr)
    {
        *statistics = {};
    }

    if (std::isfinite(settings.CoulombConstant) == false
        || settings.CoulombConstant <= 0.0
        || std::isfinite(m_Theta) == false
        || m_Theta <= 0.0)
    {
        return;
    }

    using Clock = std::chrono::steady_clock;
    CoulombOctree tree;
    const auto treeBuildBegin = Clock::now();
    tree.Build(bodies);
    const auto treeBuildEnd = Clock::now();
    if (statistics != nullptr)
    {
        statistics->TreeBuildTimeMs =
            std::chrono::duration<double, std::milli>(treeBuildEnd - treeBuildBegin).count();
    }
    if (tree.GetRootIndex() < 0)
    {
        return;
    }

    const std::vector<CoulombOctreeNode>& nodes = tree.GetNodes();
    for (std::size_t targetIndex = 0u; targetIndex < bodies.size(); ++targetIndex)
    {
        const CoulombOctreeBody& target = bodies[targetIndex];
        if (target.ChargeCoulombs == 0.0
            || std::isfinite(target.ChargeCoulombs) == false)
        {
            continue;
        }

        std::vector<std::int32_t> stack;
        stack.push_back(tree.GetRootIndex());
        while (stack.empty() == false)
        {
            const std::int32_t nodeIndex = stack.back();
            stack.pop_back();
            const CoulombOctreeNode& node = nodes[static_cast<std::size_t>(nodeIndex)];

            if (statistics != nullptr)
            {
                ++statistics->VisitedNodeCount;
            }
            if (node.PositiveCharge <= 0.0 && node.NegativeChargeMagnitude <= 0.0)
            {
                continue;
            }

            if (node.IsLeaf() == true)
            {
                for (const std::int32_t sourceIndex : node.BodyIndices)
                {
                    if (static_cast<std::size_t>(sourceIndex) == targetIndex)
                    {
                        continue;
                    }

                    if (statistics != nullptr)
                    {
                        ++statistics->PairCandidateCount;
                    }

                    const CoulombOctreeBody& source =
                        bodies[static_cast<std::size_t>(sourceIndex)];
                    const math::Vec3 force = ComputeCoulombForce(
                        ToSceneVector(source.Position),
                        source.ChargeCoulombs,
                        ToSceneVector(target.Position),
                        target.ChargeCoulombs,
                        settings);
                    outForces[targetIndex] += force;
                    if (statistics != nullptr && force.LengthSq() > 0.0f)
                    {
                        ++statistics->ForceEvaluationCount;
                    }
                }
                continue;
            }

            // target自身を含むnodeをaggregateするとself-forceが混入するため、Gravityと同様に必ず展開します。
            const double distance = Distance(node.Center, target.Position);
            const double nodeSize = node.HalfSize * 2.0;
            const bool canApproximate =
                ContainsPosition(node, target.Position) == false
                && distance > 0.0
                && nodeSize / distance < m_Theta;
            if (canApproximate == true)
            {
                AccumulateAggregateForce(
                    target,
                    node,
                    settings,
                    outForces[targetIndex],
                    statistics);
                if (statistics != nullptr)
                {
                    ++statistics->AcceptedAggregateNodeCount;
                }
                continue;
            }

            for (const std::int32_t childIndex : node.Children)
            {
                if (childIndex >= 0)
                {
                    stack.push_back(childIndex);
                }
            }
        }
    }
}

} // namespace Raven::ph
