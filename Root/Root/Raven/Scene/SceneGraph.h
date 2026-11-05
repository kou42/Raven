#pragma once

#include <vector>
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Components.h"

namespace Raven
{
class Scene;

// SceneのRelationshipComponentを正規データとする、状態を持たない階層操作です。
// Scene/Entityを所有せず、保持すべき子リンクや破棄通知を二重管理しません。
// Handleは指定Scene内だけで有効です。Scene所属は呼び出し側で保証します。
class SceneGraph final
{
public:
    static EntityHandle GetParent(const Scene& scene, EntityHandle child);
    static bool CanReparent(const Scene& scene, EntityHandle child, EntityHandle parent,
        ReparentTransformMode mode = ReparentTransformMode::KeepWorld);
    static bool Reparent(Scene& scene, EntityHandle child, EntityHandle parent,
        ReparentTransformMode mode = ReparentTransformMode::KeepWorld);
    static bool CanReparentWithLocal(const Scene& scene, EntityHandle child, EntityHandle parent, const TransformComponent& local);
    static bool ReparentWithLocal(Scene& scene, EntityHandle child, EntityHandle parent, const TransformComponent& local);
    static bool Detach(Scene& scene, EntityHandle child);

    // 返却値はHandleのSnapshotです。利用時に生存確認し、構造変更後は再取得します。
    // Children/Descendantsにroot自身は含めません。兄弟の順序は保証しません。
    static std::vector<EntityHandle> GetChildren(const Scene& scene, EntityHandle parent);
    // 近い親からRoot順。破損した循環を検出した場合は空を返します。
    static std::vector<EntityHandle> GetAncestors(const Scene& scene, EntityHandle child);
    // 非再帰の親→子順。一時的な子リンクを構築し、循環/重複は一度だけ訪問します。
    static std::vector<EntityHandle> GetDescendants(const Scene& scene, EntityHandle root);

    // Entity破棄前に直下の子だけをRootへ戻します。孫の親子関係は維持します。
    // Legacy Worldは変更せず、Local有効な子は最後のWorldをRootのLocalへ引き継ぎます。
    static void DetachChildren(Scene& scene, EntityHandle parent);

private:
    static bool CanLink(const Scene& scene, EntityHandle child, EntityHandle parent);
    static EntityHandle GetAliveHandle(const Scene& scene, EntityIndex index);
};
}
