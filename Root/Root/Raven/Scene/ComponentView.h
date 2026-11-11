#pragma once

#include <tuple>
#include <type_traits>
#include <cstdint>
#include <array>

#include "Raven/Scene/ComponentStorage.h"
#include "Raven/Scene/Entity.h"


namespace Raven
{

class Scene;

// Caller所有の任意計測先です。View/Iteratorより長生きさせ、同じThreadでのみ使います。
// Counterは累積値。時刻取得・Profiler・動的確保を走査内へ持ち込まず、時間計測とは別passで使用します。
struct ComponentViewStatistics
{
    uint64_t Candidates = 0;
    uint64_t Matches = 0;
    uint64_t AliveChecks = 0; // 候補についてViewが明示的に呼ぶIsEntityAliveの回数。
    uint64_t StorageLookups = 0;
    uint64_t FilterChecks = 0;
    uint64_t ReadChecks = 0;
    uint64_t ComponentReads = 0;
    uint64_t StructuralChecks = 0;
};

// SceneとStorageへの非所有Viewです。Scene破棄後は利用できません。
// 走査中は対象Storageの追加/削除、Entityの即時破棄、Flushを行わないでください。
// swap-and-popと終端Indexの変化で走査の取りこぼしや範囲外参照が起こり得ます。
// Component値の変更とQueueDestroyEntityは可能です。Flushは走査終了後に行います。
// 構造変更後はView/IteratorとComponent参照を再取得してください。
// Typed Storageのpointeeだけを非所有cacheし、Component配列のpointerは保存しません。
// SceneのStorage mapがrehashしてもunique_ptrのpointeeは移動しません。
// Debugは対象Storageの構造変更/Scene::OnDestroy後の使用を例外で検出します。
// Releaseでも同じ無効化契約を守ってください。Scene自体の破棄後や並列変更は検出対象外です。
template<class... Components>
class ComponentView
{

    static_assert(sizeof...(Components) > 0, "ComponentView requires at least one component type." );

public:
    using StorageTuple = std::tuple<ComponentStorage<Components>*...>;

    class Iterator
    {
    public:

        //Iterator(Scene* scene, const std::vector<EntityID>* entities, std::size_t index);
        Iterator(Scene* scene, const IComponentStorage* baseStorage, std::size_t index,
            ComponentViewStatistics* statistics = nullptr);
        Iterator(const ComponentView& view, std::size_t index);

        Iterator& operator++();

        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const;

        auto operator*() const;

    private:
        void SkipInvalidEntities();
        template<class T>
        bool Matches(ComponentStorage<T>* storage, EntityIndex index) const;
        template<class T>
        T& Read(ComponentStorage<T>* storage, EntityIndex index) const;
        void Validate() const;

    private:

        Scene* m_Scene = nullptr;
        const IComponentStorage* m_BaseStorage = nullptr;
        std::size_t m_Index = 0;
        ComponentViewStatistics* m_Statistics = nullptr;
        StorageTuple m_Storages{};
#ifndef NDEBUG
        std::array<uint64_t, sizeof...(Components)> m_Versions{};
        uint64_t m_LifetimeVersion = 0;
#endif
        //const std::vector<EntityID>* m_Entities = nullptr;

    };

public:

    //ComponentView(Scene& scene, std::vector<EntityID> entities);
    ComponentView(Scene& scene, const IComponentStorage* baseStorage, ComponentViewStatistics* statistics = nullptr);

    Iterator begin();
    Iterator begin() const;

    Iterator end();
    Iterator end() const;

    std::size_t CandidateCount() const;

    bool Empty() const;
    std::size_t Size() const;

private:
    friend class Scene;
    // 外部から別SceneのStorage tupleを渡させず、所有Sceneが解決したpointeeだけを保持します。
    ComponentView(Scene& scene, const IComponentStorage* baseStorage, StorageTuple storages,
        ComponentViewStatistics* statistics);
    void Validate() const;
#ifndef NDEBUG
    static void ValidateState(const Scene* scene, const StorageTuple& storages,
        const std::array<uint64_t, sizeof...(Components)>& versions, uint64_t lifetimeVersion,
        ComponentViewStatistics* statistics);
#endif
    Scene* m_Scene = nullptr;
    //std::vector<EntityID> m_Entities;
	const IComponentStorage* m_BaseStorage = nullptr;   
    ComponentViewStatistics* m_Statistics = nullptr;
    StorageTuple m_Storages{};
#ifndef NDEBUG
    std::array<uint64_t, sizeof...(Components)> m_Versions{};
    uint64_t m_LifetimeVersion = 0;
#endif
};

// 旧versionのComponentViewは、先頭ComponentのStorage Iteratorを保持していました。


}
