#pragma once
#include <algorithm>
#include <exception>

namespace Raven
{
namespace Data
{
template<class T>
bool DeserializeArray(DataElement input, std::vector<T>& output, JobSystem& pool,
    ArrayReadResult& result, const ArrayReadOptions& options)
{
    static_assert(std::is_same<T, bool>::value == false, "vector<bool> shares storage between elements");
    result = {};
    SerializationContext context(result.FirstError);
    std::uint32_t count = 0;
    if (input.IsType(DataType::Array) == false || input.TryGetCount(count) == false)
    {
        result.Status = ArrayReadStatus::InvalidInput;
        return context.Fail(SerializationErrorCode::TypeMismatch, input);
    }
    std::atomic<bool> observedCancellation{false};
    const auto cancelled = [&options, &observedCancellation]()
    {
        if (options.Cancel != nullptr && options.Cancel->load(std::memory_order_relaxed))
        {
            observedCancellation.store(true, std::memory_order_relaxed);
            return true;
        }
        return false;
    };
    if (cancelled()) { result.Status = ArrayReadStatus::Cancelled; return false; }
    struct Chunk
    {
        DataElement Start;
        std::size_t Begin = 0;
        std::size_t End = 0;
        std::size_t Failures = 0;
        std::size_t First = 0;
        SerializationError Error;
    };
    try
    {
        std::vector<T> candidate(count);
        const DataToken* token = input.GetToken();
        const std::size_t tokens = static_cast<std::size_t>(token->Next - input.GetId());
        const bool parallel = options.EnableParallel && count > 1 && pool.GetWorkerCount() > 1
            && count >= options.MinElements && tokens >= options.MinTokens;
        // 要素ごとのJob/診断領域は作りません。連続範囲をWorker数までに分け、投入・確保を抑えます。
        const std::size_t chunksCount = parallel ? (std::min)(static_cast<std::size_t>(count), pool.GetWorkerCount()) : 1;
        std::vector<Chunk> chunks(chunksCount);
        std::vector<std::future<void>> futures;
        futures.reserve(parallel ? chunksCount : 0);
        DataElement child = input.GetFirstChild();
        std::size_t offset = 0;
        for (std::size_t c = 0; c < chunksCount; ++c)
        {
            Chunk& chunk = chunks[c];
            chunk.Begin = offset;
            chunk.End = offset + count / chunksCount + (c < count % chunksCount ? 1 : 0);
            chunk.Start = child;
            // 逐次は予備走査を省略。並列はchunk先頭だけ索引し、O(N)で分割します。
            if (parallel)
            {
                while (offset < chunk.End) { child = child.GetNextSibling(); ++offset; }
            }
        }
        const auto convert = [&](Chunk& chunk)
        {
            DataElement element = chunk.Start;
            for (std::size_t i = chunk.Begin; i < chunk.End; ++i)
            {
                if (cancelled()) { break; }
                SerializationError error;
                SerializationContext root(error);
                auto indexed = root.Index(i);
                bool success = false;
                try
                {
                    success = DataAdapter<T>::Read(element, candidate[i], indexed) && indexed.Good();
                    if (success == false) { indexed.Fail(SerializationErrorCode::ValidationFailed, element); }
                }
                catch (const std::bad_alloc&) { indexed.Fail(SerializationErrorCode::OutOfMemory, element); }
                catch (const std::length_error&) { indexed.Fail(SerializationErrorCode::LimitExceeded, element); }
                catch (...) { indexed.Fail(SerializationErrorCode::ValidationFailed, element); }
                if (success == false)
                {
                    if (chunk.Failures == 0) { chunk.First = i; chunk.Error = error; }
                    ++chunk.Failures;
                }
                element = element.GetNextSibling();
            }
        };
        bool resourceFailure = false;
        if (parallel)
        {
            // Submit途中で例外が出ても、既に投入したJobは必ず待ちます。
            // candidate/chunks/Document参照を先に破棄するとWorkerが解放済み領域へ触れるためです。
            try
            {
                for (Chunk& chunk : chunks)
                {
                    futures.push_back(pool.Submit([&convert, target = &chunk]() { convert(*target); }));
                    if (futures.back().valid() == false) { resourceFailure = true; break; }
                    ++result.SubmittedJobs;
                }
            }
            catch (...) { resourceFailure = true; }
            result.UsedParallel = result.SubmittedJobs != 0;
            for (auto& future : futures)
            {
                if (future.valid())
                {
                    try { future.get(); } catch (...) { resourceFailure = true; }
                }
            }
        }
        else { convert(chunks.front()); }
        for (const Chunk& chunk : chunks)
        {
            if (chunk.Failures != 0)
            {
                if (result.FailedElements == 0)
                {
                    result.FirstFailedIndex = chunk.First;
                    result.FirstError = chunk.Error;
                }
                result.FailedElements += chunk.Failures;
            }
        }
        if (resourceFailure) { result.Status = ArrayReadStatus::ResourceFailure; return false; }
        if (cancelled() || observedCancellation.load(std::memory_order_relaxed))
        { result.Status = ArrayReadStatus::Cancelled; return false; }
        if (result.FailedElements != 0) { result.Status = ArrayReadStatus::ElementFailure; return false; }
        output.swap(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        result.Status = ArrayReadStatus::ResourceFailure;
        return context.Fail(SerializationErrorCode::OutOfMemory, input);
    }
    catch (const std::length_error&)
    {
        result.Status = ArrayReadStatus::ResourceFailure;
        return context.Fail(SerializationErrorCode::LimitExceeded, input);
    }
    catch (...)
    {
        result.Status = ArrayReadStatus::ResourceFailure;
        return false;
    }
}
}
}
