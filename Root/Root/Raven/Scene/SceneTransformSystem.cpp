#include "Raven/Scene/SceneTransformSystem.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Math/TransformMath.h"
#include "Raven/Physics/RigidBodyDynamics.h"

#include <unordered_set>
#include <cmath>

namespace Raven
{
namespace
{
bool ReadParent(const Scene& scene, EntityHandle child, EntityHandle& parent)
{
    const auto* relation = scene.TryGetComponent<RelationshipComponent>(child.m_Index);
    parent = relation != nullptr ? relation->Parent : InvalidEntityHandle;
    return parent.IsValid() == false || scene.IsEntityAlive(parent) == true;
}
bool Same(const TransformComponent& a, const TransformComponent& b)
{
    return a.Position == b.Position && a.Rotation == b.Rotation && a.Scale == b.Scale;
}
bool SameMatrix(const math::Mat4& a, const math::Mat4& b)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if (a.m[row][column] != b.m[row][column])
            {
                return false;
            }
        }
    }
    return true;
}
bool PreservesWorld(const math::Mat4& actual, const math::Mat4& expected)
{
    // 逆行列が有限でも、巨大な親座標との加減算で小さなWorld位置が失われることがあります。
    // 操作の要求値と再構成結果を照合し、keep-worldを満たさない候補を確定しません。
    constexpr float WorldPreservationTolerance = 0.0001f;
    if (math::IsFiniteAffine(actual) == false || math::IsFiniteAffine(expected) == false)
    {
        return false;
    }
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const float tolerance = WorldPreservationTolerance * (1.0f + std::fabs(expected.m[row][column]));
            if (std::fabs(actual.m[row][column] - expected.m[row][column]) > tolerance)
            {
                return false;
            }
        }
    }
    return true;
}
bool PreservesCameraRotation(const Scene& scene, EntityHandle handle,
    const TransformComponent& actual, const TransformComponent& expected)
{
    if (scene.HasComponent<CameraComponent>(handle.m_Index) == false
        && scene.HasComponent<RigidBodyComponent>(handle.m_Index) == false)
    {
        return true;
    }
    // CameraとRigidBody姿勢はScaleを除いて使います。符号正規化で回転が変わる候補は拒否します。
    TransformComponent actualRotation;
    TransformComponent expectedRotation;
    actualRotation.Rotation = actual.Rotation;
    expectedRotation.Rotation = expected.Rotation;
    return PreservesWorld(actualRotation.GetTransform(), expectedRotation.GetTransform());
}
bool Decompose(const math::Mat4& matrix, TransformComponent& value)
{
    return math::TryDecomposeTRS(matrix, value.Position, value.Rotation, value.Scale);
}
bool PositiveUniformScale(const math::Mat4& matrix)
{
    TransformComponent pose;
    if (Decompose(matrix, pose) == false)
    {
        return false;
    }
    constexpr float ScaleTolerance = 0.0001f;
    return pose.Scale.x > 0.0f && pose.Scale.y > 0.0f && pose.Scale.z > 0.0f
        && std::fabs(pose.Scale.x - pose.Scale.y) <= ScaleTolerance * pose.Scale.x
        && std::fabs(pose.Scale.x - pose.Scale.z) <= ScaleTolerance * pose.Scale.x;
}
}

bool SceneTransformSystem::CanResolve(const Scene& scene, EntityHandle handle) const
{
    Prepared result;
    return Resolve(scene, handle, result);
}

bool SceneTransformSystem::Resolve(const Scene& scene, EntityHandle handle,
    Prepared& result, const Override* change) const
{
    std::vector<EntityHandle> chain;
    std::unordered_set<uint64_t> visited;
    EntityHandle current = handle;
    while (result.find(current.Value()) == result.end())
    {
        if (scene.IsEntityAlive(current) == false || visited.insert(current.Value()).second == false)
        {
            return false;
        }
        const auto* world = scene.TryGetComponent<TransformComponent>(current.m_Index);
        if (world == nullptr)
        {
            return false;
        }
        const bool overridden = change != nullptr && change->Handle == current;
        const bool local = overridden == true ? change->IsLocal : scene.HasComponent<LocalTransformComponent>(current.m_Index);
        if (local == false)
        {
            Resolved root;
            root.Handle = current;
            root.World = overridden == true ? change->Value : *world;
            root.Matrix = root.World.GetTransform();
            if (math::IsFiniteAffine(root.Matrix) == false)
            {
                return false;
            }
            result.emplace(current.Value(), root);
            break;
        }
        if (scene.CanUseLocalTransform(current.m_Index) == false)
        {
            return false;
        }
        chain.push_back(current);
        const auto* relation = scene.TryGetComponent<RelationshipComponent>(current.m_Index);
        const EntityHandle parent = overridden == true ? change->Parent
            : (relation != nullptr ? relation->Parent : InvalidEntityHandle);
        if (parent.IsValid() == false)
        {
            break;
        }
        current = parent;
    }
    // 親を先に解決し、再帰stackを使わず深い階層を処理します。
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const EntityHandle node = *it;
        const bool overridden = change != nullptr && change->Handle == node;
        const auto* relation = scene.TryGetComponent<RelationshipComponent>(node.m_Index);
        Resolved value;
        value.Handle = node;
        value.IsLocal = true;
        value.Authored = overridden;
        value.Parent = overridden == true ? change->Parent
            : (relation != nullptr ? relation->Parent : InvalidEntityHandle);
        if (value.Parent.IsValid() == true)
        {
            const auto parent = result.find(value.Parent.Value());
            if (parent == result.end())
            {
                return false;
            }
            value.ParentMatrix = parent->second.Matrix;
        }
        value.Local = overridden == true ? change->Value
            : scene.GetComponent<LocalTransformComponent>(node.m_Index).Value;
        const auto cached = m_Worlds.find(node.Value());
        const auto& worldInput = scene.GetComponent<TransformComponent>(node.m_Index);
        const auto* body = scene.TryGetComponent<RigidBodyComponent>(node.m_Index);
        const bool physicsOwned = body != nullptr && body->Type == BodyType::Dynamic;
        // Dynamicは親が動いてもWorld姿勢を維持します。LocalはPhysics出力と現在の親から導く派生値です。
        // Local直接編集との競合は推測せず拒否し、Teleport/明示SetLocal/SetWorldで意図を指定させます。
        if (physicsOwned == true && overridden == false && cached != m_Worlds.end()
            && Same(value.Local, cached->second.Local) == false)
        {
            return false;
        }
        const bool preservePhysicsWorld = physicsOwned == true && overridden == false && cached != m_Worlds.end()
            && SameMatrix(value.ParentMatrix, cached->second.ParentMatrix) == false;
        const bool worldEdited = overridden == false && cached != m_Worlds.end()
            && (Same(worldInput, cached->second.World) == false || preservePhysicsWorld == true);
        if (worldEdited == true)
        {
            // 既存Inspector/Gizmo等はWorld TRSへ直接書き込みます。その編集をLocalへ戻します。
            // LocalとWorldを同時に直接変更すると意図を判別できないため、明示APIでの修復を要求します。
            if (Same(value.Local, cached->second.Local) == false)
            {
                return false;
            }
            math::Mat4 inverse;
            if (math::TryInverseAffine(value.ParentMatrix, inverse) == false
                || Decompose(inverse * worldInput.GetTransform(), value.Local) == false)
            {
                return false;
            }
        }
        if (cached != m_Worlds.end() && Same(value.Local, cached->second.Local) == true
            && value.Parent == cached->second.Parent && SameMatrix(value.ParentMatrix, cached->second.ParentMatrix) == true)
        {
            value.World = cached->second.World;
            value.Matrix = cached->second.Matrix;
        }
        else
        {
            value.Matrix = value.ParentMatrix * value.Local.GetTransform();
            if (Decompose(value.Matrix, value.World) == false)
            {
                return false;
            }
            value.Recomputed = true;
        }
        if (worldEdited == true && (PreservesWorld(value.Matrix, worldInput.GetTransform()) == false
            || PreservesCameraRotation(scene, node, value.World, worldInput) == false))
        {
            return false;
        }
        // Quaternion積分中に任意のWorld回転が発生してもWorld→LocalにShearを作らない範囲に限定します。
        // 反射/非一様Scaleの剛体対応はColliderや慣性の契約変更を伴うため、暗黙に近似しません。
        if (body != nullptr)
        {
            // 行列が同じなら前回のScale検証結果を再利用します。Body追加直後は必ず検証します。
            const bool validated = cached != m_Worlds.end() && cached->second.PhysicsScaleValidated == true
                && SameMatrix(value.ParentMatrix, cached->second.ParentMatrix) == true
                && SameMatrix(value.Matrix, cached->second.Matrix) == true;
            if (validated == false && (PositiveUniformScale(value.ParentMatrix) == false
                || PositiveUniformScale(value.Matrix) == false))
            {
                return false;
            }
            value.PhysicsScaleValidated = true;
        }
        if (physicsOwned == true && worldEdited == true)
        {
            // Localの再構成誤差をPhysics正規値へ繰り返し戻すと、静止中にも位置が漂流します。
            // 上の再構成検証を通ったLocalを保持し、World窓口にはSolverの値をそのまま残します。
            value.World = worldInput;
            value.Matrix = worldInput.GetTransform();
        }
        result.insert_or_assign(node.Value(), value);
    }
    return result.find(handle.Value()) != result.end();
}

bool SceneTransformSystem::GetParentWorld(const Scene& scene, EntityHandle parent, math::Mat4& world) const
{
    if (parent.IsValid() == false)
    {
        world = math::Mat4::Identity();
        return true;
    }
    Prepared resolved;
    if (Resolve(scene, parent, resolved) == false)
    {
        return false;
    }
    world = resolved.at(parent.Value()).Matrix;
    return true;
}

bool SceneTransformSystem::ValidateBranch(const Scene& scene, const Override& change, Prepared& result) const
{
    if (Resolve(scene, change.Handle, result, &change) == false)
    {
        return false;
    }
    // 親の変更で子孫にShearが生まれる場合も、入力を変更する前に拒否します。
    for (EntityHandle child : scene.GetEntityDescendants(change.Handle))
    {
        if (Resolve(scene, child, result, &change) == false)
        {
            return false;
        }
    }
    return true;
}

void SceneTransformSystem::Commit(Scene& scene, const Prepared& result)
{
    for (const auto& entry : result)
    {
        const auto& value = entry.second;
        if (value.IsLocal == true)
        {
            scene.GetComponent<LocalTransformComponent>(value.Handle.m_Index).Value = value.Local;
            scene.GetComponent<TransformComponent>(value.Handle.m_Index) = value.World;
            auto* body = scene.TryGetComponent<RigidBodyComponent>(value.Handle.m_Index);
            if (body != nullptr && (body->Type != BodyType::Dynamic || value.Authored == true))
            {
                // Kinematicの親回転と明示TeleportはQuaternionの初回値だけでなく現在値へ反映します。
                // Dynamicの通常評価ではSolverのQuaternionを再生成せず正規姿勢を保ちます。
                body->Orientation = ph::PhysicsOrientationFromEuler(value.World.Rotation);
                body->OrientationInitialized = true;
            }
            m_Worlds.insert_or_assign(entry.first, value);
            if (value.Recomputed == true)
            {
                ++m_RecomputationCount;
            }
        }
        else
        {
            m_Worlds.erase(entry.first);
        }
        m_InvalidWorlds.erase(entry.first);
    }
}

bool SceneTransformSystem::TryGetWorld(Scene& scene, EntityHandle handle, math::Mat4& world)
{
    if (scene.IsEntityAlive(handle) == true && scene.HasComponent<LocalTransformComponent>(handle.m_Index) == false)
    {
        const auto* value = scene.TryGetComponent<TransformComponent>(handle.m_Index);
        if (value == nullptr)
        {
            return false;
        }
        world = value->GetTransform();
        m_Worlds.erase(handle.Value());
        m_InvalidWorlds.erase(handle.Value());
        return math::IsFiniteAffine(world);
    }
    Prepared result;
    if (Resolve(scene, handle, result) == false)
    {
        if (scene.IsEntityAlive(handle) == true)
        {
            m_InvalidWorlds.insert(handle.Value());
        }
        return false;
    }
    world = result.at(handle.Value()).Matrix;
    Commit(scene, result);
    return true;
}

bool SceneTransformSystem::TryGetEvaluated(const Scene& scene, EntityHandle handle, math::Mat4& world) const
{
    if (scene.IsEntityAlive(handle) == false || m_InvalidWorlds.count(handle.Value()) != 0)
    {
        return false;
    }
    if (scene.HasComponent<LocalTransformComponent>(handle.m_Index) == false)
    {
        const auto* value = scene.TryGetComponent<TransformComponent>(handle.m_Index);
        if (value == nullptr)
        {
            return false;
        }
        world = value->GetTransform();
        return math::IsFiniteAffine(world);
    }
    const auto cached = m_Worlds.find(handle.Value());
    if (cached == m_Worlds.end())
    {
        return false;
    }
    world = cached->second.Matrix;
    return true;
}

bool SceneTransformSystem::Evaluate(Scene& scene)
{
    const auto* storage = scene.FindStorage<LocalTransformComponent>();
    if (storage == nullptr)
    {
        return true;
    }
    Prepared result;
    bool valid = true;
    for (std::size_t i = 0; i < storage->Size(); ++i)
    {
        const EntityIndex index = storage->GetEntityByIndex(i);
        const EntityHandle handle{index, scene.GetEntityGeneration(index)};
        if (Resolve(scene, handle, result) == false)
        {
            m_InvalidWorlds.insert(handle.Value());
            valid = false;
        }
    }
    Commit(scene, result);
    return valid;
}

bool SceneTransformSystem::EnableLocal(Scene& scene, EntityHandle handle, ReparentTransformMode mode)
{
    if (scene.IsEntityAlive(handle) == false || scene.CanUseLocalTransform(handle.m_Index) == false)
    {
        return false;
    }
    if (scene.HasComponent<LocalTransformComponent>(handle.m_Index) == true)
    {
        math::Mat4 ignored;
        return TryGetWorld(scene, handle, ignored);
    }
    const auto* value = scene.TryGetComponent<TransformComponent>(handle.m_Index);
    if (value == nullptr)
    {
        return false;
    }
    EntityHandle parentHandle;
    if (ReadParent(scene, handle, parentHandle) == false)
    {
        return false;
    }
    Override change{handle, parentHandle, true, *value};
    if (mode == ReparentTransformMode::KeepWorld)
    {
        math::Mat4 parent;
        math::Mat4 inverse;
        if (GetParentWorld(scene, change.Parent, parent) == false || math::TryInverseAffine(parent, inverse) == false
            || Decompose(inverse * value->GetTransform(), change.Value) == false)
        {
            return false;
        }
    }
    Prepared result;
    if (ValidateBranch(scene, change, result) == false)
    {
        return false;
    }
    if (mode == ReparentTransformMode::KeepWorld && (PreservesWorld(result.at(handle.Value()).Matrix, value->GetTransform()) == false
        || PreservesCameraRotation(scene, handle, result.at(handle.Value()).World, *value) == false))
    {
        return false;
    }
    scene.AddComponent<LocalTransformComponent>(handle.m_Index, LocalTransformComponent{change.Value});
    Commit(scene, result);
    return true;
}

bool SceneTransformSystem::DisableLocal(Scene& scene, EntityHandle handle)
{
    if (scene.IsEntityAlive(handle) == false || scene.HasComponent<LocalTransformComponent>(handle.m_Index) == false)
    {
        return false;
    }
    Prepared current;
    if (Resolve(scene, handle, current) == false)
    {
        return false;
    }
    Override change{handle, scene.GetEntityParent(handle), false, current.at(handle.Value()).World};
    Prepared result;
    if (ValidateBranch(scene, change, result) == false)
    {
        return false;
    }
    if (PreservesWorld(result.at(handle.Value()).Matrix, current.at(handle.Value()).Matrix) == false)
    {
        return false;
    }
    scene.FindStorage<LocalTransformComponent>()->Remove(handle.m_Index);
    scene.GetComponent<TransformComponent>(handle.m_Index) = change.Value;
    Commit(scene, result);
    return true;
}

bool SceneTransformSystem::SetLocal(Scene& scene, EntityHandle handle, const TransformComponent& value)
{
    if (scene.IsEntityAlive(handle) == false || scene.HasComponent<LocalTransformComponent>(handle.m_Index) == false)
    {
        return false;
    }
    EntityHandle parent;
    if (ReadParent(scene, handle, parent) == false)
    {
        return false;
    }
    Prepared result;
    if (ValidateBranch(scene, Override{handle, parent, true, value}, result) == false)
    {
        return false;
    }
    Commit(scene, result);
    return true;
}

bool SceneTransformSystem::SetWorld(Scene& scene, EntityHandle handle, const TransformComponent& value)
{
    if (scene.IsEntityAlive(handle) == false || scene.HasComponent<TransformComponent>(handle.m_Index) == false)
    {
        return false;
    }
    EntityHandle parent;
    if (ReadParent(scene, handle, parent) == false)
    {
        return false;
    }
    Override change{handle, parent, scene.HasComponent<LocalTransformComponent>(handle.m_Index), value};
    if (change.IsLocal == true)
    {
        math::Mat4 parent;
        math::Mat4 inverse;
        if (GetParentWorld(scene, change.Parent, parent) == false || math::TryInverseAffine(parent, inverse) == false
            || Decompose(inverse * value.GetTransform(), change.Value) == false)
        {
            return false;
        }
    }
    Prepared result;
    if (ValidateBranch(scene, change, result) == false)
    {
        return false;
    }
    if (PreservesWorld(result.at(handle.Value()).Matrix, value.GetTransform()) == false
        || PreservesCameraRotation(scene, handle, result.at(handle.Value()).World, value) == false)
    {
        return false;
    }
    if (change.IsLocal == false)
    {
        scene.GetComponent<TransformComponent>(handle.m_Index) = value;
    }
    Commit(scene, result);
    return true;
}

bool SceneTransformSystem::PrepareReparent(const Scene& scene, EntityHandle child, EntityHandle parent,
    ReparentTransformMode mode, Prepared& result, const TransformComponent* restoredLocal) const
{
    if (scene.HasComponent<LocalTransformComponent>(child.m_Index) == false)
    {
        return restoredLocal == nullptr;
    }
    TransformComponent local;
    math::Mat4 expectedWorld;
    TransformComponent expectedPose;
    if (restoredLocal != nullptr)
    {
        local = *restoredLocal;
    }
    else
    {
        Prepared current;
        if (Resolve(scene, child, current) == false)
        {
            return false;
        }
        local = current.at(child.Value()).Local;
        expectedWorld = current.at(child.Value()).Matrix;
        expectedPose = current.at(child.Value()).World;
        if (mode == ReparentTransformMode::KeepWorld)
        {
            math::Mat4 parentWorld;
            math::Mat4 inverse;
            if (GetParentWorld(scene, parent, parentWorld) == false || math::TryInverseAffine(parentWorld, inverse) == false
                || Decompose(inverse * current.at(child.Value()).Matrix, local) == false)
            {
                return false;
            }
        }
    }
    if (ValidateBranch(scene, Override{child, parent, true, local}, result) == false)
    {
        return false;
    }
    return restoredLocal != nullptr || mode == ReparentTransformMode::KeepLocal
        || (PreservesWorld(result.at(child.Value()).Matrix, expectedWorld) == true
            && PreservesCameraRotation(scene, child, result.at(child.Value()).World, expectedPose) == true);
}

void SceneTransformSystem::PrepareChildDestruction(Scene& scene, EntityHandle child)
{
    auto* local = scene.TryGetComponent<LocalTransformComponent>(child.m_Index);
    if (local == nullptr)
    {
        return;
    }
    math::Mat4 world;
    if (TryGetWorld(scene, child, world) == true)
    {
        local->Value = scene.GetComponent<TransformComponent>(child.m_Index);
    }
    else
    {
        // 壊れた直接編集があっても親破棄を妨げず、最後に評価できたWorldをRootのLocalにします。
        const auto cached = m_Worlds.find(child.Value());
        local->Value = cached != m_Worlds.end() ? cached->second.World : scene.GetComponent<TransformComponent>(child.m_Index);
        scene.GetComponent<TransformComponent>(child.m_Index) = local->Value;
    }
    Forget(child);
}

void SceneTransformSystem::Forget(EntityHandle handle)
{
    m_Worlds.erase(handle.Value());
    m_InvalidWorlds.erase(handle.Value());
}
void SceneTransformSystem::Clear()
{
    m_Worlds.clear();
    m_InvalidWorlds.clear();
    m_RecomputationCount = 0;
}
uint64_t SceneTransformSystem::GetRecomputationCount() const
{
    return m_RecomputationCount;
}
}
