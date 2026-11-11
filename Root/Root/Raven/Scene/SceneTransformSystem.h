#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Raven/Scene/Components.h"

namespace Raven
{
class Scene;
class SceneGraph;

// Scene所有の派生Worldキャッシュ。Scene/Componentへのpointerや参照は保持しません。
class SceneTransformSystem final
{
public:
    bool Evaluate(Scene& scene);
    bool TryGetWorld(Scene& scene, EntityHandle handle, math::Mat4& world);
    // Evaluate直後の読み取り専用描画バッチ向け。途中でTransformを変更しないこと。
    bool TryGetEvaluated(const Scene& scene, EntityHandle handle, math::Mat4& world) const;
    bool EnableLocal(Scene& scene, EntityHandle handle, ReparentTransformMode mode);
    bool DisableLocal(Scene& scene, EntityHandle handle);
    bool SetLocal(Scene& scene, EntityHandle handle, const TransformComponent& value);
    bool SetWorld(Scene& scene, EntityHandle handle, const TransformComponent& value);
    bool CanResolve(const Scene& scene, EntityHandle handle) const;
    void Forget(EntityHandle handle);
    void Clear();
    uint64_t GetRecomputationCount() const;

private:
    friend class SceneGraph;
    struct Resolved
    {
        EntityHandle Handle;
        EntityHandle Parent;
        bool IsLocal = false;
        bool Recomputed = false;
        bool Authored = false;
        bool PhysicsScaleValidated = false;
        TransformComponent Local;
        TransformComponent World;
        math::Mat4 ParentMatrix = math::Mat4::Identity();
        math::Mat4 Matrix = math::Mat4::Identity();
    };
    struct Override
    {
        EntityHandle Handle;
        EntityHandle Parent;
        bool IsLocal = false;
        TransformComponent Value;
    };
    using Prepared = std::unordered_map<uint64_t, Resolved>;
    bool Resolve(const Scene& scene, EntityHandle handle, Prepared& result, const Override* change = nullptr) const;
    bool ValidateBranch(const Scene& scene, const Override& change, Prepared& result) const;
    bool PrepareReparent(const Scene& scene, EntityHandle child, EntityHandle parent,
        ReparentTransformMode mode, Prepared& result, const TransformComponent* restoredLocal = nullptr) const;
    bool GetParentWorld(const Scene& scene, EntityHandle parent, math::Mat4& world) const;
    void Commit(Scene& scene, const Prepared& result);
    void PrepareChildDestruction(Scene& scene, EntityHandle child);
    std::unordered_map<uint64_t, Resolved> m_Worlds;
    std::unordered_set<uint64_t> m_InvalidWorlds;
    uint64_t m_RecomputationCount = 0;
};
}
