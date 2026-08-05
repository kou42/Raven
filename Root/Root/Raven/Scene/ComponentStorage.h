#pragma once

#include <vector>
#include <cstddef>
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
template<class T>
class ComponentStorage final : public IComponentStorage
{
public:
    using ComponentType = T;
    using ContainerType = std::unordered_map<EntityID, T>;

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
    T& Emplace(EntityID entityID, Args&&... args)
    {
        if (Has(entityID))
        {
            throw std::runtime_error("The entity already has this component.");
        }

        EnsureSparseCapacity(entityID);

        const SizeType denseIndex = m_Components.size();

        /*
         * まずComponentを構築する。
         *
         * Component構築が例外を投げた場合は、
         * Entity配列とSparse配列を変更しない。
         */
        m_Components.emplace_back(std::forward<Args>(args)...);

        try
        {
            m_Entities.push_back(entityID);
        }
        catch (...)
        {
            m_Components.pop_back();
            throw;
        }

        m_Sparse[entityID] = denseIndex;

        return m_Components.back();
#if 0
        auto [iterator, inserted] = m_Components.try_emplace(entityID, std::forward<Args>(args)...);

        if (inserted == false)
        {
            throw std::runtime_error("The entity already has this component.");
        }

        // m_Componentsへの追加後にm_Entities.push_back()が失敗した場合は、追加したComponentを取り消しています。
        // m_Componentsには存在するけど、m_Entitiesには存在しないという不整合を防ぎます。
        try
        {
            m_Entities.push_back(entityID);
        }
        catch (...)
        {
            m_Components.erase(iterator);
            throw;
        }

        return iterator->second;
#endif
    }

    bool Has(EntityIndex entityIndex) const override
    {
        if (static_cast<SizeType>(entityIndex) >= m_Sparse.size())
        {
            return false;
        }

        const SizeType denseIndex = m_Sparse[entityIndex];

        if (denseIndex == InvalidIndex) {
            return false;
        }

        if (denseIndex >= m_Entities.size()) {
            return false;
        }

        return m_Entities[denseIndex] == entityIndex;
#if 0
        return m_Components.find(entityID) != m_Components.end();
#endif
    }

    bool Remove(EntityIndex entityIndex) override
    {
        if (Has(entityIndex) == false) {
            return false;
        }

        const SizeType removedIndex = m_Sparse[entityIndex];

        const SizeType lastIndex = m_Components.size() - 1;

        /*
         * 最後の要素でない場合は、
         * 最後のEntityとComponentを削除位置へ移動する。
         */
        if (removedIndex != lastIndex)
        {
            const EntityIndex movedEntity = m_Entities[lastIndex];

            m_Entities[removedIndex] = movedEntity;

            m_Components[removedIndex] = std::move(m_Components[lastIndex]);

            m_Sparse[movedEntity] = removedIndex;
        }

        m_Entities.pop_back();
        m_Components.pop_back();

        m_Sparse[entityIndex] = InvalidIndex;

        return true;

        // 旧実装
#if 0
        const auto componentIterator = m_Components.find(entityID);

        if (componentIterator == m_Components.end()) {
            return false;
        }

        const auto entityIterator = std::find(m_Entities.begin(), m_Entities.end(), entityID);

        if (entityIterator != m_Entities.end())
        {
            m_Entities.erase(entityIterator);
        }

        // Entity IDの並び順を保証しなくてよければ、swap-and-popが使えます。
#if 0
        if (entityIterator != m_Entities.end())
        {
            *entityIterator = m_Entities.back();
            m_Entities.pop_back();
        }
#endif

        m_Components.erase(componentIterator);

        return true;
#endif
    }

    T& Get(EntityIndex entityIndex)
    {
        if (Has(entityIndex) == false)
        {
            throw std::out_of_range("The entity does not have this component.");
        }

        return m_Components[m_Sparse[entityIndex]];
        //return m_Components.at(entityID);
    }

    const T& Get(EntityIndex entityIndex) const
    {
        if (Has(entityIndex) == false)
        {
            throw std::out_of_range("The entity does not have this component.");
        }

        return m_Components[m_Sparse[entityIndex]];
        // return m_Components.at(entityID);
    }

    T* TryGet(EntityIndex entityIndex)
    {
        if (Has(entityIndex) == false) {
            return nullptr;
        }

        return &m_Components[m_Sparse[entityIndex]];
        //const auto iterator = m_Components.find(entityID);
        //
        //if (iterator == m_Components.end())
        //   return nullptr;

        //return &iterator->second;
    }

    const T* TryGet(EntityIndex entityIndex) const
    {
        if (Has(entityIndex) == false) {
            return nullptr;
        }

        return &m_Components[m_Sparse[entityIndex]];
        /*const auto iterator = m_Components.find(entityID);

        if (iterator == m_Components.end())
            return nullptr;

        return &iterator->second;
        */
    }

    void Clear() override
    {
        /*
         * Sparse配列の容量を保持したまま、
         * 使用中だったEntityだけを無効化する。
         */
        for (EntityIndex entityID : m_Entities)
        {
            m_Sparse[entityID] = InvalidIndex;
        }

        m_Entities.clear();
        m_Components.clear();
    }

    EntityIndex GetEntityID(std::size_t index) const override
    {
        //内部処理の速度を優先する段階では、return m_Entities[index];でもOK
        return m_Entities.at(index);
    }

    EntityIndex GetEntityIndex(std::size_t index) const override
    {
        return m_Entities.at(index);
    }

    bool Empty() const override
    {
        return m_Components.empty();
    }

    std::size_t Size() const override
    {
        return m_Components.size();
    }

    void CollectEntityIDs(std::vector<EntityIndex>& destination) const override
    {
        destination.reserve(destination.size() + m_Components.size());

        destination.insert(destination.end(), m_Entities.begin(), m_Entities.end());

        //for (const auto& [entityID, component] : m_Components)
        //{
        //    static_cast<void>(component);
        //    destination.push_back(entityID);
        //}

        //for (const auto& entry : m_Components)
        //{
        //    destination.push_back(entry.first);
        //}
    }

    void Reserve(SizeType componentCapacity)
    {
        m_Entities.reserve(componentCapacity);
        m_Components.reserve(componentCapacity);
    }

    void ReserveEntities(EntityID maximumEntityID)
    {
        const SizeType requiredSize = static_cast<SizeType>(maximumEntityID) + 1;

        if (m_Sparse.size() < requiredSize)
        {
            m_Sparse.resize(requiredSize, InvalidIndex );
        }
    }

    T& GetByIndex(SizeType index)
    {
        return m_Components.at(index);
    }

    const T& GetByIndex(SizeType index) const
    {
        return m_Components.at(index);
    }

    EntityIndex GetEntityByIndex(SizeType index) const
    {
        return m_Entities.at(index);
    }

    Iterator begin()
    {
        return m_Components.begin();
    }

    Iterator end()
    {
        return m_Components.end();
    }

    ConstIterator begin() const
    {
        return m_Components.begin();
    }

    ConstIterator end() const
    {
        return m_Components.end();
    }

    ConstIterator cbegin() const
    {
        return m_Components.cbegin();
    }

    ConstIterator cend() const
    {
        return m_Components.cend();
    }

    void EnsureSparseCapacity(EntityID entityID)
    {
        const SizeType requiredSize = static_cast<SizeType>(entityID) + 1;

        if (m_Sparse.size() >= requiredSize) {
            return;
        }

        m_Sparse.resize(requiredSize, InvalidIndex);
    }
};

#if 0
template<class T>
class ComponentStorage
{
public:
    using ComponentType = T;
    using ContainerType = std::unordered_map<EntityID, T>;

    using Iterator = typename ContainerType::iterator;
    using ConstIterator = typename ContainerType::const_iterator;

public:
    ComponentStorage() = default;
    ~ComponentStorage() = default;

    ComponentStorage(const ComponentStorage&) = default;
    ComponentStorage& operator=(const ComponentStorage&) = default;

    ComponentStorage(ComponentStorage&&) noexcept = default;
    ComponentStorage& operator=(ComponentStorage&&) noexcept = default;

    template<class... Args>
    T& Emplace(EntityID entityID, Args&&... args)
    {
        auto [iterator, inserted] = m_Components.try_emplace(
            entityID,
            std::forward<Args>(args)...
        );

        if (!inserted)
        {
            throw std::runtime_error(
                "The entity already has this component."
            );
        }

        return iterator->second;
    }

    bool Remove(EntityID entityID)
    {
        return m_Components.erase(entityID) > 0;
    }

    bool Has(EntityID entityID) const
    {
        return m_Components.find(entityID) != m_Components.end();
    }

    T& Get(EntityID entityID)
    {
        return m_Components.at(entityID);
    }

    const T& Get(EntityID entityID) const
    {
        return m_Components.at(entityID);
    }

    T* TryGet(EntityID entityID)
    {
        auto iterator = m_Components.find(entityID);

        if (iterator == m_Components.end())
            return nullptr;

        return &iterator->second;
    }

    const T* TryGet(EntityID entityID) const
    {
        auto iterator = m_Components.find(entityID);

        if (iterator == m_Components.end())
            return nullptr;

        return &iterator->second;
    }

    void Clear()
    {
        m_Components.clear();
    }

    bool Empty() const
    {
        return m_Components.empty();
    }

    std::size_t Size() const
    {
        return m_Components.size();
    }

    Iterator begin()
    {
        return m_Components.begin();
    }

    Iterator end()
    {
        return m_Components.end();
    }

    ConstIterator begin() const
    {
        return m_Components.begin();
    }

    ConstIterator end() const
    {
        return m_Components.end();
    }

    ConstIterator cbegin() const
    {
        return m_Components.cbegin();
    }

    ConstIterator cend() const
    {
        return m_Components.cend();
    }

private:
    ContainerType m_Components;
};
#endif

}
