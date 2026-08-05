#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

// Entity = ID
// Component = データ
// System = 処理

// using EntityID = uint32_t
// でエンティティを定義し、
// TransformComponent, MeshRendererComponent, TagComponent などのコンポーネントを定義する。
// RigitBodyComponent, ColliderComponent, LightComponent CameraCompomentなどのコンポーネントも追加可能。

//Entityを破棄して同じIndexを再利用すると、Generationを増やします。
//
//古いEntity : Index 5, Generation 2
//新しいEntity : Index 5, Generation 3
//
//このため、古いEntityは新しいEntityと一致しません。

namespace Raven
{

class Scene;

// 現在はEntityIndexに変更されています
using EntityID = uint32_t;

using EntityIndex = uint32_t;
using EntityGeneration = uint32_t;

inline constexpr EntityIndex InvalidEntityIndex = 0;

class Entity
{
public:
    Entity() = default;
    //Entity(EntityID id, Scene* scene)
    //   : m_ID(id), m_Scene(scene) {}
    //

    Entity(EntityIndex index, EntityGeneration generation, Scene* scene);

    template <class T, class... Args>
    T& AddComponent(Args&&... args);

    template <class T>
    T& GetComponent();

    template <class T>
    const T& GetComponent() const;

    template <class T>
    bool HasComponent() const;

    template <class T>
    bool RemoveComponent();

    EntityIndex GetIndex() const;

    EntityGeneration GetGeneration() const;

#if 0
    explicit operator bool() const
    {
        return m_Index != 0 && m_Generation  != 0 && m_Scene != nullptr;
    }
#endif

    explicit operator bool() const;

    bool operator==(const Entity& other) const
    {
        return m_Index == other.m_Index && m_Generation == other.m_Generation && m_Scene == other.m_Scene;
    }

    bool operator!=(const Entity& other) const
    {
        return !(*this == other);
    }

public:

private:
    EntityIndex m_Index = InvalidEntityIndex;
    EntityGeneration m_Generation = 0;
    Scene* m_Scene = nullptr;
};

inline Entity::operator bool() const
{
    if (m_Scene == nullptr) {
        return false;
    }

    return m_Scene->IsEntityAlive(m_Index, m_Generation);

}

}
