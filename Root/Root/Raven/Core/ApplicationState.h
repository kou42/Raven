#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace Raven
{

using ApplicationStateValue = std::variant<bool, std::int64_t, double, std::string>;

// Sceneに依存しないGame/Application状態を保持します。
// EntityやRenderer Resourceは格納せず、Scene交換を跨いで必要な小さな値だけを扱います。
class ApplicationState
{
public:
    void Set(const std::string& key, ApplicationStateValue value);
    const ApplicationStateValue* Find(const std::string& key) const;
    bool Contains(const std::string& key) const;
    bool Remove(const std::string& key);
    void Clear();
    std::size_t GetValueCount() const { return m_Values.size(); }

private:
    std::unordered_map<std::string, ApplicationStateValue> m_Values;
};

} // namespace Raven
