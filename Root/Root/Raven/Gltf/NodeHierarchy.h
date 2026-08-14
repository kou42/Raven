// Raven/Gltf/NodeHierarchy.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Gltf/JsonValue.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// NodeTransform
// ============================================================================
// glTF NodeのLocal TransformをRavenの行列規約へ変換した値です。
//
// glTFのmatrix配列はcolumn-majorで格納されていますが、Raven::math::Mat4は
// row-major storage / column-vector multiplication styleです。
// そのためmatrixを読む際は、配列のcolumn-major表現をRavenの[row][column]へ
// 明示的に並べ替えます。
//
// TRS指定の場合はglTF仕様どおり M = T * R * S でLocalMatrixを構築します。
// HasExplicitTrsは後続のSkeleton / Animation Importerが元のTRSを再利用するために保持します。
struct NodeTransform
{
    math::Vec3 Translation{ 0.0f, 0.0f, 0.0f };
    math::Quat Rotation = math::Quat::Identity();
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };
    math::Mat4 LocalMatrix = math::Mat4::Identity();

    bool HasExplicitTrs = true;
};

// ============================================================================
// Node
// ============================================================================
// Scene表示だけでなく、後続のskin.joints -> Skeleton変換でも共通利用するNode定義です。
// ParentIndexはglTF JSONには直接存在しないため、children配列からImporter側で逆引きします。
struct Node
{
    std::string Name;
    std::size_t ParentIndex = InvalidGltfIndex;
    std::vector<std::size_t> Children;

    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t SkinIndex = InvalidGltfIndex;

    NodeTransform Transform;
};

struct Scene
{
    std::string Name;
    std::vector<std::size_t> RootNodes;
};

// ============================================================================
// NodeHierarchy
// ============================================================================
// glTF nodes/scenesをRaven内で扱いやすいForest構造へ変換します。
//
// ここで以下を保証します。
// - child indexがnodes範囲内
// - 1 Nodeが複数Parentを持たない
// - Node階層にcycleがない
// - Scene rootはParentを持たない
// - matrixとTRSを同時指定しない
//
// Skeleton構築時に同じ検証を繰り返さないため、Node階層の正当性はこの層で確定させます。
class NodeHierarchy
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        NodeHierarchy& outHierarchy,
        std::string* errorMessage = nullptr);

    static bool BuildFromJson(
        const JsonValue& root,
        NodeHierarchy& outHierarchy,
        std::string* errorMessage = nullptr);

    const std::vector<Node>& GetNodes() const { return m_Nodes; }
    const std::vector<Scene>& GetScenes() const { return m_Scenes; }

    std::size_t GetDefaultSceneIndex() const { return m_DefaultSceneIndex; }

    // Parentを先に評価し、各NodeのGlobal Matrixを構築します。
    // Node配列順は親->子である必要はありません。
    bool BuildGlobalTransforms(
        std::vector<math::Mat4>& outGlobalTransforms,
        std::string* errorMessage = nullptr) const;

private:
    std::vector<Node> m_Nodes;
    std::vector<Scene> m_Scenes;
    std::size_t m_DefaultSceneIndex = InvalidGltfIndex;
};

} // namespace Gltf
} // namespace Raven
