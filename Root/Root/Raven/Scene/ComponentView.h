#pragma once

#include <tuple>
#include <type_traits>

#include "Raven/Scene/ComponentStorage.h"
#include "Raven/Scene/Entity.h"

#if 0
//+ ---------------------------------------------------

ComponentView.h内ではSceneを前方宣言しかしていないため、

m_Scene->GetComponent<T>()
m_Scene->HasComponent<T>()

の呼び出しをコンパイラが正しく解釈できない場合があります。

さらに、依存テンプレート名なのでtemplateキーワードも必要になります。

したがって、実際には循環includeを避けるため、Component Viewの定義位置を少し工夫します。
//+ ---------------------------------------------------

次の構成にします。

Scene /
├─ Entity.h
├─ ComponentStorage.h
├─ ComponentView.h
├─ Scene.h
└─ Scene.cpp

ComponentView.hではクラスの宣言だけを行い、Sceneを使う関数本体はScene.hの後半に置きます。
//+ ---------------------------------------------------

#endif

namespace Raven
{

class Scene;

#if 0
前回のComponent Viewは、先頭ComponentのStorage Iteratorを保持していました。

今回はEntity ID一覧を保持するように変更します。
#endif

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
#if 0
template<class FirstComponent, class... OtherComponents>
class ComponentView
{
private:

    using BaseStorage = ComponentStorage<FirstComponent>;
    using BaseIterator = typename BaseStorage::Iterator;

public:

    class Iterator
    {
    public:

        Iterator(Scene* scene, BaseIterator current, BaseIterator end);

        Iterator& operator++();

        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const;

        auto operator*() const;

    private:
        void SkipInvalidEntities();

    private:
        Scene* m_Scene = nullptr;

        BaseIterator m_Current;
        BaseIterator m_End;
    };

public:

    ComponentView(Scene& scene, BaseStorage& baseStorage);

    Iterator begin();
    Iterator end();

private:
    Scene* m_Scene = nullptr;
    BaseStorage* m_BaseStorage = nullptr;
};
#endif


}
