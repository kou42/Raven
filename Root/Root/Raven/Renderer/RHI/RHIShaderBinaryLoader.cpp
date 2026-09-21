#include "RHIShaderBinaryLoader.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

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

bool IsValidCode(
    RHIShaderBinaryFormat format,
    const std::vector<uint8_t>& code)
{
    if (code.empty() == true)
    {
        return false;
    }

    if (format == RHIShaderBinaryFormat::SPIRV)
    {
        // Vulkanへ渡す前にword境界とSPIR-V magicを確認し、
        // 任意のファイルをShader Moduleとして解釈しないようにします。
        return code.size() >= sizeof(uint32_t) &&
            code.size() % sizeof(uint32_t) == 0 &&
            code[0] == 0x03u &&
            code[1] == 0x02u &&
            code[2] == 0x23u &&
            code[3] == 0x07u;
    }

    // DXILにはContainerとraw bitcodeの両形式があるため、
    // 共通層では非空のみ確認し、詳細検証はDirectX12 Backendへ委譲します。
    return format == RHIShaderBinaryFormat::DXIL;
}
} // namespace

bool RHIShaderBinaryLoader::LoadFromFile(
    const std::filesystem::path& path,
    RHIShaderBinaryFormat format,
    RHIShaderBinary& outShader,
    const std::string& entryPoint)
{
    if (path.empty() == true ||
        format == RHIShaderBinaryFormat::None ||
        entryPoint.empty() == true)
    {
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (file.is_open() == false)
    {
        return false;
    }

    const std::streamoff fileSize =
        static_cast<std::streamoff>(file.tellg());
    if (fileSize <= 0 ||
        static_cast<uintmax_t>(fileSize) >
            static_cast<uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        fileSize > static_cast<std::streamoff>(
            std::numeric_limits<std::streamsize>::max()))
    {
        return false;
    }

    const std::streamsize readSize =
        static_cast<std::streamsize>(fileSize);
    std::vector<uint8_t> code(static_cast<std::size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    if (file.good() == false ||
        file.read(reinterpret_cast<char*>(code.data()), readSize).gcount() != readSize ||
        file.bad() == true ||
        IsValidCode(format, code) == false)
    {
        return false;
    }

    RHIShaderBinary shader{};
    shader.Format = format;
    shader.Code = std::move(code);
    shader.EntryPoint = entryPoint;

    // 完全に読み込み・検証できた場合だけ出力を更新します。
    outShader = std::move(shader);
    return true;
}

bool RHIShaderBinaryLoader::LoadForBackend(
    const std::filesystem::path& path,
    RHIBackend backend,
    RHIShaderBinary& outShader,
    const std::string& entryPoint)
{
    const RHIShaderBinaryFormat format = GetRequiredFormat(backend);
    if (format == RHIShaderBinaryFormat::None)
    {
        return false;
    }
    return LoadFromFile(path, format, outShader, entryPoint);
}

} // namespace Raven
