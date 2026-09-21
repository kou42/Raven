#include "Raven/Assets/RHIShaderAsset.h"

#include "Raven/Assets/RHIShaderAssetImporter.h"

#include <filesystem>
#include <functional>
#include <utility>

namespace Raven
{
namespace
{
RHIShaderBinaryFormat GetRequiredFormat(RHIBackend backend)
{
    switch (backend)
    {
    case RHIBackend::Vulkan:
        return RHIShaderBinaryFormat::SPIRV;
    case RHIBackend::DirectX12:
        return RHIShaderBinaryFormat::DXIL;
    case RHIBackend::OpenGL:
    case RHIBackend::DirectX11:
    case RHIBackend::None:
    default:
        return RHIShaderBinaryFormat::None;
    }
}

const std::string* ResolveSourcePath(
    const RHIShaderAssetSpecification& specification,
    RHIBackend backend)
{
    switch (backend)
    {
    case RHIBackend::Vulkan:
        return &specification.VulkanPath;
    case RHIBackend::DirectX12:
        return &specification.DirectX12Path;
    case RHIBackend::OpenGL:
    case RHIBackend::DirectX11:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}
} // namespace

RHIShaderAsset::RHIShaderAsset(
    std::string sourcePath,
    RHIBackend backend,
    RHIShaderBinary binary)
    : m_SourcePath(std::move(sourcePath)),
      m_Backend(backend),
      m_Binary(std::move(binary))
{
}

const std::string& RHIShaderAsset::GetSourcePath() const
{
    return m_SourcePath;
}

RHIBackend RHIShaderAsset::GetBackend() const
{
    return m_Backend;
}

const RHIShaderBinary& RHIShaderAsset::GetBinary() const
{
    return m_Binary;
}

bool RHIShaderAsset::IsValid() const
{
    return m_SourcePath.empty() == false &&
        m_Binary.Format == GetRequiredFormat(m_Backend) &&
        m_Binary.Code.empty() == false &&
        m_Binary.EntryPoint.empty() == false;
}

Ref<RHIShaderAsset> RHIShaderAssetManager::Load(
    const RHIShaderAssetSpecification& specification,
    RHIBackend backend)
{
    const std::string* sourcePath =
        ResolveSourcePath(specification, backend);
    if (sourcePath == nullptr || sourcePath->empty() == true)
    {
        return nullptr;
    }
    return Load(*sourcePath, backend, specification.EntryPoint);
}

Ref<RHIShaderAsset> RHIShaderAssetManager::Load(
    const std::string& sourcePath,
    RHIBackend backend,
    const std::string& entryPoint)
{
    const CacheKey key = BuildCacheKey(sourcePath, backend, entryPoint);
    if (key.SourcePath.empty() == true ||
        key.Backend == RHIBackend::None ||
        key.EntryPoint.empty() == true)
    {
        return nullptr;
    }

    const auto existing = m_Assets.find(key);
    if (existing != m_Assets.end())
    {
        return existing->second;
    }

    Ref<RHIShaderAsset> asset = RHIShaderAssetImporter::Import(
        key.SourcePath,
        key.Backend,
        key.EntryPoint);
    if (asset == nullptr || asset->IsValid() == false)
    {
        return nullptr;
    }

    m_Assets.emplace(key, asset);
    return asset;
}

Ref<RHIShaderAsset> RHIShaderAssetManager::Get(
    const std::string& sourcePath,
    RHIBackend backend,
    const std::string& entryPoint) const
{
    const CacheKey key = BuildCacheKey(sourcePath, backend, entryPoint);
    const auto existing = m_Assets.find(key);
    if (existing == m_Assets.end())
    {
        return nullptr;
    }
    return existing->second;
}

bool RHIShaderAssetManager::Exists(
    const std::string& sourcePath,
    RHIBackend backend,
    const std::string& entryPoint) const
{
    const CacheKey key = BuildCacheKey(sourcePath, backend, entryPoint);
    return m_Assets.find(key) != m_Assets.end();
}

void RHIShaderAssetManager::Clear()
{
    m_Assets.clear();
}

std::size_t RHIShaderAssetManager::CacheKeyHash::operator()(
    const CacheKey& key) const
{
    std::size_t seed = std::hash<std::string>{}(key.SourcePath);
    const auto combine = [&seed](std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };
    combine(std::hash<int>{}(static_cast<int>(key.Backend)));
    combine(std::hash<std::string>{}(key.EntryPoint));
    return seed;
}

RHIShaderAssetManager::CacheKey RHIShaderAssetManager::BuildCacheKey(
    const std::string& sourcePath,
    RHIBackend backend,
    const std::string& entryPoint)
{
    CacheKey key{};
    if (sourcePath.empty() == false)
    {
        // canonical()は実ファイルを要求するため使わず、相対Pathの冗長要素だけを除去します。
        key.SourcePath =
            std::filesystem::path(sourcePath).lexically_normal().generic_string();
    }
    key.Backend = backend;
    key.EntryPoint = entryPoint;
    return key;
}

} // namespace Raven
