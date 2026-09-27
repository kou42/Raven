#pragma once

#include "Raven/Assets/TextureAsset.h"

#include <functional>
#include <string>
#include <vector>

namespace Raven
{

// Scene Preparation等で複数TextureをCPU decodeするための一時Batchです。
// Worker側ではPixel Dataだけを保持し、共有Cacheへの登録はMain ThreadのFinalize()で行います。
class TextureAssetBatch
{
public:
    using ProgressCallback = std::function<void(float)>;
    using CancellationCallback = std::function<bool()>;

    explicit TextureAssetBatch(std::vector<std::string> sourcePaths);

    bool DecodeAll(
        const ProgressCallback& onProgress = {},
        const CancellationCallback& isCancellationRequested = {});
    bool Finalize(TextureAssetManager& manager);

    std::size_t GetAssetCount() const { return m_Entries.size(); }
    std::size_t GetDecodedCount() const;

private:
    struct Entry
    {
        std::string SourcePath;
        TextureAssetPixelData PixelData;
    };

    std::vector<Entry> m_Entries;
};

} // namespace Raven
