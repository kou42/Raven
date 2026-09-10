#include "Raven/Animation/MotionDatabase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Raven
{
namespace
{

constexpr float TrajectoryOffsetEpsilon = 1.0e-4f;

math::Vec3 ExtractTranslation(const math::Mat4& matrix)
{
    return {
        matrix.m[0][3],
        matrix.m[1][3],
        matrix.m[2][3]
    };
}

math::Vec3 TransformPoint(const math::Mat4& matrix, const math::Vec3& point)
{
    const math::Vec4 transformed = matrix * math::Vec4(point, 1.0f);
    return { transformed.x, transformed.y, transformed.z };
}

math::Vec3 TransformVector(const math::Mat4& matrix, const math::Vec3& vector)
{
    // w=0として扱い、Rootの平行移動成分をBone速度へ混ぜません。
    const math::Vec4 transformed = matrix * math::Vec4(vector, 0.0f);
    return { transformed.x, transformed.y, transformed.z };
}

bool ContainsDuplicateBoneIndex(const std::vector<BoneIndex>& boneIndices)
{
    for (std::size_t i = 0; i < boneIndices.size(); ++i)
    {
        for (std::size_t j = i + 1; j < boneIndices.size(); ++j)
        {
            if (boneIndices[i] == boneIndices[j])
            {
                return true;
            }
        }
    }

    return false;
}

bool ValidateRootBone(const Skeleton& skeleton, BoneIndex rootBone)
{
    if (skeleton.IsValidBoneIndex(rootBone) == false)
    {
        return false;
    }

    // 現段階ではRoot自身のLocal Transformから安全に逆行列を作るため、
    // Skeleton階層のRoot Boneだけを基準Boneとして許可します。
    return skeleton.GetBone(rootBone).Parent == InvalidBoneIndex;
}

bool ValidateTrajectoryOffsets(const std::vector<float>& offsets)
{
    if (offsets.empty() == true)
    {
        return false;
    }

    float previousOffset = 0.0f;
    for (float offset : offsets)
    {
        if (offset <= 0.0f || std::isfinite(offset) == false)
        {
            return false;
        }

        if (offset <= previousOffset)
        {
            return false;
        }

        previousOffset = offset;
    }

    return true;
}

bool ValidateSearchWeights(const MotionSearchWeights& weights)
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

double LengthSquared(const math::Vec3& value)
{
    const double x = static_cast<double>(value.x);
    const double y = static_cast<double>(value.y);
    const double z = static_cast<double>(value.z);
    return x * x + y * y + z * z;
}

bool HasMatchingFeatureLayout(
    const MotionFrame& frame,
    const MotionSearchQuery& query)
{
    if (frame.PoseFeatures.size() != query.PoseFeatures.size() ||
        frame.Trajectory.size() != query.Trajectory.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < frame.PoseFeatures.size(); ++i)
    {
        if (frame.PoseFeatures[i].Bone != query.PoseFeatures[i].Bone)
        {
            return false;
        }
    }

    for (std::size_t i = 0; i < frame.Trajectory.size(); ++i)
    {
        if (std::fabs(frame.Trajectory[i].TimeOffset - query.Trajectory[i].TimeOffset) >
            TrajectoryOffsetEpsilon)
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool MotionDatabase::AddClip(std::shared_ptr<AnimationClip> clip)
{
    if (clip == nullptr)
    {
        return false;
    }

    // 同じClipを複数回登録すると完全に同一の検索候補が増え、将来のCost比較で
    // 不要な同点候補を作るため、shared_ptrが指す実体単位で重複を拒否します。
    for (const std::shared_ptr<AnimationClip>& existingClip : m_Clips)
    {
        if (existingClip.get() == clip.get())
        {
            return false;
        }
    }

    m_Clips.emplace_back(std::move(clip));

    // Clip構成が変わった時点で既存Frame列は対応関係が古くなります。
    // Build()し直すまで未構築状態として扱い、古い検索候補を参照させません。
    m_Frames.clear();
    m_SampleRate = 0.0f;
    return true;
}

void MotionDatabase::Clear()
{
    m_Clips.clear();
    m_Frames.clear();
    m_SampleRate = 0.0f;
}

bool MotionDatabase::Build(float sampleRate)
{
    m_Frames.clear();
    m_SampleRate = 0.0f;

    if (sampleRate <= 0.0f || std::isfinite(sampleRate) == false)
    {
        return false;
    }

    if (m_Clips.empty() == true)
    {
        return false;
    }

    if (m_Clips.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }

    std::size_t totalFrameCount = 0;

    // 先に全Clipを検証して必要Frame数を求めます。
    // 検証途中で失敗した場合に一部Clipだけを持つDatabaseを残さないため、
    // 実際のMotionFrame生成は全Clipの検証後に行います。
    for (const std::shared_ptr<AnimationClip>& clip : m_Clips)
    {
        if (clip == nullptr)
        {
            return false;
        }

        const float duration = clip->GetDuration();
        if (duration <= 0.0f || std::isfinite(duration) == false)
        {
            return false;
        }

        const double exactFrameCount =
            std::ceil(static_cast<double>(duration) * static_cast<double>(sampleRate));

        if (exactFrameCount <= 0.0 ||
            exactFrameCount > static_cast<double>(std::numeric_limits<std::size_t>::max()))
        {
            return false;
        }

        const std::size_t clipFrameCount = static_cast<std::size_t>(exactFrameCount);
        if (clipFrameCount > (std::numeric_limits<std::size_t>::max() - totalFrameCount))
        {
            return false;
        }

        totalFrameCount += clipFrameCount;
    }

    m_Frames.reserve(totalFrameCount);

    const double sampleInterval = 1.0 / static_cast<double>(sampleRate);

    for (std::size_t clipIndex = 0; clipIndex < m_Clips.size(); ++clipIndex)
    {
        const float duration = m_Clips[clipIndex]->GetDuration();
        const std::size_t clipFrameCount = static_cast<std::size_t>(
            std::ceil(static_cast<double>(duration) * static_cast<double>(sampleRate)));

        for (std::size_t frameIndex = 0; frameIndex < clipFrameCount; ++frameIndex)
        {
            MotionFrame frame{};
            frame.ClipIndex = static_cast<std::uint32_t>(clipIndex);
            frame.Time = static_cast<float>(static_cast<double>(frameIndex) * sampleInterval);

            // ceil()で個数を決めているため理論上Duration未満ですが、floatへ戻す際の丸めで
            // 終端以上になった場合は候補を追加しません。終端重複を作らないというBuild契約を
            // 浮動小数点誤差があっても維持します。
            if (frame.Time < duration)
            {
                m_Frames.emplace_back(std::move(frame));
            }
        }
    }

    if (m_Frames.empty() == true)
    {
        return false;
    }

    m_SampleRate = sampleRate;
    return true;
}

bool MotionDatabase::BuildPoseFeatures(
    const Skeleton& skeleton,
    const MotionPoseFeatureConfig& config)
{
    ClearPoseFeatures();

    if (m_Frames.empty() == true || m_SampleRate <= 0.0f)
    {
        return false;
    }

    if (ValidateRootBone(skeleton, config.RootBone) == false)
    {
        return false;
    }

    if (config.PoseBones.empty() == true || ContainsDuplicateBoneIndex(config.PoseBones) == true)
    {
        return false;
    }

    for (BoneIndex boneIndex : config.PoseBones)
    {
        if (skeleton.IsValidBoneIndex(boneIndex) == false)
        {
            return false;
        }
    }

    const float sampleInterval = 1.0f / m_SampleRate;

    SkeletonPose currentPose;
    SkeletonPose previousPose;
    SkeletonPose nextPose;

    for (MotionFrame& frame : m_Frames)
    {
        if (frame.ClipIndex >= m_Clips.size())
        {
            ClearPoseFeatures();
            return false;
        }

        const std::shared_ptr<AnimationClip>& clip = m_Clips[frame.ClipIndex];
        if (clip == nullptr)
        {
            ClearPoseFeatures();
            return false;
        }

        const float duration = clip->GetDuration();
        const float previousTime = std::max(0.0f, frame.Time - sampleInterval);
        const float nextTime = std::min(duration, frame.Time + sampleInterval);
        const float velocityDeltaTime = nextTime - previousTime;

        if (velocityDeltaTime <= 0.0f)
        {
            ClearPoseFeatures();
            return false;
        }

        if (clip->Sample(skeleton, frame.Time, currentPose) == false ||
            clip->Sample(skeleton, previousTime, previousPose) == false ||
            clip->Sample(skeleton, nextTime, nextPose) == false)
        {
            ClearPoseFeatures();
            return false;
        }

        // Root Boneは階層Rootに限定しているため、そのLocal TransformはGlobal Transformと同じです。
        // Bone位置はRoot Transform全体の逆変換、Bone速度はw=0で同じ逆変換を適用し、
        // CharacterのWorld位置・向きに依存しない検索特徴量へ変換します。
        const math::Mat4 rootInverse = currentPose.GetLocalTransform(config.RootBone).ToInverseMatrix();

        frame.PoseFeatures.reserve(config.PoseBones.size());

        for (BoneIndex boneIndex : config.PoseBones)
        {
            const math::Vec3 currentWorldPosition =
                ExtractTranslation(currentPose.GetGlobalTransform(boneIndex));
            const math::Vec3 previousWorldPosition =
                ExtractTranslation(previousPose.GetGlobalTransform(boneIndex));
            const math::Vec3 nextWorldPosition =
                ExtractTranslation(nextPose.GetGlobalTransform(boneIndex));

            const math::Vec3 worldVelocity =
                (nextWorldPosition - previousWorldPosition) / velocityDeltaTime;

            MotionPoseFeature feature{};
            feature.Bone = boneIndex;
            feature.Position = TransformPoint(rootInverse, currentWorldPosition);
            feature.Velocity = TransformVector(rootInverse, worldVelocity);

            frame.PoseFeatures.emplace_back(feature);
        }
    }

    return true;
}

bool MotionDatabase::BuildTrajectoryFeatures(
    const Skeleton& skeleton,
    const MotionTrajectoryFeatureConfig& config)
{
    ClearTrajectoryFeatures();

    if (m_Frames.empty() == true || m_SampleRate <= 0.0f)
    {
        return false;
    }

    if (ValidateRootBone(skeleton, config.RootBone) == false ||
        ValidateTrajectoryOffsets(config.FutureTimeOffsets) == false)
    {
        return false;
    }

    SkeletonPose currentPose;
    SkeletonPose futurePose;

    for (MotionFrame& frame : m_Frames)
    {
        if (frame.ClipIndex >= m_Clips.size())
        {
            ClearTrajectoryFeatures();
            return false;
        }

        const std::shared_ptr<AnimationClip>& clip = m_Clips[frame.ClipIndex];
        if (clip == nullptr)
        {
            ClearTrajectoryFeatures();
            return false;
        }

        if (clip->Sample(skeleton, frame.Time, currentPose) == false)
        {
            ClearTrajectoryFeatures();
            return false;
        }

        const BoneTransform& currentRoot = currentPose.GetLocalTransform(config.RootBone);
        const math::Mat4 currentRootInverse = currentRoot.ToInverseMatrix();
        const math::Quat inverseCurrentRootRotation =
            currentRoot.Rotation.Normalized().Conjugate();

        frame.Trajectory.reserve(config.FutureTimeOffsets.size());

        for (float timeOffset : config.FutureTimeOffsets)
        {
            // AnimationClipはLoop情報を持たない既存設計なので、Database側で勝手にWrapしません。
            // Clip終端を越えるTrajectoryはDurationへClampし、Loop/遷移先の扱いは後続Runtimeへ残します。
            const float futureTime = std::min(frame.Time + timeOffset, clip->GetDuration());
            if (clip->Sample(skeleton, futureTime, futurePose) == false)
            {
                ClearTrajectoryFeatures();
                return false;
            }

            const BoneTransform& futureRoot = futurePose.GetLocalTransform(config.RootBone);
            const math::Vec3 futureWorldPosition =
                ExtractTranslation(futurePose.GetGlobalTransform(config.RootBone));
            const math::Vec3 futureWorldDirection =
                futureRoot.Rotation.Normalized().Rotate(math::Vec3{ 0.0f, 0.0f, 1.0f });

            MotionTrajectoryPoint point{};
            point.TimeOffset = timeOffset;
            point.Position = TransformPoint(currentRootInverse, futureWorldPosition);
            point.Direction = inverseCurrentRootRotation.Rotate(futureWorldDirection).Normalized();

            frame.Trajectory.emplace_back(point);
        }
    }

    return true;
}

bool MotionDatabase::FindBestMatch(
    const MotionSearchQuery& query,
    const MotionSearchWeights& weights,
    MotionSearchResult& outResult) const
{
    outResult = MotionSearchResult{};

    if (m_Frames.empty() == true ||
        (query.PoseFeatures.empty() == true && query.Trajectory.empty() == true) ||
        ValidateSearchWeights(weights) == false)
    {
        return false;
    }

    double bestCost = std::numeric_limits<double>::max();

    for (std::size_t frameIndex = 0; frameIndex < m_Frames.size(); ++frameIndex)
    {
        const MotionFrame& frame = m_Frames[frameIndex];
        if (HasMatchingFeatureLayout(frame, query) == false)
        {
            continue;
        }

        double cost = 0.0;

        for (std::size_t i = 0; i < frame.PoseFeatures.size(); ++i)
        {
            const math::Vec3 positionDelta =
                frame.PoseFeatures[i].Position - query.PoseFeatures[i].Position;
            const math::Vec3 velocityDelta =
                frame.PoseFeatures[i].Velocity - query.PoseFeatures[i].Velocity;

            cost += static_cast<double>(weights.PosePosition) * LengthSquared(positionDelta);
            cost += static_cast<double>(weights.PoseVelocity) * LengthSquared(velocityDelta);
        }

        for (std::size_t i = 0; i < frame.Trajectory.size(); ++i)
        {
            const math::Vec3 positionDelta =
                frame.Trajectory[i].Position - query.Trajectory[i].Position;
            const math::Vec3 directionDelta =
                frame.Trajectory[i].Direction - query.Trajectory[i].Direction;

            cost += static_cast<double>(weights.TrajectoryPosition) * LengthSquared(positionDelta);
            cost += static_cast<double>(weights.TrajectoryDirection) * LengthSquared(directionDelta);
        }

        if (std::isfinite(cost) == false)
        {
            continue;
        }

        if (cost < bestCost)
        {
            bestCost = cost;
            outResult.FrameIndex = frameIndex;
        }
    }

    if (outResult.IsValid() == false ||
        bestCost > static_cast<double>(std::numeric_limits<float>::max()))
    {
        outResult = MotionSearchResult{};
        return false;
    }

    outResult.Cost = static_cast<float>(bestCost);
    return true;
}

const std::shared_ptr<AnimationClip>& MotionDatabase::GetClip(std::size_t clipIndex) const
{
    static const std::shared_ptr<AnimationClip> NullClip{};

    if (clipIndex >= m_Clips.size())
    {
        return NullClip;
    }

    return m_Clips[clipIndex];
}

const MotionFrame* MotionDatabase::GetFrame(std::size_t frameIndex) const
{
    if (frameIndex >= m_Frames.size())
    {
        return nullptr;
    }

    return &m_Frames[frameIndex];
}

void MotionDatabase::ClearPoseFeatures()
{
    for (MotionFrame& frame : m_Frames)
    {
        frame.PoseFeatures.clear();
    }
}

void MotionDatabase::ClearTrajectoryFeatures()
{
    for (MotionFrame& frame : m_Frames)
    {
        frame.Trajectory.clear();
    }
}

} // namespace Raven
