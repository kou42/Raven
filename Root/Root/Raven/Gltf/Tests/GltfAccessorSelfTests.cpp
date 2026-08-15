// Raven/Gltf/Tests/GltfAccessorSelfTests.cpp
#include "Raven/Gltf/Tests/GltfAccessorSelfTests.h"

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

bool NearlyEqual(float a, float b, float tolerance = 1.0e-5f)
{
    return std::fabs(a - b) <= tolerance;
}

template<typename T>
void WriteValue(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value)
{
    assert(offset <= bytes.size());
    assert(sizeof(T) <= bytes.size() - offset);
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void RunInterleavedPositionAndNormalizedTexCoordTest()
{
    // POSITIONは16 byte strideでfloat3 + 4 byte padding。
    // TEXCOORD_0はUNSIGNED_BYTE normalized、IndexはUNSIGNED_SHORTです。
    // 1つのテストでAccessorReaderの主要なバイナリ解決経路をまとめて確認します。
    std::vector<std::uint8_t> binary(64u, 0u);

    const float positions[3][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }
    };

    for (std::size_t vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        const std::size_t baseOffset = vertexIndex * 16u;
        WriteValue(binary, baseOffset + 0u, positions[vertexIndex][0]);
        WriteValue(binary, baseOffset + 4u, positions[vertexIndex][1]);
        WriteValue(binary, baseOffset + 8u, positions[vertexIndex][2]);
    }

    binary[48u] = 0u;
    binary[49u] = 0u;
    binary[50u] = 255u;
    binary[51u] = 0u;
    binary[52u] = 0u;
    binary[53u] = 255u;

    const std::uint16_t index0 = 0u;
    const std::uint16_t index1 = 1u;
    const std::uint16_t index2 = 2u;
    WriteValue(binary, 56u, index0);
    WriteValue(binary, 58u, index1);
    WriteValue(binary, 60u, index2);

    const std::string json = R"json(
{
  "asset": { "version": "2.0" },
  "buffers": [
    { "byteLength": 64 }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 48, "byteStride": 16, "target": 34962 },
    { "buffer": 0, "byteOffset": 48, "byteLength": 6,  "target": 34962 },
    { "buffer": 0, "byteOffset": 56, "byteLength": 6,  "target": 34963 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5121, "normalized": true, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
}
)json";

    Gltf::JsonValue root;
    std::string error;
    assert(Gltf::JsonParser::Parse(json, root, &error));

    Gltf::GltfDocument document;
    assert(Gltf::GltfDocument::BuildFromJson(root, std::move(binary), document, &error));

    Gltf::AccessorReader reader(document);

    std::vector<math::Vec3> readPositions;
    assert(reader.ReadVec3(0u, readPositions, &error));
    assert(readPositions.size() == 3u);
    assert(NearlyEqual(readPositions[0].x, 0.0f));
    assert(NearlyEqual(readPositions[1].x, 1.0f));
    assert(NearlyEqual(readPositions[2].y, 1.0f));

    std::vector<math::Vec2> texCoords;
    assert(reader.ReadVec2(1u, texCoords, &error));
    assert(texCoords.size() == 3u);
    assert(NearlyEqual(texCoords[0].x, 0.0f));
    assert(NearlyEqual(texCoords[0].y, 0.0f));
    assert(NearlyEqual(texCoords[1].x, 1.0f));
    assert(NearlyEqual(texCoords[1].y, 0.0f));
    assert(NearlyEqual(texCoords[2].x, 0.0f));
    assert(NearlyEqual(texCoords[2].y, 1.0f));

    std::vector<std::uint32_t> indices;
    assert(reader.ReadIndices(2u, indices, &error));
    assert(indices.size() == 3u);
    assert(indices[0] == 0u);
    assert(indices[1] == 1u);
    assert(indices[2] == 2u);
}

} // namespace

void RunGltfAccessorSelfTests()
{
    RunInterleavedPositionAndNormalizedTexCoordTest();
}

} // namespace tests
} // namespace Raven
