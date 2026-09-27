#include "Raven/Assets/TextureAssetBatch.h"

#include "Raven/Assets/TextureAssetImporter.h"

#include <utility>

namespace Raven
{

TextureAssetBatch::TextureAssetBatch(std::vector<std::string> sourcePaths)
{
    m_Entries.reserve(sourcePaths.size());
    for (std::string& sourcePath : sourcePaths)
    {
        Entry entry{};
        entry.SourcePath = std::move(sourcePath);
        m_Entries.emplace_back(std::move(entry));
    }
}

bool TextureAssetBatch::DecodeAll(
    const ProgressCallback& onProgress,
    const CancellationCallback& isCancellationRequested)
{
    if (m_Entries.empty() == true)
    {
        if (onProgress != nullptr)
        {
            onProgress(1.0f);
        }
        return true;
    }

    const float count = static_cast<float>(m_Entries.size());
    for (std::size_t index = 0u; index < m_Entries.size(); ++index)
    {
        if (isCancellationRequested != nullptr && isCancellationRequested() == true)
        {
            return false;
        }

        Entry& entry = m_Entries[index];
        entry.PixelData = TextureAssetImporter::DecodePixelData(entry.SourcePath);
        if (entry.PixelData.IsValid() == false)
        {
            return false;
        }

        if (onProgress != nullptr)
        {
            onProgress(static_cast<float>(index + 1u) / count);
        }
    }

    return true;
}

bool TextureAssetBatch::Finalize(TextureAssetManager& manager)
{
    // 共有Asset Cacheの更新はApplication/Main Thread側からだけ呼びます。
    // Decode済みPixelはmoveし、同じ画像byte列を余分に複製しません。
    for (Entry& entry : m_Entries)
    {
        if (entry.PixelData.IsValid() == false)
        {
            return false;
        }

        Ref<TextureAsset> asset =
            manager.RegisterDecoded(entry.SourcePath, std::move(entry.PixelData));
        if (asset == nullptr)
        {
            return false;
        }
    }

    return true;
}

std::size_t TextureAssetBatch::GetDecodedCount() const
{
    std::size_t count = 0u;
    for (const Entry& entry : m_Entries)
    {
        if (entry.PixelData.IsValid() == true)
        {
            ++count;
        }
    }
    return count;
}

} // namespace Raven
