#pragma once

#include "Raven/Core/Containers/FlatHashMap.h"
#include "Raven/Core/Containers/StringView.h"
#include "Raven/Core/Containers/Vector.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Raven
{

class NameTable;
struct NameHasher;

// NameTableへInternされた識別子を参照する軽量Handleです。
// Nameは文字列を所有しません。view()を利用する場合は生成元NameTableがNameより長生きする必要があります。
// 異なるNameTableから生成したNameは、同じ文字列であっても別の識別子として扱います。
class Name
{
public:
    Name() noexcept = default;

    [[nodiscard]] bool IsNone() const noexcept;
    [[nodiscard]] std::size_t GetIndex() const noexcept;
    [[nodiscard]] StringView view() const noexcept;

private:
    friend class NameTable;
    friend struct NameHasher;
    friend bool operator==(Name left, Name right) noexcept;
    friend bool operator!=(Name left, Name right) noexcept;

    Name(const NameTable& table, std::size_t index) noexcept;

    const NameTable* m_Table = nullptr;
    std::size_t m_Index = 0;
};

bool operator==(Name left, Name right) noexcept;
bool operator!=(Name left, Name right) noexcept;

// FlatHashMapやstd::unordered_mapでNameをKeyにするためのHasherです。
// Hash値はProcess内のTable addressを含むため、永続化には使用しないでください。
struct NameHasher
{
    std::size_t operator()(Name name) const noexcept;
};

// UTF-8 byte列を一度だけ所有し、以後の比較をNameのTable + index比較へ変換します。
// すべての公開操作は内部で同期しますが、Table破棄と他Threadの操作は呼び出し側で同期してください。
// 同じAllocatorを別の利用者も並行使用する場合は、Allocator側の同期または外部同期が別途必要です。
// AllocatorはNameTableより長生きし、個別Deallocateをサポートする必要があります。
class NameTable
{
public:
    using HashFunction = std::uint64_t (*)(StringView) noexcept;

    explicit NameTable(Allocator& allocator, HashFunction hashFunction = &NameTable::HashString) noexcept;
    ~NameTable();

    NameTable(const NameTable&) = delete;
    NameTable& operator=(const NameTable&) = delete;
    NameTable(NameTable&&) = delete;
    NameTable& operator=(NameTable&&) = delete;

    [[nodiscard]] Name Intern(StringView text);
    [[nodiscard]] StringView Resolve(Name name) const noexcept;
    [[nodiscard]] bool Contains(Name name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] Allocator* GetAllocator() const noexcept;

    // 決定的なFNV-1a 64-bit Hashです。衝突時はIntern元文字列を比較するため、Hash一致だけで同一視しません。
    [[nodiscard]] static std::uint64_t HashString(StringView text) noexcept;

private:
    struct Entry;

    static constexpr std::size_t InvalidIndex = static_cast<std::size_t>(-1);

    Entry* CreateEntry(StringView text, std::size_t nextIndex);
    void DestroyEntry(Entry* entry) noexcept;

    Allocator* m_Allocator = nullptr;
    Vector<Entry*> m_Entries;
    FlatHashMap<std::uint64_t, std::size_t> m_HashHeads;
    HashFunction m_HashFunction = nullptr;
    mutable std::mutex m_Mutex;
};

} // namespace Raven
