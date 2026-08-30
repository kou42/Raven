#include "Raven/Scene/Scene.h"

namespace Raven
{

Entity Scene::TryGetEntity(EntityIndex index)
{
    // Picking AttachmentにはEntityIndexだけを保存します。
    // Index 0はInvalidEntityIndexとして予約されているため選択なしとして扱います。
    if (index == InvalidEntityIndex)
    {
        return Entity{};
    }

    if (static_cast<std::size_t>(index) >= m_EntitySlots.size())
    {
        return Entity{};
    }

    const EntitySlot& slot = m_EntitySlots[index];
    if (slot.Alive == false)
    {
        return Entity{};
    }

    // 現在のGenerationをScene側の正規データから復元します。
    // Destroy後に同じIndexが再利用された場合でも、その時点で生存しているGenerationだけを
    // Entityへ組み込むため、Editorが古いGenerationを保持することを防げます。
    return Entity(EntityHandle{ index, slot.Generation }, this);
}

} // namespace Raven
