#include "Raven/Core/ApplicationState.h"

namespace Raven
{

void ApplicationState::Set(const std::string& key, ApplicationStateValue value)
{
    if (key.empty() == true)
    {
        return;
    }
    m_Values.insert_or_assign(key, std::move(value));
}

const ApplicationStateValue* ApplicationState::Find(const std::string& key) const
{
    const auto it = m_Values.find(key);
    return it != m_Values.end() ? &it->second : nullptr;
}

bool ApplicationState::Contains(const std::string& key) const
{
    return m_Values.find(key) != m_Values.end();
}

bool ApplicationState::Remove(const std::string& key)
{
    return m_Values.erase(key) > 0u;
}

void ApplicationState::Clear()
{
    m_Values.clear();
}

} // namespace Raven
