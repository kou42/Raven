#include "Raven/Core/Containers/Name.h"

#include "Raven/Core/Containers/String.h"

#include <cassert>
#include <limits>
#include <new>

namespace Raven
{

struct NameTable::Entry
{
    Entry(Allocator& allocator, StringView text, std::size_t nextIndex)
        : Text(allocator, text), NextIndex(nextIndex)
    {
    }

    String Text;
    std::size_t NextIndex = InvalidIndex;
};

Name::Name(const NameTable& table, std::size_t index) noexcept
    : m_Table(&table), m_Index(index)
{
}

bool Name::IsNone() const noexcept
{
    return m_Table == nullptr;
}

std::size_t Name::GetIndex() const noexcept
{
    return m_Index;
}

StringView Name::view() const noexcept
{
    if (m_Table == nullptr)
    {
        return {};
    }
    return m_Table->Resolve(*this);
}

bool operator==(Name left, Name right) noexcept
{
    return left.m_Table == right.m_Table && left.m_Index == right.m_Index;
}

bool operator!=(Name left, Name right) noexcept
{
    return (left == right) == false;
}

std::size_t NameHasher::operator()(Name name) const noexcept
{
    const std::size_t tableHash = reinterpret_cast<std::size_t>(name.m_Table);
    // Pointer alignmentで空く下位bitだけに依存しないよう、Tableとindexを混合します。
    std::size_t result = tableHash;
    result ^= name.m_Index + static_cast<std::size_t>(0x9e3779b9u) + (result << 6) + (result >> 2);
    return result;
}

NameTable::NameTable(Allocator& allocator, HashFunction hashFunction) noexcept
    : m_Allocator(&allocator),
      m_Entries(allocator),
      m_HashHeads(allocator),
      m_HashFunction(hashFunction != nullptr ? hashFunction : &NameTable::HashString)
{
}

NameTable::~NameTable()
{
    // Entryは個別確保しているため、VectorやHash tableを解放する前に文字列を破棄します。
    // これにより短いSSO文字列もIntern後にEntryの移動でaddressが変化しません。
    for (Entry* entry : m_Entries)
    {
        DestroyEntry(entry);
    }
}

Name NameTable::Intern(StringView text)
{
    if (text.empty())
    {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    const std::uint64_t hash = m_HashFunction(text);
    std::size_t* headIndex = m_HashHeads.find(hash);
    if (headIndex != nullptr)
    {
        std::size_t index = *headIndex;
        while (index != InvalidIndex)
        {
            assert(index < m_Entries.size());
            const Entry* entry = m_Entries[index];
            if (entry->Text == text)
            {
                return Name(*this, index);
            }
            index = entry->NextIndex;
        }
    }

    if (m_Entries.size() == (std::numeric_limits<std::size_t>::max)())
    {
        throw std::bad_alloc{};
    }

    const std::size_t newIndex = m_Entries.size();
    Entry* entry = CreateEntry(text, headIndex != nullptr ? *headIndex : InvalidIndex);
    try
    {
        m_Entries.push_back(entry);
    }
    catch (...)
    {
        DestroyEntry(entry);
        throw;
    }

    if (headIndex != nullptr)
    {
        // 既存Hash chainの先頭更新はnoexceptで、Entry公開後に失敗する処理はありません。
        *headIndex = newIndex;
    }
    else
    {
        try
        {
            const auto inserted = m_HashHeads.emplace(hash, newIndex);
            assert(inserted.second == true);
        }
        catch (...)
        {
            m_Entries.pop_back();
            DestroyEntry(entry);
            throw;
        }
    }

    return Name(*this, newIndex);
}

StringView NameTable::Resolve(Name name) const noexcept
{
    if (name.m_Table != this)
    {
        assert(name.IsNone() && "NameTable: another table's Name cannot be resolved");
        return {};
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    assert(name.m_Index < m_Entries.size());
    if (name.m_Index >= m_Entries.size())
    {
        return {};
    }
    return m_Entries[name.m_Index]->Text.view();
}

bool NameTable::Contains(Name name) const noexcept
{
    if (name.m_Table != this)
    {
        return name.IsNone();
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    return name.m_Index < m_Entries.size();
}

std::size_t NameTable::size() const noexcept
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Entries.size();
}

Allocator* NameTable::GetAllocator() const noexcept
{
    return m_Allocator;
}

std::uint64_t NameTable::HashString(StringView text) noexcept
{
    constexpr std::uint64_t OffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t Prime = 1099511628211ull;

    std::uint64_t hash = OffsetBasis;
    for (const char value : text)
    {
        hash ^= static_cast<unsigned char>(value);
        hash *= Prime;
    }
    return hash;
}

NameTable::Entry* NameTable::CreateEntry(StringView text, std::size_t nextIndex)
{
    void* memory = m_Allocator->Allocate(sizeof(Entry), alignof(Entry));
    if (memory == nullptr)
    {
        throw std::bad_alloc{};
    }

    try
    {
        return new (memory) Entry(*m_Allocator, text, nextIndex);
    }
    catch (...)
    {
        m_Allocator->Deallocate(memory);
        throw;
    }
}

void NameTable::DestroyEntry(Entry* entry) noexcept
{
    if (entry == nullptr)
    {
        return;
    }
    entry->~Entry();
    m_Allocator->Deallocate(entry);
}

} // namespace Raven
