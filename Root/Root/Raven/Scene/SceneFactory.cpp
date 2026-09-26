#include "Raven/Scene/SceneFactory.h"

namespace Raven
{

bool SceneFactory::Register(const std::string& sceneID, SceneFactoryFunction factory)
{
    if (sceneID.empty() == true || factory == nullptr)
    {
        return false;
    }

    // 同じIDの暗黙上書きは、別Sceneへ遷移する設定ミスを見つけにくくします。
    // 再登録したい場合は明示的にUnregisterしてから登録します。
    if (m_Factories.find(sceneID) != m_Factories.end())
    {
        return false;
    }

    m_Factories.emplace(sceneID, std::move(factory));
    return true;
}

bool SceneFactory::Unregister(const std::string& sceneID)
{
    return m_Factories.erase(sceneID) > 0u;
}

bool SceneFactory::Contains(const std::string& sceneID) const
{
    return m_Factories.find(sceneID) != m_Factories.end();
}

Scope<Scene> SceneFactory::Create(const std::string& sceneID) const
{
    const auto it = m_Factories.find(sceneID);
    if (it == m_Factories.end() || it->second == nullptr)
    {
        return nullptr;
    }

    return it->second();
}

} // namespace Raven
