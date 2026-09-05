#include "Raven/UI/Document/DocumentLoader.h"

#include "Raven/UI/Svg/SvgImporter.h"

#include <memory>
#include <utility>

namespace Raven
{

DocumentLoader::DocumentLoader()
{
    // 標準対応形式はここで登録します。新形式追加時もLoad本体へ形式固有分岐を追加しません。
    m_Registry.Register(".svg", []()
    {
        return std::make_unique<SvgImporter>();
    });
}

bool DocumentLoader::CanLoad(const std::string& path) const
{
    const std::string extension = GetExtension(path);
    if (extension.empty())
    {
        return false;
    }
    return m_Registry.Contains(extension);
}

bool DocumentLoader::Load(
    const std::string& path,
    UIDocument& outDocument,
    std::string* outError) const
{
    const std::string extension = GetExtension(path);
    if (extension.empty())
    {
        if (outError != nullptr)
        {
            *outError = "Document file extension was not found: " + path;
        }
        return false;
    }

    std::unique_ptr<IDocumentImporter> importer = m_Registry.Create(extension);
    if (importer == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported document file extension: " + extension;
        }
        return false;
    }

    UIDocument imported;
    if (importer->ImportFile(path, imported, outError) == false)
    {
        return false;
    }

    // Import失敗時に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(imported);
    return true;
}

std::string DocumentLoader::GetExtension(const std::string& path)
{
    // directory名に含まれる'.'を拡張子と誤認しないよう、最後のseparatorより後ろだけを対象にします。
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (separator != std::string::npos && dot < separator) ||
        dot + 1u >= path.size())
    {
        return {};
    }
    return path.substr(dot);
}

} // namespace Raven
