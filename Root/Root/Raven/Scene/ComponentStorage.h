#pragma once

#include <vector>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "Raven/Scene/Entity.h"

namespace Raven
{

// Component型に依存しないStorageの共通インターフェース
class IComponentStorage
{
public:
    virtual ~IComponentStorage() = default;

    virtual bool Has(EntityIndex entityID) const = 0;
    virtual bool Remove(EntityIndex entityID) = 0;

    virtual void Clear() = 0;

    virtual bool Empty() const = 0;
    virtual std::size_t Size() const = 0;

    virtual void CollectEntityIDs(std::vector<EntityIndex>& destination) const = 0;
    virtual EntityID GetEntityID(std::size_t index) const = 0;
    virtual EntityIndex GetEntityIndex(std::size_t index) const = 0;

};


// 特定のComponent型を保存するStorage
// Scene内部のIndex専用Storageです。所属と世代の検証は呼び出し側が担当します。
// 追加/Reserveによるvector再確保は全参照とIteratorを無効化します。削除は削除位置と
// 末尾の参照を無効化し、swap-and-popで順序も変えます。Clearは全参照を無効化します。
// 再確保時の例外保証はstd::vector<T>に従い、copy可能またはnoexcept move構築を推奨します。
template<class T>
class ComponentStorage final : public IComponentStorage
{
public:
    using ComponentType = T;
    using ContainerType = std::vector<T>;

    using SizeType = std::size_t;

    using Iterator = typename ContainerType::iterator;
    using ConstIterator = typename ContainerType::const_iterator;

private:
    inline static constexpr SizeType InvalidIndex = std::numeric_limits<SizeType>::max();

private:
    //ContainerType m_Components;
	std::vector<T> m_Components;
    std::vector<EntityIndex> m_Entities;
	std::vector<SizeType> m_Sparse;

public:
    ComponentStorage() = default;
    ~ComponentStorage() override = default;

    ComponentStorage(const ComponentStorage&) = default;
    ComponentStorage& operator=(const ComponentStorage&) = default;

    ComponentStorage(ComponentStorage&&) noexcept = default;
    ComponentStorage& operator=(ComponentStorage&&) noexcept = default;

    /*例えば、最初にEntity 5へComponentを追加します
    storage.Emplace(5);
    Sparse配列が不足していれば、
    m_Sparse.resize(6, InvalidIndex);されます。
    その後、
    m_Entities[5]
    m_Components
    [Component for Entity 5]
    m_Sparse
    [Invalid, Invalid, Invalid, Invalid, Invalid, 0]
    になります。
    Entity 5のDense indexは0です。
    */
    template<class... Args>
    T& Emplace(EntityID entityID, Args&&... args);

    bool Has(EntityIndex entityIndex) const override;

    bool Remove(EntityIndex entityIndex) override;

    T& Get(EntityIndex entityIndex);

    const T& Get(EntityIndex entityIndex) const;

    T* TryGet(EntityIndex entityIndex);

    const T* TryGet(EntityIndex entityIndex) const;

    void Clear() override;

    EntityIndex GetEntityID(std::size_t index) const override;

    EntityIndex GetEntityIndex(std::size_t index) const override;

    bool Empty() const override;

    std::size_t Size() const override;

    void CollectEntityIDs(std::vector<EntityIndex>& destination) const override;

    void Reserve(SizeType componentCapacity);

    void ReserveEntities(EntityID maximumEntityID);

    T& GetByIndex(SizeType index);

    const T& GetByIndex(SizeType index) const;

    EntityIndex GetEntityByIndex(SizeType index) const;

    Iterator begin();

    Iterator end();

    ConstIterator begin() const;

    ConstIterator end() const;

    ConstIterator cbegin() const;

    ConstIterator cend() const;

    void EnsureSparseCapacity(EntityID entityID);
};


}

#include "Raven/Scene/ComponentStorage.inl"
