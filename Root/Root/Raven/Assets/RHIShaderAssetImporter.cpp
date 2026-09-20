#include "Raven/Assets/RHIShaderAssetImporter.h"

#include "Raven/Assets/RHIShaderAsset.h"
#include "Raven/Renderer/RHI/RHIShaderBinaryLoader.h"

#include <utility>

namespace Raven
{

Ref<RHIShaderAsset> RHIShaderAssetImporter::Import(
    const std::string& sourcePath,
    RHIBackend backend,
    const std::string& entryPoint)
{
    RHIShaderBinary binary{};
    if (RHIShaderBinaryLoader::LoadForBackend(
        sourcePath, backend, binary, entryPoint) == false)
    {
        return nullptr;
    }

    Ref<RHIShaderAsset> asset =
        CreateRef<RHIShaderAsset>(sourcePath, backend, std::move(binary));
    if (asset->IsValid() == false)
    {
        return nullptr;
    }
    return asset;
}

} // namespace Raven
