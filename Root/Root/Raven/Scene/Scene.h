#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <typeindex>
#include <utility>
#include <algorithm>
#include <array>
#include <vector>

//#include "Raven/Core/Event.h"
#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"

#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/ComponentStorage.h"
#include "Raven/Scene/ComponentView.h"

#include "Raven/Physics/PhysicsSimulationWorld.h"

// コンポーネント汎用化
#define USE_STORAGE_VERSION_2 1
#define USE_STORAGE_VERSION_1 1

namespace Raven
{
class RHIDevice;
class SceneGraph;

class Scene
{

public:

    using ComponentStorageMap = std::unordered_map< std::type_index, std::unique_ptr<IComponentStorage>>;

public:
    //Scene() = default;
    Scene();

    // ApplicationはScope<Scene>としてSceneGame等の派生Sceneを所有します。
    // 基底Sceneポインタ経由で破棄した際にも派生デストラクタを確実に呼ぶためvirtualにします。
    virtual ~Scene() = default;

    //+---------------------------------------------------------------------
    // 仮想関数
    //+---------------------------------------------------------------------
    // GLリソース、Entity生成、Texture/Shader読み込み
    virtual void OnCreate();
	// GLリソース解放、Entity破棄、Texture/Shader解放
    virtual void OnDestroy();
    // 入力、物理演算、ゲームロジックなど
    virtual void OnUpdate(float dt);
    // 描画処理
    virtual void OnRender();
    virtual void OnEvent(Event& e);

    //+---------------------------------------------------------------------
    // メンバ関数
    //+---------------------------------------------------------------------
    void PushLayer(Scope<Layer> layer);

    // Scene-owned Layerを型で検索します。返却pointerはScene lifetime中だけ有効です。
    // Application Overlayは毎Frame Active Sceneから再取得し、Scene交換を跨いで保持しないでください。
    template <class T>
    T* FindLayer();

    template <class T>
    const T* FindLayer() const;

    void RenderEntities();

    // Explicit RHI描画のFrame開始前に、Entityが共有するMeshのGPU Bufferを準備します。
    // SceneはBackendを知らず、呼び出し側がScene Contextと同じDeviceを渡します。
    // Backend/Context切替時は新しいDeviceで再実行してください。
    // falseは一部Meshの準備失敗を示し、描画Frameを開始してはいけません。
    bool PrepareRHIMeshes(RHIDevice& device);

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    // HandleはこのScene内の識別子です。Scene所属は呼び出し側で確認します。
    // 循環と破棄済みHandleを拒否して親子関係を更新します。
    bool CanSetEntityParent(EntityHandle child, EntityHandle parent) const;
    bool SetEntityParent(EntityHandle child, EntityHandle parent);
    EntityHandle GetEntityParent(EntityHandle child) const;
    bool DetachEntity(EntityHandle child);
    std::vector<EntityHandle> GetEntityChildren(EntityHandle parent) const;
    std::vector<EntityHandle> GetEntityAncestors(EntityHandle child) const;
    std::vector<EntityHandle> GetEntityDescendants(EntityHandle root) const;

    // Editorが復元できない未知のComponentを持つEntityの削除を防ぎます。
    bool CanSnapshotEntityComponents(EntityHandle handle) const;

    void QueueDestroyEntity(Entity entity);
    void FlushDestroyedEntities();

    // ========================================================================
    // Physics World Access
    // ========================================================================
    // Sceneは上位PhysicsSimulationWorldを所有しますが、既存ゲームコードからの
    // RayCast / AddImpulse / QueryAABBなどは従来どおりRigid Body PhysicsWorld経由で
    // 利用できるよう、GetPhysicsWorld()を互換APIとして維持します。
    const ph::PhysicsWorld& GetPhysicsWorld() const;
    ph::PhysicsWorld& GetPhysicsWorld();

    // General Physics側のDomain Registryや将来のCoupling制御へアクセスする明示APIです。
    // 既存Rigid Body利用側はGetPhysicsWorld()を使い続け、上位Worldが必要なSystemだけが
    // このAccessorを使用することで依存範囲を限定します。
    const ph::PhysicsSimulationWorld& GetPhysicsSimulationWorld() const { return m_PhysicsWorld; }
    ph::PhysicsSimulationWorld& GetPhysicsSimulationWorld() { return m_PhysicsWorld; }

    // 1描画frameが物理catch-upだけで占有されないようにする実行予算です。
    // fixed dtやSolver反復数とは独立し、上限超過時は整数step分の遅れだけを破棄します。
    void SetMaxPhysicsStepsPerFrame(uint32_t count) { m_MaxPhysicsStepsPerFrame = std::max(1u, count); }
    uint32_t GetMaxPhysicsStepsPerFrame() const { return m_MaxPhysicsStepsPerFrame; }

    //+---------------------------------------------------------------------
    // テンプレート
    //+---------------------------------------------------------------------
    // Index APIは現在のSlotの生存だけを検証します。Index単独では世代とScene所属を
    // 復元できないため、保存した参照やScene外の呼び出しにはEntity経由のAPIを使います。
    // StorageはSceneが所有し、Entity/ComponentViewはSceneの寿命を延ばしません。
    template <class T, class... Args>
    T& AddComponent(EntityIndex index, Args&&... args);

    template <class T>
    T& GetComponent(EntityIndex index);

    template <class T>
    const T& GetComponent(EntityIndex index) const;

    template <class T>
    bool HasComponent(EntityIndex index) const;

    template <class T>
    bool RemoveComponent(EntityIndex index);

    template<class T>
    T* TryGetComponent(EntityIndex index);

    template<class T>
    const T* TryGetComponent(EntityIndex index) const;

    template<class... Components>
    ComponentView<Components...> View();

protected:
    std::vector<Scope<Layer>> m_layers;

protected:

    virtual void OnUpdateGame(float dt) {}
    // Game Logic後にSimulation更新を継続するかを派生Sceneが決定します。
    // Pause Menu等はUI入力を処理した後、Animation/Physics/Layerだけを停止できます。
    virtual bool ShouldUpdateSimulation() const { return true; }

private:

    friend class SceneGraph;

    struct EntitySlot
    {
        EntityGeneration Generation = 0;
        bool Alive = false;
    };

    // 各Entity IndexのGenerationと生存状態
    std::vector<EntitySlot> m_EntitySlots;

    // 破棄され、再利用可能なIndex
    std::vector<EntityIndex> m_FreeEntityIndices;

    //std::vector<EntityIndex> m_DestroyQueue;
    //Destroy QueueにIndexだけを保存すると、同じIndexが再利用された後に誤って新しいEntityを破棄する危険があります。
    //そのため、キューにはEntityハンドル全体を保存します。
    std::vector<Entity> m_DestroyQueue;

    // Sceneは個別Physics Domainを直接所有せず、上位Worldを所有します。
    // Phase 0では内部にRigid Body PhysicsWorldだけを保持し、既存挙動をそのまま委譲します。
    ph::PhysicsSimulationWorld m_PhysicsWorld;

    float m_PhysicsAccumulator = 0.0f;
    float m_FixedDeltaTime = 1.0f / 60.0f;
    uint32_t m_MaxPhysicsStepsPerFrame = 4u;

private:

    template<class>
    inline static constexpr bool AlwaysFalse = false;

    template<class T>
    ComponentStorage<T>& GetStorage();

    template<class T>
    const ComponentStorage<T>& GetStorage() const;

    // Storageがなければ生成する
    template<class T>
    ComponentStorage<T>& GetOrCreateStorage();

    //  Storageがなければnullptrを返す
    template<class T>
    ComponentStorage<T>* FindStorage();

    template<class T>
    const ComponentStorage<T>* FindStorage() const;

private:

	void OnUpdatePhysics(float dt);
    void OnUpdateLayer(float dt);
    
public:

    bool IsEntityAlive(EntityIndex index, EntityGeneration generation) const;
    bool IsEntityAlive(EntityHandle handle) const;
    bool IsEntityAlive(Entity entity) const;

    EntityGeneration GetEntityGeneration(EntityIndex index) const;

private:
    EntityIndex m_NextEntityIndex = 1;
    ComponentStorageMap m_ComponentStorages;
};

}

#include "Raven/Scene/Scene.inl"
