#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIGraphicsPipelineSpecification.h"

#include <string>
#include <unordered_map>

namespace Raven
{

// 一つの論理Shaderに対するBackend別のコンパイル済みバイナリ候補です。
// Source GLSL/HLSLのcompile責務は持たず、実行時に選択する成果物の場所だけを表します。
struct RHIShaderAssetSpecification
{
    std::string VulkanPath;
    std::string DirectX12Path;
    std::string EntryPoint = "main";
};

class RHIShaderAsset
{
public:
    RHIShaderAsset(
        std::string sourcePath,
        RHIBackend backend,
        RHIShaderBinary binary);

    const std::string& GetSourcePath() const;
    RHIBackend GetBackend() const;
    const RHIShaderBinary& GetBinary() const;
    bool IsValid() const;

private:
    std::string m_SourcePath;
    RHIBackend m_Backend = RHIBackend::None;
    RHIShaderBinary m_Binary;
};

// Backend・正規化済みPath・Entry Pointが同じShader Assetを再利用するRuntime Cacheです。
// 読み込みと形式検証はRHIShaderAssetImporterへ委譲します。
class RHIShaderAssetManager
{
public:
    Ref<RHIShaderAsset> Load(
        const RHIShaderAssetSpecification& specification,
        RHIBackend backend);

    Ref<RHIShaderAsset> Load(
        const std::string& sourcePath,
        RHIBackend backend,
        const std::string& entryPoint = "main");

    Ref<RHIShaderAsset> Get(
        const std::string& sourcePath,
        RHIBackend backend,
        const std::string& entryPoint = "main") const;

    bool Exists(
        const std::string& sourcePath,
        RHIBackend backend,
        const std::string& entryPoint = "main") const;

    void Clear();

private:
    struct CacheKey
    {
        std::string SourcePath;
        RHIBackend Backend = RHIBackend::None;
        std::string EntryPoint;

        bool operator==(const CacheKey& other) const
        {
            return SourcePath == other.SourcePath &&
                Backend == other.Backend &&
                EntryPoint == other.EntryPoint;
        }
    };

    struct CacheKeyHash
    {
        std::size_t operator()(const CacheKey& key) const;
    };

    static CacheKey BuildCacheKey(
        const std::string& sourcePath,
        RHIBackend backend,
        const std::string& entryPoint);

    std::unordered_map<CacheKey, Ref<RHIShaderAsset>, CacheKeyHash> m_Assets;
};

} // namespace Raven
