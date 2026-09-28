#include "Raven/Physics/Astro/Gravity/BarnesHutGravitySolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "Raven/Physics/Astro/Spatial/AstroOctree.h"

namespace Raven::ph
{
namespace
{
bool ContainsPosition(const AstroOctreeNode& node, const AstroVector3& position)
{
    return std::abs(position.x - node.Center.x) <= node.HalfSize
        && std::abs(position.y - node.Center.y) <= node.HalfSize
        && std::abs(position.z - node.Center.z) <= node.HalfSize;
}

AstroVector3 ComputeAggregateForce(
    const AstroBodyState& target,
    const AstroVector3& sourcePosition,
    double sourceMass,
    const GravitySolverSettings& settings)
{
    const AstroVector3 delta = sourcePosition - target.Position;
    const double distanceSquared = delta.LengthSq();
    if (distanceSquared <= 0.0 || std::isfinite(distanceSquared) == false)
    {
        return {};
    }

    const double distance = std::sqrt(distanceSquared);
    const double effectiveDistance = std::max(distance, std::max(settings.MinimumDistance, 0.0));
    if (effectiveDistance <= std::numeric_limits<double>::epsilon())
    {
        return {};
    }

    const double magnitude =
        settings.GravitationalConstant * target.Mass * sourceMass
        / (effectiveDistance * effectiveDistance);
    if (std::isfinite(magnitude) == false)
    {
        return {};
    }

    return delta * (magnitude / distance);
}
}

void BarnesHutGravitySolver::ComputeForces(
    const std::vector<AstroBodyState>& bodies,
    const GravitySolverSettings& settings,
    std::vector<AstroVector3>& outForces,
    AstroStatistics* statistics) const
{
    outForces.assign(bodies.size(), AstroVector3{});
    if (statistics != nullptr)
    {
        statistics->GravityPairCandidateCount = 0u;
        statistics->GravityForceEvaluationCount = 0u;
        statistics->GravityVisitedNodeCount = 0u;
        statistics->GravityAcceptedAggregateNodeCount = 0u;
        statistics->GravityTreeBuildTimeMs = 0.0;
    }

    if (std::isfinite(settings.GravitationalConstant) == false
        || settings.GravitationalConstant <= 0.0
        || std::isfinite(m_Theta) == false
        || m_Theta <= 0.0)
    {
        return;
    }

    using Clock = std::chrono::steady_clock;
    AstroOctree tree;
    const auto treeBuildBegin = Clock::now();
    tree.Build(bodies);
    const auto treeBuildEnd = Clock::now();
    if (statistics != nullptr)
    {
        statistics->GravityTreeBuildTimeMs =
            std::chrono::duration<double, std::milli>(treeBuildEnd - treeBuildBegin).count();
    }
    if (tree.GetRootIndex() < 0)
    {
        return;
    }

    const std::vector<AstroOctreeNode>& nodes = tree.GetNodes();
    for (std::size_t targetIndex = 0u; targetIndex < bodies.size(); ++targetIndex)
    {
        const AstroBodyState& target = bodies[targetIndex];
        if (target.ReceiveGravity == false
            || std::isfinite(target.Mass) == false
            || target.Mass <= 0.0)
        {
            continue;
        }

        std::vector<std::int32_t> stack;
        stack.push_back(tree.GetRootIndex());
        while (stack.empty() == false)
        {
            const std::int32_t nodeIndex = stack.back();
            stack.pop_back();
            const AstroOctreeNode& node = nodes[static_cast<std::size_t>(nodeIndex)];

            if (statistics != nullptr)
            {
                ++statistics->GravityVisitedNodeCount;
            }
            if (node.TotalMass <= 0.0)
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
                        ++statistics->GravityPairCandidateCount;
                    }
                    const AstroBodyState& source =
                        bodies[static_cast<std::size_t>(sourceIndex)];
                    if (source.GenerateGravity == false)
                    {
                        continue;
                    }

                    const AstroVector3 force =
                        ComputeAggregateForce(target, source.Position, source.Mass, settings);
                    if (force.LengthSq() > 0.0)
                    {
                        outForces[targetIndex] += force;
                        if (statistics != nullptr)
                        {
                            ++statistics->GravityForceEvaluationCount;
                        }
                    }
                }
                continue;
            }

            const AstroVector3 delta = node.CenterOfMass - target.Position;
            const double distance = delta.Length();
            const double nodeSize = node.HalfSize * 2.0;
            // target自身を含むnodeはaggregateするとself-forceが混入するため必ず展開します。
            const bool canApproximate =
                ContainsPosition(node, target.Position) == false
                && distance > 0.0
                && nodeSize / distance < m_Theta;
            if (canApproximate == true)
            {
                const AstroVector3 force =
                    ComputeAggregateForce(target, node.CenterOfMass, node.TotalMass, settings);
                if (force.LengthSq() > 0.0)
                {
                    outForces[targetIndex] += force;
                    if (statistics != nullptr)
                    {
                        ++statistics->GravityForceEvaluationCount;
                        ++statistics->GravityAcceptedAggregateNodeCount;
                    }
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
