#include "Raven/Data/Tests/DataBenchmarkMemory.h"
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <new>
#include <mutex>

namespace
{
struct Header { void* Base; std::size_t Size; std::uint64_t Generation; };
std::atomic<std::uint64_t> generation{0}, active{0}, allocations{0}, bytes{0}, live{0}, peak{0};
std::atomic<std::int64_t> failAfter{-1};
std::mutex tracking;
void* Allocate(std::size_t size, std::size_t alignment)
{
    alignment = (std::max)(alignment, alignof(Header));
    const std::size_t overhead = sizeof(Header) + alignment - 1;
    if (size > (std::numeric_limits<std::size_t>::max)() - overhead) { throw std::bad_alloc(); }
    if (active.load() != 0)
    {
        auto remaining = failAfter.load();
        while (remaining >= 0)
        {
            if (remaining == 0) { throw std::bad_alloc(); }
            if (failAfter.compare_exchange_weak(remaining, remaining - 1)) { break; }
        }
    }
    void* base = nullptr;
    for (;;)
    {
        base = std::malloc((size == 0 ? 1 : size) + overhead);
        if (base != nullptr) { break; }
        auto handler = std::get_new_handler();
        if (handler == nullptr) { throw std::bad_alloc(); }
        handler();
    }
    const auto address = (reinterpret_cast<std::uintptr_t>(base) + sizeof(Header) + alignment - 1) & ~(alignment - 1);
    auto* header = reinterpret_cast<Header*>(address) - 1;
    std::lock_guard<std::mutex> lock(tracking);
    const auto session = active.load();
    new (header) Header{base, size, session};
    if (session != 0)
    {
        allocations.fetch_add(1);
        bytes.fetch_add(size);
        const auto current = live.fetch_add(size) + size;
        auto previous = peak.load();
        while (current > previous && peak.compare_exchange_weak(previous, current) == false) {}
    }
    return reinterpret_cast<void*>(address);
}
void Free(void* memory) noexcept
{
    if (memory == nullptr) { return; }
    auto* header = reinterpret_cast<Header*>(memory) - 1;
    if (header->Generation != 0)
    {
        std::lock_guard<std::mutex> lock(tracking);
        if (header->Generation == active.load()) { live.fetch_sub(header->Size); }
    }
    std::free(header->Base);
}
}
namespace Raven { namespace Data { namespace Tests {
void BeginBenchmarkMemory()
{
    std::lock_guard<std::mutex> lock(tracking);
    allocations = 0; bytes = 0; live = 0; peak = 0;
    active = generation.fetch_add(1) + 1;
}
BenchmarkMemory EndBenchmarkMemory()
{
    std::lock_guard<std::mutex> lock(tracking);
    active = 0;
    failAfter = -1;
    return {allocations.load(), bytes.load(), peak.load()};
}
void FailBenchmarkAllocationAfter(std::int64_t count) { failAfter = count; }
} } }
void* operator new(std::size_t size) { return Allocate(size, alignof(std::max_align_t)); }
void* operator new[](std::size_t size) { return Allocate(size, alignof(std::max_align_t)); }
void operator delete(void* p) noexcept { Free(p); }
void operator delete[](void* p) noexcept { Free(p); }
void operator delete(void* p, std::size_t) noexcept { Free(p); }
void operator delete[](void* p, std::size_t) noexcept { Free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) { return Allocate(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return Allocate(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* p, std::align_val_t) noexcept { Free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { Free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { Free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { Free(p); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { try { return ::operator new(size); } catch (...) { return nullptr; } }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { try { return ::operator new[](size); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { Free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { Free(p); }
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new(size, a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new[](size, a); } catch (...) { return nullptr; } }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { Free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { Free(p); }
#else
namespace Raven { namespace Data { namespace Tests {
void BeginBenchmarkMemory() {}
BenchmarkMemory EndBenchmarkMemory() { return {}; }
void FailBenchmarkAllocationAfter(std::int64_t) {}
} } }
#endif
