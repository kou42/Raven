#include "Raven/UI/Document/DocumentImporterRegistry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Raven
{

bool DocumentImporterRegistry::Register(const std::string& extension, ImporterFactory factory)
{
    const std::string normalized = NormalizeExtension(extension);
    if (normalized.empty() || factory == nullptr)
    {
        return false;
    }

    // 同じ拡張子のImporterを暗黙に上書きすると初期化順依存の挙動になるため、重複登録は失敗させます。
    return m_Factories.emplace(normalized, std::move(factory)).second;
}

bool DocumentImporterRegistry::Unregister(const std::string& extension)
{
    const std::string normalized = NormalizeExtension(extension);
    if (normalized.empty())
    {
        return false;
    }

    // 動的Plugin等がImporterを破棄する前にFactory参照を確実に外せるよう、明示的な登録解除を提供します。
    return m_Factories.erase(normalized) > 0u;
}

bool DocumentImporterRegistry::Contains(const std::string& extension) const
{
    const std::string normalized = NormalizeExtension(extension);
    return normalized.empty() == false && m_Factories.find(normalized) != m_Factories.end();
}

std::unique_ptr<IDocumentImporter> DocumentImporterRegistry::Create(const std::string& extension) const
{
    const std::string normalized = NormalizeExtension(extension);
    const auto found = m_Factories.find(normalized);
    if (found == m_Factories.end())
    {
        return nullptr;
    }
    return found->second();
}

std::string DocumentImporterRegistry::NormalizeExtension(const std::string& extension)
{
    if (extension.empty())
    {
        return {};
    }

    std::string normalized = extension;
    if (normalized.front() != '.')
    {
        normalized.insert(normalized.begin(), '.');
    }

    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return normalized;
}

} // namespace Raven
