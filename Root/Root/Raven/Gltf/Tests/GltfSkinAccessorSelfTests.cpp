// Raven/Gltf/Tests/GltfSkinAccessorSelfTests.cpp
#include "Raven/Gltf/Tests/GltfSkinAccessorSelfTests.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Gltf/AccessorReader.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Gltf/JsonParser.h"

namespace Raven
{
namespace tests
{
namespace
{

template<typename T>
void WriteValue(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value)
{
    assert(offset <= bytes.size());
    assert(sizeof(T) <= bytes.size() - offset);
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

bool NearlyEqual(float a, float b, float tolerance = 1.0e-5f)
{
    return std::fabs(a - b) <= tolerance;
}

void RunUnsignedJointsAndNormalizedWeightsTest()
{
    // JOINTS_0はUNSIGNED_SHORT VEC4、WEIGHTS_0はUNSIGNED_BYTE normalized VEC4です。
    // 実Human Assetでよく使われる整数圧縮されたSkin Attributeの基本経路を確認します。
    std::vector<std::uint8_t> binary(24u, 0u);

    const std::uint16_t joints[2][4] = {
        { 3u, 1u, 0u, 2u },
        { 4u, 5u, 6u, 7u }
    };

    for (std::size_t vertexIndex = 0u; vertexIndex < 2u; ++vertexIndex)
    {
        for (std::size_t influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
        {
            WriteValue(
                binary,
                vertexIndex * 8u + influenceIndex * 2u,
                joints[vertexIndex][influenceIndex]);
        }
    }

    // 255,0,0,0 -> 1,0,0,0
    // 128,64,63,0 -> normalized後に約0.502,0.251,0.247,0
    binary[16u] = 255u;
    binary[17u] = 0u;
    binary[18u] = 0u;
    binary[19u] = 0u;
    binary[20u] = 128u;
    binary[21u] = 64u;
    binary[22u] = 63u;
    binary[23u] = 0u;

    const std::string json = R"json(
{
  "asset": { "version": "2.0" },
  "buffers": [
    { "byteLength": 24 }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 16, "target": 34962 },
    { "buffer": 0, "byteOffset": 16, "byteLength": 8,  "target": 34962 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5123, "count": 2, "type": "VEC4" },
    { "bufferView": 1, "componentType": 5121, "normalized": true, "count": 2, "type": "VEC4" }
  ]
}
)json";

    Gltf::JsonValue root;
    std::string error;
    assert(Gltf::JsonParser::Parse(json, root, &error));

    Gltf::GltfDocument document;
    assert(Gltf::GltfDocument::BuildFromJson(root, std::move(binary), document, &error));

    Gltf::AccessorReader reader(document);

    std::vector<Gltf::AccessorReader::UnsignedVec4> readJoints;
    assert(reader.ReadUnsignedVec4(0u, readJoints, &error));
    assert(readJoints.size() == 2u);
    assert(readJoints[0][0] == 3u);
    assert(readJoints[0][1] == 1u);
    assert(readJoints[1][3] == 7u);

    std::vector<math::Vec4> readWeights;
    assert(reader.ReadVec4(1u, readWeights, &error));
    assert(readWeights.size() == 2u);
    assert(NearlyEqual(readWeights[0].x, 1.0f));
    assert(NearlyEqual(readWeights[0].y, 0.0f));
    assert(NearlyEqual(readWeights[1].x, 128.0f / 255.0f));
    assert(NearlyEqual(readWeights[1].y, 64.0f / 255.0f));
    assert(NearlyEqual(readWeights[1].z, 63.0f / 255.0f));
}

} // namespace

void RunGltfSkinAccessorSelfTests()
{
    RunUnsignedJointsAndNormalizedWeightsTest();
}

} // namespace tests
} // namespace Raven
