#pragma once
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace Raven
{
namespace Data
{
namespace Tests
{
// 専用テストAssetのRuntime形式。汎用BinaryDocumentとは独立したMagic/Version。
// Cache KeyはソースByte列とCompiler Versionに依存し、依存Assetは現段階では存在しません。
int RunCompiledTestAssetTests();
class CompiledTestAsset
{
public:
    static constexpr std::uint32_t Version = 1;
    static bool Compile(std::string_view sourceJson, std::string& output, TestAssetError& error);
    static bool Load(std::string_view compiled, TestSettings& output, TestAssetError& error);
};
}
}
