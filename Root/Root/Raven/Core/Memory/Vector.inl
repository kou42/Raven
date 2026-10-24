#pragma once

namespace Raven
{

template<typename T>
Vector<T>::Vector(Allocator& allocator)
        : m_Values(STLAllocatorAdapter<T>(allocator))
{
}

template<typename T>
Vector<T>::Vector(Allocator& allocator, std::initializer_list<T> values)
        : m_Values(values, STLAllocatorAdapter<T>(allocator))
{
}

template<typename T>
typename Vector<T>::size_type Vector<T>::size() const noexcept
{ return m_Values.size(); }

template<typename T>
typename Vector<T>::size_type Vector<T>::capacity() const noexcept
{ return m_Values.capacity(); }

template<typename T>
bool Vector<T>::empty() const noexcept
{ return m_Values.empty(); }

template<typename T>
T* Vector<T>::data() noexcept
{ return m_Values.data(); }

template<typename T>
const T* Vector<T>::data() const noexcept
{ return m_Values.data(); }

template<typename T>
T& Vector<T>::operator[](size_type index) noexcept
{
    assert(index < size());
    return m_Values[index];
}

template<typename T>
const T& Vector<T>::operator[](size_type index) const noexcept
{
    assert(index < size());
    return m_Values[index];
}

template<typename T>
T& Vector<T>::front() noexcept
{ return m_Values.front(); }

template<typename T>
const T& Vector<T>::front() const noexcept
{ return m_Values.front(); }

template<typename T>
T& Vector<T>::back() noexcept
{ return m_Values.back(); }

template<typename T>
const T& Vector<T>::back() const noexcept
{ return m_Values.back(); }

template<typename T>
typename Vector<T>::iterator Vector<T>::begin() noexcept
{ return m_Values.begin(); }

template<typename T>
typename Vector<T>::const_iterator Vector<T>::begin() const noexcept
{ return m_Values.begin(); }

template<typename T>
typename Vector<T>::const_iterator Vector<T>::cbegin() const noexcept
{ return m_Values.cbegin(); }

template<typename T>
typename Vector<T>::iterator Vector<T>::end() noexcept
{ return m_Values.end(); }

template<typename T>
typename Vector<T>::const_iterator Vector<T>::end() const noexcept
{ return m_Values.end(); }

template<typename T>
typename Vector<T>::const_iterator Vector<T>::cend() const noexcept
{ return m_Values.cend(); }

template<typename T>
void Vector<T>::clear() noexcept
{ m_Values.clear(); }

template<typename T>
void Vector<T>::reserve(size_type capacity)
{ m_Values.reserve(capacity); }

template<typename T>
void Vector<T>::resize(size_type count)
{ m_Values.resize(count); }

template<typename T>
void Vector<T>::resize(size_type count, const T& value)
{ m_Values.resize(count, value); }

template<typename T>
void Vector<T>::push_back(const T& value)
{ m_Values.push_back(value); }

template<typename T>
void Vector<T>::push_back(T&& value)
{ m_Values.push_back(std::move(value)); }

template<typename T>
template<typename... Args>
T& Vector<T>::emplace_back(Args&&... args)
{
    return m_Values.emplace_back(std::forward<Args>(args)...);
}

template<typename T>
void Vector<T>::pop_back()
{
    assert(empty() == false);
    m_Values.pop_back();
}

template<typename T>
Allocator* Vector<T>::GetAllocator() const noexcept
{
    return m_Values.get_allocator().GetAllocator();
}

} // namespace Raven
