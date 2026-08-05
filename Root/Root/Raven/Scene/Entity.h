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

//ビット配置
//
//例えば、Index = 42 Generation = 3の場合、
//64bit値
//00000000 00000000 00000000 00000011
//00000000 00000000 00000000 00101010
//になります。
//
//コードでは、
//Generation << 32
//で上位へ移動し、
//
// | Index
//で下位32bitへIndexを入れています。

struct EntityHandle
{

    EntityIndex m_Index = InvalidEntityIndex;
    EntityGeneration m_Generation = 0;

   /* IndexとGenerationは、それぞれ32bitです。
    上位32bit : Generation
    下位32bit : Index
    として1つのuint64_tへパックできます。*/
    constexpr uint64_t Value() const
    {
        return (static_cast<uint64_t>(m_Generation) << 32) | static_cast<uint64_t>(m_Index);
    }

    static constexpr EntityHandle FromValue(uint64_t value)
    {
        return EntityHandle{static_cast<EntityIndex>(value & 0xFFFFFFFFull), static_cast<EntityGeneration>(value >> 32)};
    }

    constexpr bool IsValid() const
    {
        return m_Index != InvalidEntityIndex;
    }

    constexpr explicit operator bool() const
    {
        return IsValid();
    }

    constexpr bool operator==(const EntityHandle& other) const
    {
        return m_Index == other.m_Index && m_Generation == other.m_Generation;
    }

    constexpr bool operator!=(const EntityHandle& other) const
    {
        return !(*this == other);
    }

    constexpr bool operator<(const EntityHandle& other) const
    {
        return Value() < other.Value();
    }

};

inline constexpr EntityHandle InvalidEntityHandle{InvalidEntityIndex, 0 };

class Entity
{

public:
    Entity() = default;
    //Entity(EntityID id, Scene* scene)
    //   : m_ID(id), m_Scene(scene) {}
    //

    Entity(EntityIndex index, EntityGeneration generation, Scene* scene);
    Entity(EntityHandle handle, Scene* scene);

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

    EntityHandle GetHandle() const;

    uint64_t GetValue() const;

    Scene* GetScene() const noexcept;

#if 0
    explicit operator bool() const
    {
        return m_Index != 0 && m_Generation  != 0 && m_Scene != nullptr;
    }
#endif

    explicit operator bool() const;

    bool operator==(const Entity& other) const
    {
        return ((m_Handle == other.m_Handle) && (m_Scene == other.m_Scene));
    }

    bool operator!=(const Entity& other) const
    {
        return !(*this == other);
    }

public:

private:
    EntityHandle m_Handle = InvalidEntityHandle;
    Scene* m_Scene = nullptr;
};

// std::unordered_set<Entity, EntityHasher>
// 1つのScene内だけで使う
// EntityHandleをキーにする
//
// 複数Sceneをまたいで使う
// Entityをキーにする
// Handle + Scene * をハッシュする

// ※ operator==で比較する情報は、必ずハッシュにも反映すると覚えると安全です。

// Scene内のECS処理にはEntityHandleをキーとして使い、Editorなど複数Sceneをまたぐ管理ではEntity + EntityHasherを使う分け方が扱いやすいです。
struct EntityHasher
{
    std::size_t operator()(const Entity& entity) const noexcept
    {
        std::size_t seed = std::hash<uint64_t>{}(entity.GetHandle().Value());

        const std::size_t sceneHash = std::hash<const Scene*>{}( entity.GetScene());

        constexpr std::size_t Magic = static_cast<std::size_t>(0x9e3779b97f4a7c15ull);

        seed ^= sceneHash + Magic + (seed << 6) + (seed >> 2);

        return seed;
    }
};

}
