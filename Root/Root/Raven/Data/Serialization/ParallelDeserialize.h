#pragma once
#include "Raven/Data/Serialization/Serialization.h"
#include "Raven/Core/Jobs/JobSystem.h"
#include <atomic>

namespace Raven
{
namespace Data
{
enum class ArrayReadStatus { Success, InvalidInput, ElementFailure, Cancelled, ResourceFailure };
struct ArrayReadResult
{
    ArrayReadStatus Status = ArrayReadStatus::Success;
    SerializationError FirstError;
    std::size_t FailedElements = 0;
    std::size_t FirstFailedIndex = 0;
    std::size_t SubmittedJobs = 0;
    bool UsedParallel = false;
};
struct ArrayReadOptions
{
    // 既定では逐次。対象Adapterと実機の計測から閾値を指定して有効化してください。
    bool EnableParallel = false;
    std::size_t MinElements = 0;
    std::size_t MinTokens = 0;
    const std::atomic<bool>* Cancel = nullptr;
};

// 独立した値だけを生成する明示的Adapter用。参照解決・Scene/GPU反映は成功後に呼び出し側で行います。
// 呼び出しはpoolのWorker以外から行い、Document・pool・Cancelを終了まで生存させてください。
// AdapterはThread-safeで、共有状態・入力Documentを変更せず、出力に非所有参照を残さない契約です。
// 全Jobを回収してから戻り、成功時だけoutputをswapします。取消は要素間の協調的な確認です。
// エラーは全要素の失敗件数と最小indexの診断を集約します。取消時は完了済み分だけです。
template<class T>
bool DeserializeArray(DataElement input, std::vector<T>& output, JobSystem& pool,
    ArrayReadResult& result, const ArrayReadOptions& options = {});
}
}
#include "Raven/Data/Serialization/ParallelDeserialize.inl"
