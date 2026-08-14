// Raven/Gltf/Tests/GltfNodeHierarchySelfTests.cpp
#include "Raven/Gltf/Tests/GltfNodeHierarchySelfTests.h"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include "Raven/Gltf/JsonParser.h"
#include "Raven/Gltf/NodeHierarchy.h"

namespace Raven
{
namespace tests
{
namespace
{

bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

void RunTrsAndMatrixHierarchyTest()
{
    const std::string json = R"json(
{
  "asset": { "version": "2.0" },
  "nodes": [
    {
      "name": "Root",
      "translation": [1.0, 0.0, 0.0],
      "children": [1]
    },
    {
      "name": "Rotated",
      "rotation": [0.0, 0.0, 0.70710678118, 0.70710678118],
      "children": [2]
    },
    {
      "name": "MatrixNode",
      "matrix": [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 2.0, 0.0, 1.0
      ]
    }
  ],
  "scenes": [
    { "nodes": [0] }
  ],
  "scene": 0
}
)json";

    Gltf::JsonValue root;
    std::string error;
    assert(Gltf::JsonParser::Parse(json, root, &error));

    Gltf::NodeHierarchy hierarchy;
    assert(Gltf::NodeHierarchy::BuildFromJson(root, hierarchy, &error));

    const std::vector<Gltf::Node>& nodes = hierarchy.GetNodes();
    assert(nodes.size() == 3u);
    assert(nodes[0].ParentIndex == Gltf::InvalidGltfIndex);
    assert(nodes[1].ParentIndex == 0u);
    assert(nodes[2].ParentIndex == 1u);

    // matrix配列のtranslation(0, 2, 0)がRavenの[row][column]へ正しく変換されているか確認します。
    assert(NearlyEqual(nodes[2].Transform.LocalMatrix[0][3], 0.0f));
    assert(NearlyEqual(nodes[2].Transform.LocalMatrix[1][3], 2.0f));
    assert(NearlyEqual(nodes[2].Transform.LocalMatrix[2][3], 0.0f));

    std::vector<math::Mat4> globals;
    assert(hierarchy.BuildGlobalTransforms(globals, &error));
    assert(globals.size() == 3u);

    // Rootで(+1, 0)、その子がZ +90deg、さらにLocalで(0, +2)なので、
    // GrandchildのWorld位置は(-1, 0, 0)になります。
    assert(NearlyEqual(globals[2][0][3], -1.0f));
    assert(NearlyEqual(globals[2][1][3], 0.0f));
    assert(NearlyEqual(globals[2][2][3], 0.0f));
}

void RunCycleRejectionTest()
{
    const std::string json = R"json(
{
  "asset": { "version": "2.0" },
  "nodes": [
    { "children": [1] },
    { "children": [0] }
  ]
}
)json";

    Gltf::JsonValue root;
    std::string error;
    assert(Gltf::JsonParser::Parse(json, root, &error));

    Gltf::NodeHierarchy hierarchy;
    const bool result = Gltf::NodeHierarchy::BuildFromJson(root, hierarchy, &error);
    assert(result == false);
}

void RunMatrixAndTrsRejectionTest()
{
    const std::string json = R"json(
{
  "asset": { "version": "2.0" },
  "nodes": [
    {
      "translation": [1.0, 0.0, 0.0],
      "matrix": [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
      ]
    }
  ]
}
)json";

    Gltf::JsonValue root;
    std::string error;
    assert(Gltf::JsonParser::Parse(json, root, &error));

    Gltf::NodeHierarchy hierarchy;
    const bool result = Gltf::NodeHierarchy::BuildFromJson(root, hierarchy, &error);
    assert(result == false);
}

} // namespace

void RunGltfNodeHierarchySelfTests()
{
    RunTrsAndMatrixHierarchyTest();
    RunCycleRejectionTest();
    RunMatrixAndTrsRejectionTest();
}

} // namespace tests
} // namespace Raven
