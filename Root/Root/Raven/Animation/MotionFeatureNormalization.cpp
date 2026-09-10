#include "Raven/Animation/MotionDatabase.h"

#include <cmath>
#include <limits>

namespace Raven
{
namespace
{

constexpr double MinimumStandardDeviation = 1.0e-4;

struct ScalarStatisticsAccumulator
{
    double Sum = 0.0;
    double SumSquared = 0.0;
    std::size_t Count = 0;

    bool Add(const math::Vec3& value)
    {
        const float values[] = { value.x, value.y, value.z };
        for (float component : values)
        {
            if (std::isfinite(component) == false)
            {
                return false;
            }

            const double scalar = static_cast<double>(component);
            Sum += scalar;
            SumSquared += scalar * scalar;
            ++Count;
        }

        return true;
    }

    bool Finish(float& outMean, float& outStdDev) const
    {
        if (Count == 0)
        {
            return false;
        }

        const double count = static_cast<double>(Count);
        const double mean = Sum / count;
        const double variance = std::max(0.0, (SumSquared / count) - (mean * mean));
        const double standardDeviation = std::sqrt(variance);

        if (std::isfinite(mean) == false || std::isfinite(standardDeviation) == false)
        {
            return false;
        }

        // ほぼ一定のFeatureをsigma≈0で割ると、数値Noiseだけが巨大なCostになります。
        // そのため下限を設け、一定Featureは実質的に通常Scaleで比較します。
        const double safeStandardDeviation = std::max(
            standardDeviation,
            MinimumStandardDeviation);

        if (std::fabs(mean) > static_cast<double>(std::numeric_limits<float>::max()) ||
            safeStandardDeviation > static_cast<double>(std::numeric_limits<float>::max()))
        {
            return false;
        }

        outMean = static_cast<float>(mean);
        outStdDev = static_cast<float>(safeStandardDeviation);
        return true;
    }
};

bool ValidateSemanticWeights(const MotionSearchWeights& weights)
{
    const float values[] = {
        weights.PosePosition,
        weights.PoseVelocity,
        weights.TrajectoryPosition,
        weights.TrajectoryDirection
    };

    bool hasPositiveWeight = false;
    for (float value : values)
    {
        if (value < 0.0f || std::isfinite(value) == false)
        {
            return false;
        }

        if (value > 0.0f)
        {
            hasPositiveWeight = true;
        }
    }

    return hasPositiveWeight;
}

bool NormalizeWeight(float semanticWeight, float standardDeviation, float& outWeight)
{
    if (standardDeviation <= 0.0f || std::isfinite(standardDeviation) == false)
    {
        return false;
    }

    const double variance =
        static_cast<double>(standardDeviation) * static_cast<double>(standardDeviation);
    const double normalizedWeight = static_cast<double>(semanticWeight) / variance;

    if (std::isfinite(normalizedWeight) == false ||
        normalizedWeight > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }

    outWeight = static_cast<float>(normalizedWeight);
    return true;
}

} // namespace

bool MotionDatabase::BuildFeatureNormalization()
{
    ClearFeatureNormalization();

    if (m_Frames.empty() == true)
    {
        return false;
    }

    ScalarStatisticsAccumulator posePosition;
    ScalarStatisticsAccumulator poseVelocity;
    ScalarStatisticsAccumulator trajectoryPosition;
    ScalarStatisticsAccumulator trajectoryDirection;

    bool hasPoseFeature = false;
    bool hasTrajectoryFeature = false;

    for (const MotionFrame& frame : m_Frames)
    {
        for (const MotionPoseFeature& feature : frame.PoseFeatures)
        {
            if (posePosition.Add(feature.Position) == false ||
                poseVelocity.Add(feature.Velocity) == false)
            {
                ClearFeatureNormalization();
                return false;
            }

            hasPoseFeature = true;
        }

        for (const MotionTrajectoryPoint& point : frame.Trajectory)
        {
            if (trajectoryPosition.Add(point.Position) == false ||
                trajectoryDirection.Add(point.Direction) == false)
            {
                ClearFeatureNormalization();
                return false;
            }

            hasTrajectoryFeature = true;
        }
    }

    // 現在のMotion Matching QueryはPoseとTrajectoryを組み合わせる設計なので、
    // 片方だけの不完全なDatabaseから統計を作らず、Feature構築漏れを早期に検出します。
    if (hasPoseFeature == false || hasTrajectoryFeature == false)
    {
        return false;
    }

    MotionFeatureNormalization normalization{};
    if (posePosition.Finish(
            normalization.PosePositionMean,
            normalization.PosePositionStdDev) == false ||
        poseVelocity.Finish(
            normalization.PoseVelocityMean,
            normalization.PoseVelocityStdDev) == false ||
        trajectoryPosition.Finish(
            normalization.TrajectoryPositionMean,
            normalization.TrajectoryPositionStdDev) == false ||
        trajectoryDirection.Finish(
            normalization.TrajectoryDirectionMean,
            normalization.TrajectoryDirectionStdDev) == false)
    {
        return false;
    }

    normalization.Valid = true;
    m_Normalization = normalization;
    return true;
}

bool MotionDatabase::MakeNormalizedSearchWeights(
    const MotionSearchWeights& semanticWeights,
    MotionSearchWeights& outWeights) const
{
    outWeights = MotionSearchWeights{};

    if (m_Normalization.Valid == false || ValidateSemanticWeights(semanticWeights) == false)
    {
        return false;
    }

    if (NormalizeWeight(
            semanticWeights.PosePosition,
            m_Normalization.PosePositionStdDev,
            outWeights.PosePosition) == false ||
        NormalizeWeight(
            semanticWeights.PoseVelocity,
            m_Normalization.PoseVelocityStdDev,
            outWeights.PoseVelocity) == false ||
        NormalizeWeight(
            semanticWeights.TrajectoryPosition,
            m_Normalization.TrajectoryPositionStdDev,
            outWeights.TrajectoryPosition) == false ||
        NormalizeWeight(
            semanticWeights.TrajectoryDirection,
            m_Normalization.TrajectoryDirectionStdDev,
            outWeights.TrajectoryDirection) == false)
    {
        outWeights = MotionSearchWeights{};
        return false;
    }

    return true;
}

void MotionDatabase::ClearFeatureNormalization()
{
    m_Normalization = MotionFeatureNormalization{};
}

} // namespace Raven
