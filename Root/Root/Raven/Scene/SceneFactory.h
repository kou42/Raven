#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Scene/Scene.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace Raven
{

using SceneFactoryFunction = std::function<Scope<Scene>()>;

// Scene IDと生成関数の対応だけを保持する軽量Registryです。
// Scene lifetimeやTransition状態は持たず、SceneManager / SceneTransitionControllerと責務を分離します。
class SceneFactory
{
public:
    bool Register(const std::string& sceneID, SceneFactoryFunction factory);
    bool Unregister(const std::string& sceneID);
    bool Contains(const std::string& sceneID) const;

    Scope<Scene> Create(const std::string& sceneID) const;

private:
    std::unordered_map<std::string, SceneFactoryFunction> m_Factories;
};

} // namespace Raven
