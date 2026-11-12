#pragma once
#include "Raven/Data/DataDocument.h"
#include "Raven/Core/Containers/HandlePool.h"
#include <memory>

namespace Raven
{
namespace Data
{
// 再帰処理のStack使用量を制限する編集用の上限。不変Parserの設定とは独立です。
inline constexpr std::uint32_t MutableDataMaxDepth = 128;
struct MutableDataHandle
{
    std::uint64_t Owner = 0;
    std::uint64_t Epoch = 0;
    PoolHandle Slot;
    bool operator==(const MutableDataHandle& other) const noexcept;
    bool operator!=(const MutableDataHandle& other) const noexcept;
};

// 単一Threadの編集用Storage。不変DocumentのTapeやViewは変更しません。
// 通常編集は生存SlotのHandleを維持し、削除/置換した部分木のHandleだけを無効化します。
// Reset・Compact・Undo/Redo復元はEpochを進め、すべての編集Handleを無効化します。
// 文字列Viewは成功した変更すべてで無効。失敗時は状態・Handle・Viewを維持します。
// WorkerへはFreezeした独立Documentを渡してください。編集と読み取りの競合は非対応です。
class MutableDataDocument
{
public:
    explicit MutableDataDocument(const DataLimits& limits = {});
    ~MutableDataDocument();
    MutableDataDocument(const MutableDataDocument&) = delete;
    MutableDataDocument& operator=(const MutableDataDocument&) = delete;
    MutableDataDocument(MutableDataDocument&&) = delete;
    MutableDataDocument& operator=(MutableDataDocument&&) = delete;

    bool Reset(DataElement source, DataError* error = nullptr);
    bool Freeze(DataDocument& output, DataError* error = nullptr) const;
    bool Freeze(MutableDataHandle root, DataDocument& output, DataError* error = nullptr) const;
    bool Compact(DataError* error = nullptr);
    MutableDataHandle GetRoot() const;
    bool IsValid(MutableDataHandle handle) const;
    bool TryGetType(MutableDataHandle handle, DataType& output) const;
    bool TryGetCount(MutableDataHandle handle, std::uint32_t& output) const;
    bool TryGetChild(MutableDataHandle parent, std::uint32_t index, MutableDataHandle& output) const;
    bool TryFind(MutableDataHandle parent, std::string_view key, MutableDataHandle& output) const;
    bool TryGetStringView(MutableDataHandle handle, std::string_view& output) const;
    bool InsertArray(MutableDataHandle parent, std::uint32_t index, DataElement value,
        MutableDataHandle& inserted, DataError* error = nullptr);
    bool InsertObject(MutableDataHandle parent, std::string_view key, DataElement value,
        MutableDataHandle& inserted, DataError* error = nullptr);
    bool Remove(MutableDataHandle handle, DataError* error = nullptr);
    bool Replace(MutableDataHandle handle, DataElement value, MutableDataHandle& replacement,
        DataError* error = nullptr);
    const DataLimits& GetLimits() const;
    std::size_t GetSlotCount() const;
    std::uint64_t GetRevision() const;
private:
    struct Storage;
    MutableDataHandle Handle(std::uint32_t index) const;
    bool Edit(MutableDataHandle target, std::uint32_t index, std::string_view key, DataElement value,
        unsigned operation, MutableDataHandle* output, DataError* error);
    std::unique_ptr<Storage> m_Storage;
    DataLimits m_Limits;
    std::uint64_t m_Owner;
    std::uint64_t m_Epoch = 1;
    std::uint64_t m_Revision = 0;
};
}
}
