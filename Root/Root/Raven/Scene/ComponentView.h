#pragma once

#include <tuple>
#include <type_traits>

#include "Raven/Scene/ComponentStorage.h"
#include "Raven/Scene/Entity.h"


namespace Raven
{

class Scene;


// SceneとStorageへの非所有Viewです。Scene破棄後は利用できません。
// 走査中は対象Storageの追加/削除、Entityの即時破棄、Flushを行わないでください。
// swap-and-popと終端Indexの変化で走査の取りこぼしや範囲外参照が起こり得ます。
// Component値の変更とQueueDestroyEntityは可能です。Flushは走査終了後に行います。
// 構造変更後はView/IteratorとComponent参照を再取得してください。
template<class... Components>
class ComponentView
{

    static_assert(sizeof...(Components) > 0, "ComponentView requires at least one component type." );

public:

    class Iterator
    {
    public:

        //Iterator(Scene* scene, const std::vector<EntityID>* entities, std::size_t index);
        Iterator(Scene* scene, const IComponentStorage* baseStorage, std::size_t index);

        Iterator& operator++();

        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const;

        auto operator*() const;

    private:
        void SkipInvalidEntities();

    private:

        Scene* m_Scene = nullptr;
        const IComponentStorage* m_BaseStorage = nullptr;
        std::size_t m_Index = 0;
        //const std::vector<EntityID>* m_Entities = nullptr;

    };

public:

    //ComponentView(Scene& scene, std::vector<EntityID> entities);
    ComponentView(Scene& scene, const IComponentStorage* baseStorage);

    Iterator begin();
    Iterator begin() const;

    Iterator end();
    Iterator end() const;

    std::size_t CandidateCount() const;

    bool Empty() const;
    std::size_t Size() const;

private:
    Scene* m_Scene = nullptr;
    //std::vector<EntityID> m_Entities;
	const IComponentStorage* m_BaseStorage = nullptr;   
};

// 旧versionのComponentViewは、先頭ComponentのStorage Iteratorを保持していました。


}
