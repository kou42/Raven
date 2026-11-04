#pragma once

namespace Raven
{

template<class T>
template<class... Args>
T& ComponentStorage<T>::Emplace(EntityID entityID, Args&&... args)
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

template<class T>
bool ComponentStorage<T>::Has(EntityIndex entityIndex) const
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

template<class T>
bool ComponentStorage<T>::Remove(EntityIndex entityIndex)
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

        // 移動代入が失敗してもEntity/Sparseの対応を壊さないよう、索引の更新は後にします。
        // T自身の値の巻き戻しは保証できないため、通常のComponentはnoexcept移動を推奨します。
        m_Components[removedIndex] = std::move(m_Components[lastIndex]);

        m_Entities[removedIndex] = movedEntity;

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

template<class T>
T& ComponentStorage<T>::Get(EntityIndex entityIndex)
{
    if (Has(entityIndex) == false)
    {
        throw std::out_of_range("The entity does not have this component.");
    }

    return m_Components[m_Sparse[entityIndex]];
    //return m_Components.at(entityID);
}

template<class T>
const T& ComponentStorage<T>::Get(EntityIndex entityIndex) const
{
    if (Has(entityIndex) == false)
    {
        throw std::out_of_range("The entity does not have this component.");
    }

    return m_Components[m_Sparse[entityIndex]];
    // return m_Components.at(entityID);
}

template<class T>
T* ComponentStorage<T>::TryGet(EntityIndex entityIndex)
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

template<class T>
const T* ComponentStorage<T>::TryGet(EntityIndex entityIndex) const
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

template<class T>
void ComponentStorage<T>::Clear()
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

template<class T>
EntityIndex ComponentStorage<T>::GetEntityID(std::size_t index) const
{
    //内部処理の速度を優先する段階では、return m_Entities[index];でもOK
    return m_Entities.at(index);
}

template<class T>
EntityIndex ComponentStorage<T>::GetEntityIndex(std::size_t index) const
{
    return m_Entities.at(index);
}

template<class T>
bool ComponentStorage<T>::Empty() const
{
    return m_Components.empty();
}

template<class T>
std::size_t ComponentStorage<T>::Size() const
{
    return m_Components.size();
}

template<class T>
void ComponentStorage<T>::CollectEntityIDs(std::vector<EntityIndex>& destination) const
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

template<class T>
void ComponentStorage<T>::Reserve(SizeType componentCapacity)
{
    m_Entities.reserve(componentCapacity);
    m_Components.reserve(componentCapacity);
}

template<class T>
void ComponentStorage<T>::ReserveEntities(EntityID maximumEntityID)
{
    const SizeType requiredSize = static_cast<SizeType>(maximumEntityID) + 1;

    if (m_Sparse.size() < requiredSize)
    {
        m_Sparse.resize(requiredSize, InvalidIndex );
    }
}

template<class T>
T& ComponentStorage<T>::GetByIndex(SizeType index)
{
    return m_Components.at(index);
}

template<class T>
const T& ComponentStorage<T>::GetByIndex(SizeType index) const
{
    return m_Components.at(index);
}

template<class T>
EntityIndex ComponentStorage<T>::GetEntityByIndex(SizeType index) const
{
    return m_Entities.at(index);
}

template<class T>
typename ComponentStorage<T>::Iterator ComponentStorage<T>::begin()
{
    return m_Components.begin();
}

template<class T>
typename ComponentStorage<T>::Iterator ComponentStorage<T>::end()
{
    return m_Components.end();
}

template<class T>
typename ComponentStorage<T>::ConstIterator ComponentStorage<T>::begin() const
{
    return m_Components.begin();
}

template<class T>
typename ComponentStorage<T>::ConstIterator ComponentStorage<T>::end() const
{
    return m_Components.end();
}

template<class T>
typename ComponentStorage<T>::ConstIterator ComponentStorage<T>::cbegin() const
{
    return m_Components.cbegin();
}

template<class T>
typename ComponentStorage<T>::ConstIterator ComponentStorage<T>::cend() const
{
    return m_Components.cend();
}

template<class T>
void ComponentStorage<T>::EnsureSparseCapacity(EntityID entityID)
{
    const SizeType requiredSize = static_cast<SizeType>(entityID) + 1;

    if (m_Sparse.size() >= requiredSize) {
        return;
    }

    m_Sparse.resize(requiredSize, InvalidIndex);
}

}
