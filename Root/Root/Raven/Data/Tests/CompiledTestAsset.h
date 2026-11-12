#pragma once
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace Raven
{
namespace Data
{
namespace Tests
{
// 専用テストAssetのRuntime形式。汎用BinaryDocumentとは独立したMagic/Version。
// Cache KeyはソースByte列とCompiler Versionに依存し、依存Assetは明示的IDとResolverで扱う。
int RunCompiledTestAssetTests();
class CompiledTestAsset
{
public:
    static constexpr std::uint32_t Version = 2;
    // IDは安定したAsset識別子。ResolverはIDから現在の依存Asset内容を取得する。
    // 参照が解決できなければCompileは失敗し、既存outputを維持する。
    using DependencyResolver = std::function<bool(std::string_view, std::string&)>;
    // 各依存Assetの子IDを列挙する。循環参照と重複はGraph側で検出する。
    using DependencyEnumerator = std::function<bool(std::string_view, std::vector<std::string>&)>;
    static bool Compile(std::string_view sourceJson, std::string& output, TestAssetError& error);
    static bool CompileWithGraph(std::string_view sourceJson,
        const std::vector<std::string>& rootIds, const DependencyResolver& resolver,
        const DependencyEnumerator& enumerate, std::string& output, TestAssetError& error);
    static bool IsGraphCacheCurrent(std::string_view compiled, std::string_view sourceJson,
        const std::vector<std::string>& rootIds, const DependencyResolver& resolver,
        const DependencyEnumerator& enumerate);
    static bool CompileWithDependencies(std::string_view sourceJson,
        const std::vector<std::string>& dependencyIds, const DependencyResolver& resolver,
        std::string& output, TestAssetError& error);
    // Cache照合はソース・依存内容・Compiler Versionを検査し、古いArtifactを拒否する。
    static bool IsCacheCurrent(std::string_view compiled, std::string_view sourceJson,
        const std::vector<std::string>& dependencyIds, const DependencyResolver& resolver);
    static bool Load(std::string_view compiled, TestSettings& output, TestAssetError& error);
};
}
}
