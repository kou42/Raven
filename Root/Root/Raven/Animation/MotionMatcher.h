#pragma once

#include "Raven/Animation/MotionDatabase.h"
#include "Raven/Animation/PoseInertializer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace Raven
{

// ============================================================================
// MotionMatcherConfig
// ============================================================================
// Motion Matching専用のRuntime設定です。
// 既存AnimatorのLoop / CrossFade設定とは独立させ、従来Animation経路へ影響させません。
struct MotionMatcherConfig
{
    MotionSearchWeights SearchWeights{};

    // 最良候補が毎Frame変化した場合でも短時間に連続切替しないための最低保持時間です。
    // 0なら検索結果に応じて毎Frame切替可能です。
    float MinimumSwitchInterval = 0.15f;

    // 現在再生を継続する候補のCostから差し引くBonusです。
    // 小さなCost差で別Motionへ切り替わるChatteringを抑えます。
    float StayBonus = 0.0f;

    // 現在の連続再生地点から別候補へJumpする場合に加えるCostです。
    // StayBonusと組み合わせ、切替には明確な検索品質改善を要求します。
    float SwitchCost = 0.0f;

    // Motion Matchingの候補切替時だけPose差分を減衰させます。
    // falseの場合は検索先Poseをそのまま出力するため、検索品質を確認したいDebug用途にも使えます。
    bool EnableInertialization = true;
    PoseInertializerConfig Inertialization{};

    // 現段階ではLocomotion Databaseを主対象とするため、選択Clipの終端でLoopできます。
    // AnimationClip自体へLoop状態は追加せず、再生InstanceであるMotionMatcherが保持します。
    bool Loop = true;
};

// ============================================================================
// MotionMatcher
// ============================================================================
// MotionDatabaseの検索結果から再生するClip / Timeを選び、SkeletonPoseを評価するRuntime Stateです。
//
// 既存Animator / BlendTree / StateMachineを置き換えず、Motion Matchingを選択したCharacterだけが
// このRuntimeを使用します。候補切替時のPose接続はPoseInertializerへ分離し、既存CrossFadeとは
// 独立した経路として扱います。
class MotionMatcher
{
public:
    void SetDatabase(std::shared_ptr<const MotionDatabase> database);
    const std::shared_ptr<const MotionDatabase>& GetDatabase() const { return m_Database; }

    void SetConfig(const MotionMatcherConfig& config) { m_Config = config; }
    const MotionMatcherConfig& GetConfig() const { return m_Config; }

    void Reset();

    // Queryから最良候補を検索し、必要ならClipを切り替えた後でPoseを評価します。
    // 初回Updateでは比較元PoseがないためTarget Poseをそのまま返し、2回目以降の候補切替で
    // Inertializationを開始します。
    bool Update(
        const Skeleton& skeleton,
        const MotionSearchQuery& query,
        float deltaTime,
        SkeletonPose& outPose);

    bool HasSelection() const { return m_HasSelection; }

    // 最後に遷移先として選んだDatabase Frameです。
    // 再生中はCurrentTimeが連続的に進むため「現在時刻に最も近いFrame Index」ではありません。
    std::size_t GetSelectedFrameIndex() const { return m_SelectedFrameIndex; }

    std::uint32_t GetCurrentClipIndex() const { return m_CurrentClipIndex; }
    float GetCurrentTime() const { return m_CurrentTime; }

    // 最後に実際にDatabase検索を行ったときの生Costです。
    // Stay Bonus / Switch CostはRuntimeの切替判断だけに使用し、Feature検索自体の品質を
    // Debugできるよう、この値にはBiasを含めません。
    float GetLastSearchCost() const { return m_LastSearchCost; }

    bool IsInertializing() const { return m_Inertializer.IsActive(); }

private:
    bool SelectFrame(const MotionSearchResult& searchResult);
    bool AdvanceCurrentTime(float deltaTime);

    // 現在Clip / Timeに最も近いDatabase Frameを連続再生候補として取得します。
    bool FindContinuationFrame(std::size_t& outFrameIndex) const;

    // MotionDatabase::FindBestMatch()と同じFeature Cost式で任意Frameを評価します。
    // Runtime状態依存のBias比較にだけ使い、Database AssetへCurrentTimeを持ち込みません。
    bool CalculateFrameCost(
        const MotionSearchQuery& query,
        std::size_t frameIndex,
        float& outCost) const;

private:
    std::shared_ptr<const MotionDatabase> m_Database;
    MotionMatcherConfig m_Config{};
    PoseInertializer m_Inertializer{};
    SkeletonPose m_LastOutputPose{};

    std::size_t m_SelectedFrameIndex = std::numeric_limits<std::size_t>::max();
    std::uint32_t m_CurrentClipIndex = 0;
    float m_CurrentTime = 0.0f;
    float m_TimeSinceSwitch = 0.0f;
    float m_LastSearchCost = std::numeric_limits<float>::max();
    bool m_HasSelection = false;
    bool m_HasLastOutputPose = false;
};

} // namespace Raven
