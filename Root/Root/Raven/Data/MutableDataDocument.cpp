#include "Raven/Data/MutableDataDocument.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace Raven
{
namespace Data
{
namespace
{
bool Fail(DataError* error, DataErrorCode code)
{
    if (error != nullptr) { *error = {code, 0}; }
    return false;
}
bool CheckLimits(const DataLimits& limits, DataError* error)
{
    if (limits.MaxTokens == InvalidTokenId || limits.MaxInputBytes == InvalidTokenId)
    { return Fail(error, DataErrorCode::InvalidLimits); }
    return true;
}
std::uint64_t NextOwner()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    for (;;)
    {
        // アドレス再利用・整数wrapでも別Documentの古いHandleを復活させません。
        if (value == (std::numeric_limits<std::uint64_t>::max)()) { throw std::length_error("Document identity exhausted"); }
        if (next.compare_exchange_weak(value, value + 1)) { return value; }
    }
}
constexpr unsigned InsertArrayOperation = 0;
constexpr unsigned InsertObjectOperation = 1;
constexpr unsigned RemoveOperation = 2;
constexpr unsigned ReplaceOperation = 3;
}

// 既存HandlePoolはSlotごとの安定アドレスを提供する非Copy型です。
// 編集の検証後CommitにはStorage全体の候補Copyが必要なため、ここでは連続Slotを使います。
// 同じPoolHandleのindex/generation規約と、世代上限でのSlot退役規約を採用します。
struct MutableDataDocument::Storage
{
    struct Edge { std::string Key; std::uint32_t Child = InvalidTokenId; };
    struct Slot
    {
        std::uint32_t Generation = 1;
        std::uint32_t NextFree = InvalidTokenId;
        std::uint32_t Parent = InvalidTokenId;
        bool Live = false;
        DataType Type = DataType::Null;
        std::uint64_t Payload = 0;
        std::string Bytes;
        std::vector<Edge> Children;
    };
    std::vector<Slot> Slots;
    std::uint32_t Root = InvalidTokenId;
    std::uint32_t Free = InvalidTokenId;
    std::size_t LiveCount = 0;
    std::size_t TokenCount = 0;
    std::size_t ByteCount = 0;

    void CheckBytes(std::size_t length, const DataLimits& limits) const
    {
        if (length > limits.MaxValueBytes || ByteCount > limits.MaxInputBytes
            || length > limits.MaxInputBytes - ByteCount)
        { throw std::length_error("Mutable byte limit"); }
    }

    std::uint32_t Allocate(const DataLimits& limits)
    {
        if (TokenCount >= limits.MaxTokens) { throw std::length_error("Mutable token limit"); }
        std::uint32_t index = Free;
        if (index != InvalidTokenId) { Free = Slots[index].NextFree; }
        else
        {
            if (Slots.size() >= limits.MaxTokens || Slots.size() >= InvalidTokenId)
            { throw std::length_error("Mutable slot limit"); }
            index = static_cast<std::uint32_t>(Slots.size());
            Slots.emplace_back();
        }
        Slots[index].Live = true;
        Slots[index].NextFree = InvalidTokenId;
        ++LiveCount;
        ++TokenCount;
        return index;
    }
    bool Import(DataElement element, std::uint32_t parent, std::uint32_t depth,
        const DataLimits& limits, std::uint32_t& output, DataError* error)
    {
        const auto* token = element.GetToken();
        if (token == nullptr) { return Fail(error, DataErrorCode::InvalidStructure); }
        const bool container = token->Type == DataType::Array || token->Type == DataType::Object;
        if (container && (depth >= limits.MaxDepth || depth >= MutableDataMaxDepth)) { return Fail(error, DataErrorCode::LimitExceeded); }
        if (container && token->Count > limits.MaxContainerElements) { return Fail(error, DataErrorCode::LimitExceeded); }
        const auto index = Allocate(limits);
        Slots[index].Type = token->Type;
        Slots[index].Payload = token->Payload;
        Slots[index].Parent = parent;
        if (token->Type == DataType::String || token->Type == DataType::Binary)
        {
            // 外部Documentの大きい値を小さい編集Limitへ取り込む際も、展開用確保の前に検査します。
            if (token->Length > limits.MaxValueBytes) { return Fail(error, DataErrorCode::LimitExceeded); }
            CheckBytes(token->Length, limits);
            if (token->Type == DataType::String)
            {
                if (element.TryGetDecodedString(Slots[index].Bytes) == false) { return Fail(error, DataErrorCode::OutOfMemory); }
                Slots[index].Payload = DecodedUtf8String;
            }
            else
            {
                std::string_view source;
                if (element.TryGetSource(source) == false) { return Fail(error, DataErrorCode::InvalidStructure); }
                Slots[index].Bytes.assign(source);
            }
            ByteCount += Slots[index].Bytes.size();
        }
        if (container)
        {
            auto child = element.GetFirstChild();
            for (std::uint32_t i = 0; i < token->Count; ++i)
            {
                Edge edge;
                if (token->Type == DataType::Object)
                {
                    const auto* key = child.GetToken();
                    if (key == nullptr || key->Length > limits.MaxValueBytes) { return Fail(error, DataErrorCode::LimitExceeded); }
                    CheckBytes(key->Length, limits);
                    if (TokenCount >= limits.MaxTokens) { return Fail(error, DataErrorCode::LimitExceeded); }
                    if (child.TryGetDecodedString(edge.Key) == false) { return Fail(error, DataErrorCode::OutOfMemory); }
                    ++TokenCount;
                    ByteCount += edge.Key.size();
                    child = child.GetNextSibling();
                }
                if (Import(child, index, depth + 1, limits, edge.Child, error) == false) { return false; }
                // Import中にSlotsが再配置されるため、Slot参照を再帰呼び出しを跨いで保持しません。
                Slots[index].Children.push_back(std::move(edge));
                child = child.GetNextSibling();
            }
        }
        output = index;
        return true;
    }
    void Erase(std::uint32_t index)
    {
        Slot& slot = Slots[index];
        for (const auto& edge : slot.Children) { Erase(edge.Child); }
        if (slot.Type == DataType::Object)
        {
            TokenCount -= slot.Children.size();
            for (const auto& edge : slot.Children) { ByteCount -= edge.Key.size(); }
        }
        ByteCount -= slot.Bytes.size();
        --TokenCount;
        slot.Children.clear();
        slot.Bytes.clear();
        slot.Live = false;
        --LiveCount;
        if (slot.Generation != (std::numeric_limits<std::uint32_t>::max)())
        {
            ++slot.Generation;
            slot.NextFree = Free;
            Free = index;
        }
        // 上限世代のSlotはFree Listへ戻さず永久退役します。
    }
    bool Emit(std::uint32_t index, std::uint32_t depth, std::string& source,
        std::vector<DataToken>& tape, const DataLimits& limits, DataError* error) const
    {
        const Slot& slot = Slots[index];
        const bool container = slot.Type == DataType::Array || slot.Type == DataType::Object;
        if (container && (depth >= limits.MaxDepth || depth >= MutableDataMaxDepth)) { return Fail(error, DataErrorCode::LimitExceeded); }
        if (slot.Children.size() > limits.MaxContainerElements || tape.size() >= limits.MaxTokens)
        { return Fail(error, DataErrorCode::LimitExceeded); }
        const auto position = static_cast<TokenId>(tape.size());
        DataToken token;
        token.Type = slot.Type; token.Payload = slot.Payload;
        token.Count = static_cast<std::uint32_t>(slot.Children.size());
        if (slot.Bytes.size() > limits.MaxValueBytes || source.size() > limits.MaxInputBytes
            || slot.Bytes.size() > limits.MaxInputBytes - source.size())
        { return Fail(error, DataErrorCode::LimitExceeded); }
        token.Offset = static_cast<std::uint32_t>(source.size());
        token.Length = static_cast<std::uint32_t>(slot.Bytes.size());
        source += slot.Bytes;
        tape.push_back(token);
        for (const auto& edge : slot.Children)
        {
            if (slot.Type == DataType::Object)
            {
                if (tape.size() >= limits.MaxTokens || edge.Key.size() > limits.MaxValueBytes
                    || edge.Key.size() > limits.MaxInputBytes - source.size())
                { return Fail(error, DataErrorCode::LimitExceeded); }
                DataToken key;
                key.Type = DataType::String; key.Payload = DecodedUtf8String;
                key.Offset = static_cast<std::uint32_t>(source.size());
                key.Length = static_cast<std::uint32_t>(edge.Key.size());
                key.Next = static_cast<TokenId>(tape.size() + 1);
                source += edge.Key;
                tape.push_back(key);
            }
            if (Emit(edge.Child, depth + 1, source, tape, limits, error) == false) { return false; }
        }
        tape[position].Next = static_cast<TokenId>(tape.size());
        return true;
    }
    bool Freeze(std::uint32_t root, DataDocument& output, const DataLimits& limits, DataError* error) const
    {
        if (root == InvalidTokenId) { return Fail(error, DataErrorCode::InvalidStructure); }
        std::string source;
        std::vector<DataToken> tape;
        if (Emit(root, 0, source, tape, limits, error) == false) { return false; }
        return DataDocument::Create(source, tape, output, error, limits);
    }
};

bool MutableDataHandle::operator==(const MutableDataHandle& other) const noexcept
{ return Owner == other.Owner && Epoch == other.Epoch && Slot == other.Slot; }
bool MutableDataHandle::operator!=(const MutableDataHandle& other) const noexcept { return (*this == other) == false; }
MutableDataDocument::MutableDataDocument(const DataLimits& limits)
    : m_Storage(std::make_unique<Storage>()), m_Limits(limits), m_Owner(NextOwner()) {}
MutableDataDocument::~MutableDataDocument() = default;
const DataLimits& MutableDataDocument::GetLimits() const { return m_Limits; }
std::size_t MutableDataDocument::GetSlotCount() const { return m_Storage->Slots.size(); }
std::uint64_t MutableDataDocument::GetRevision() const { return m_Revision; }
MutableDataHandle MutableDataDocument::Handle(std::uint32_t index) const
{
    if (index == InvalidTokenId) { return {}; }
    return {m_Owner, m_Epoch, {index, m_Storage->Slots[index].Generation}};
}
MutableDataHandle MutableDataDocument::GetRoot() const { return Handle(m_Storage->Root); }
bool MutableDataDocument::IsValid(MutableDataHandle handle) const
{
    return handle.Owner == m_Owner && handle.Epoch == m_Epoch && handle.Slot.index < m_Storage->Slots.size()
        && m_Storage->Slots[handle.Slot.index].Live
        && m_Storage->Slots[handle.Slot.index].Generation == handle.Slot.generation;
}
bool MutableDataDocument::TryGetType(MutableDataHandle handle, DataType& output) const
{
    if (IsValid(handle) == false) { return false; }
    output = m_Storage->Slots[handle.Slot.index].Type; return true;
}
bool MutableDataDocument::TryGetCount(MutableDataHandle handle, std::uint32_t& output) const
{
    DataType type;
    if (TryGetType(handle, type) == false || (type != DataType::Array && type != DataType::Object)) { return false; }
    output = static_cast<std::uint32_t>(m_Storage->Slots[handle.Slot.index].Children.size()); return true;
}
bool MutableDataDocument::TryGetChild(MutableDataHandle parent, std::uint32_t index, MutableDataHandle& output) const
{
    std::uint32_t count;
    if (TryGetCount(parent, count) == false || index >= count) { return false; }
    output = Handle(m_Storage->Slots[parent.Slot.index].Children[index].Child); return true;
}
bool MutableDataDocument::TryFind(MutableDataHandle parent, std::string_view key, MutableDataHandle& output) const
{
    DataType type;
    if (TryGetType(parent, type) == false || type != DataType::Object) { return false; }
    for (const auto& edge : m_Storage->Slots[parent.Slot.index].Children)
    {
        if (edge.Key == key) { output = Handle(edge.Child); return true; }
    }
    return false;
}
bool MutableDataDocument::TryGetStringView(MutableDataHandle handle, std::string_view& output) const
{
    DataType type;
    if (TryGetType(handle, type) == false || type != DataType::String) { return false; }
    output = m_Storage->Slots[handle.Slot.index].Bytes; return true;
}
bool MutableDataDocument::Freeze(DataDocument& output, DataError* error) const { return Freeze(GetRoot(), output, error); }
bool MutableDataDocument::Freeze(MutableDataHandle root, DataDocument& output, DataError* error) const
{
    if (error != nullptr) { *error = {}; }
    if (CheckLimits(m_Limits, error) == false) { return false; }
    if (IsValid(root) == false) { return Fail(error, DataErrorCode::InvalidStructure); }
    try { return m_Storage->Freeze(root.Slot.index, output, m_Limits, error); }
    catch (const std::bad_alloc&) { return Fail(error, DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(error, DataErrorCode::LimitExceeded); }
}
bool MutableDataDocument::Reset(DataElement source, DataError* error)
{
    if (error != nullptr) { *error = {}; }
    if (CheckLimits(m_Limits, error) == false) { return false; }
    if (m_Epoch == (std::numeric_limits<std::uint64_t>::max)() || m_Revision == (std::numeric_limits<std::uint64_t>::max)())
    { return Fail(error, DataErrorCode::LimitExceeded); }
    try
    {
        auto candidate = std::make_unique<Storage>();
        if (candidate->Import(source, InvalidTokenId, 0, m_Limits, candidate->Root, error) == false) { return false; }
        DataDocument validated;
        if (candidate->Freeze(candidate->Root, validated, m_Limits, error) == false) { return false; }
        m_Storage.swap(candidate); ++m_Epoch; ++m_Revision; return true;
    }
    catch (const std::bad_alloc&) { return Fail(error, DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(error, DataErrorCode::LimitExceeded); }
}
bool MutableDataDocument::Compact(DataError* error)
{
    DataDocument frozen;
    return Freeze(frozen, error) && Reset(frozen.GetRoot(), error);
}
bool MutableDataDocument::Edit(MutableDataHandle target, std::uint32_t index, std::string_view key, DataElement value,
    unsigned operation, MutableDataHandle* output, DataError* error)
{
    if (error != nullptr) { *error = {}; }
    if (CheckLimits(m_Limits, error) == false) { return false; }
    if (IsValid(target) == false) { return Fail(error, DataErrorCode::InvalidStructure); }
    if (m_Revision == (std::numeric_limits<std::uint64_t>::max)()) { return Fail(error, DataErrorCode::LimitExceeded); }
    try
    {
        // 初期版は編集頻度より失敗時の安全性を優先します。候補Copyを既存Document検証へ通し、
        // 確保失敗・上限超過・不正UTF-8・重複キーのどれでも元Storageへ部分反映しません。
        auto candidate = std::make_unique<Storage>(*m_Storage);
        const auto slot = target.Slot.index;
        std::uint32_t created = InvalidTokenId;
        if (operation == InsertArrayOperation || operation == InsertObjectOperation)
        {
            const bool object = operation == InsertObjectOperation;
            if (candidate->Slots[slot].Type != (object ? DataType::Object : DataType::Array))
            { return Fail(error, DataErrorCode::InvalidStructure); }
            const auto count = candidate->Slots[slot].Children.size();
            if (count >= m_Limits.MaxContainerElements || (object && key.size() > m_Limits.MaxValueBytes))
            { return Fail(error, DataErrorCode::LimitExceeded); }
            if (object)
            {
                for (const auto& edge : candidate->Slots[slot].Children)
                { if (edge.Key == key) { return Fail(error, DataErrorCode::DuplicateKey); } }
                candidate->CheckBytes(key.size(), m_Limits);
                if (candidate->TokenCount >= m_Limits.MaxTokens) { return Fail(error, DataErrorCode::LimitExceeded); }
                ++candidate->TokenCount;
                candidate->ByteCount += key.size();
                index = static_cast<std::uint32_t>(count);
            }
            if (index > count) { return Fail(error, DataErrorCode::InvalidStructure); }
            if (candidate->Import(value, slot, 0, m_Limits, created, error) == false) { return false; }
            auto& edges = candidate->Slots[slot].Children;
            Storage::Edge edge{std::string(key), created};
            edges.insert(edges.begin() + index, std::move(edge));
        }
        else
        {
            const auto parent = candidate->Slots[slot].Parent;
            if (operation == RemoveOperation && parent == InvalidTokenId) { return Fail(error, DataErrorCode::InvalidStructure); }
            std::size_t edgeIndex = 0;
            if (parent != InvalidTokenId)
            {
                const auto& edges = candidate->Slots[parent].Children;
                while (edgeIndex < edges.size() && edges[edgeIndex].Child != slot) { ++edgeIndex; }
                if (edgeIndex == edges.size()) { return Fail(error, DataErrorCode::InvalidStructure); }
            }
            candidate->Erase(slot);
            if (operation == RemoveOperation)
            {
                auto& edges = candidate->Slots[parent].Children;
                if (candidate->Slots[parent].Type == DataType::Object)
                {
                    --candidate->TokenCount;
                    candidate->ByteCount -= edges[edgeIndex].Key.size();
                }
                edges.erase(edges.begin() + edgeIndex);
            }
            else
            {
                if (candidate->Import(value, parent, 0, m_Limits, created, error) == false) { return false; }
                if (parent == InvalidTokenId) { candidate->Root = created; }
                else { candidate->Slots[parent].Children[edgeIndex].Child = created; }
            }
        }
        DataDocument validated;
        if (candidate->Freeze(candidate->Root, validated, m_Limits, error) == false) { return false; }
        m_Storage.swap(candidate); ++m_Revision;
        if (output != nullptr) { *output = Handle(created); }
        return true;
    }
    catch (const std::bad_alloc&) { return Fail(error, DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(error, DataErrorCode::LimitExceeded); }
}
bool MutableDataDocument::InsertArray(MutableDataHandle parent, std::uint32_t index, DataElement value,
    MutableDataHandle& inserted, DataError* error)
{ return Edit(parent, index, {}, value, InsertArrayOperation, &inserted, error); }
bool MutableDataDocument::InsertObject(MutableDataHandle parent, std::string_view key, DataElement value,
    MutableDataHandle& inserted, DataError* error)
{ return Edit(parent, 0, key, value, InsertObjectOperation, &inserted, error); }
bool MutableDataDocument::Remove(MutableDataHandle handle, DataError* error)
{ return Edit(handle, 0, {}, {}, RemoveOperation, nullptr, error); }
bool MutableDataDocument::Replace(MutableDataHandle handle, DataElement value, MutableDataHandle& replacement, DataError* error)
{ return Edit(handle, 0, {}, value, ReplaceOperation, &replacement, error); }
}
}
