#pragma once
#include <cassert>
#include <new>
#include <stdexcept>
namespace Raven {
template<typename T> UniquePtr<T>::UniquePtr(std::nullptr_t) noexcept {}
template<typename T> UniquePtr<T>::UniquePtr(T* p, Allocator& a) noexcept : m_Pointer(p), m_Allocator(p != nullptr ? &a : nullptr) {}
template<typename T> UniquePtr<T>::~UniquePtr() noexcept { reset(); }
template<typename T> UniquePtr<T>::UniquePtr(UniquePtr&& o) noexcept : m_Pointer(o.m_Pointer), m_Allocator(o.m_Allocator) { o.m_Pointer = nullptr; o.m_Allocator = nullptr; }
template<typename T> UniquePtr<T>& UniquePtr<T>::operator=(UniquePtr&& o) noexcept { if (this != &o) { reset(); m_Pointer=o.m_Pointer; m_Allocator=o.m_Allocator; o.m_Pointer=nullptr; o.m_Allocator=nullptr; } return *this; }
template<typename T> T* UniquePtr<T>::get() const noexcept { return m_Pointer; }
template<typename T> Allocator* UniquePtr<T>::GetAllocator() const noexcept { return m_Allocator; }
template<typename T> UniquePtr<T>::operator bool() const noexcept { return m_Pointer != nullptr; }
template<typename T> T& UniquePtr<T>::operator*() const noexcept { assert(m_Pointer != nullptr); return *m_Pointer; }
template<typename T> T* UniquePtr<T>::operator->() const noexcept { assert(m_Pointer != nullptr); return m_Pointer; }
template<typename T> void UniquePtr<T>::reset() noexcept {
 T* p=m_Pointer; Allocator* a=m_Allocator; m_Pointer=nullptr; m_Allocator=nullptr;
 if (p != nullptr) { assert(a != nullptr); p->~T(); a->Deallocate(p); }
}
template<typename T> T* UniquePtr<T>::release() noexcept { T* p=m_Pointer; m_Pointer=nullptr; m_Allocator=nullptr; return p; }
template<typename T> void UniquePtr<T>::swap(UniquePtr& o) noexcept { using std::swap; swap(m_Pointer,o.m_Pointer); swap(m_Allocator,o.m_Allocator); }
template<typename T, typename... Args> UniquePtr<T> MakeUnique(Allocator& a, Args&&... args) {
 static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");
 void* memory=a.Allocate(sizeof(T),alignof(T));
 if (memory == nullptr) { throw std::bad_alloc(); }
 try { return UniquePtr<T>(new (memory) T(std::forward<Args>(args)...),a); }
 catch (...) { /* 構築失敗時は未構築の領域だけを返す */ a.Deallocate(memory); throw; }
}
} // namespace Raven
