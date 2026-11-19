#include "Raven/Physics/Field/DenseSDF.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace Raven::ph
{
namespace
{
using math::Vec3;

bool IsFinite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float SegmentDistanceSquared(Vec3 p, Vec3 a, Vec3 b)
{
    const Vec3 ab = b - a;
    const float lengthSquared = ab.LengthSq();
    const float t = lengthSquared > 0.0f
        ? std::clamp(Vec3::Dot(p - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).LengthSq();
}

float TriangleDistanceSquared(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
    const Vec3 n = Vec3::Cross(b - a, c - a);
    const float areaSquared = n.LengthSq();
    const Vec3 ap = p - a;
    // 面内の射影は同じ向きの3辺判定で判別します。退化面は線分の集合として評価します。
    if (areaSquared > 0.0f &&
        Vec3::Dot(Vec3::Cross(b - a, ap), n) >= 0.0f &&
        Vec3::Dot(Vec3::Cross(c - b, p - b), n) >= 0.0f &&
        Vec3::Dot(Vec3::Cross(a - c, p - c), n) >= 0.0f)
    {
        const float height = Vec3::Dot(ap, n);
        return height * height / areaSquared;
    }
    return std::min({SegmentDistanceSquared(p, a, b),
        SegmentDistanceSquared(p, b, c), SegmentDistanceSquared(p, c, a)});
}

double SolidAngle(Vec3 a, Vec3 b, Vec3 c)
{
    const float la = a.Length();
    const float lb = b.Length();
    const float lc = c.Length();
    if (la == 0.0f || lb == 0.0f || lc == 0.0f)
    {
        return 0.0;
    }
    a /= la;
    b /= lb;
    c /= lc;
    return 2.0 * std::atan2(static_cast<double>(Vec3::Dot(a, Vec3::Cross(b, c))),
        1.0 + Vec3::Dot(a, b) + Vec3::Dot(b, c) + Vec3::Dot(c, a));
}

std::size_t Index(const RHIComputeExtent& size, std::size_t x, std::size_t y, std::size_t z)
{
    return x + size[0] * (y + size[1] * z);
}

using ShaderWord = std::array<float, 4>;
ShaderWord Pack(Vec3 value, float w)
{
    return {value.x, value.y, value.z, w};
}
} // namespace

std::optional<DenseSDFSource> DenseSDFSource::CreateSphere(Vec3 center, float radius)
{
    if (IsFinite(center) == false || std::isfinite(radius) == false || radius <= 0.0f)
    {
        return std::nullopt;
    }
    DenseSDFSource source;
    source.m_Center = center;
    source.m_Radius = radius;
    return source;
}

std::optional<DenseSDFSource> DenseSDFSource::CreateMesh(std::span<const Vec3> vertices,
    std::span<const Triangle> triangles, Mode mode)
{
    if ((mode != Mode::ClosedMesh && mode != Mode::UnsignedMesh) || triangles.empty() ||
        triangles.size() > std::numeric_limits<std::uint32_t>::max() / 3u)
    {
        return std::nullopt;
    }
    DenseSDFSource source;
    source.m_Mode = mode;
    source.m_TriangleVertices.reserve(triangles.size() * 3u);
    struct Edge { int Count = 0; int Direction = 0; };
    std::map<std::pair<std::uint32_t, std::uint32_t>, Edge> edges;
    for (const auto& triangle : triangles)
    {
        for (const auto index : triangle)
        {
            if (index >= vertices.size() || IsFinite(vertices[index]) == false)
            {
                return std::nullopt;
            }
            source.m_TriangleVertices.push_back(vertices[index]);
        }
        if (mode == Mode::ClosedMesh)
        {
            const Vec3 a = vertices[triangle[0]];
            const Vec3 b = vertices[triangle[1]];
            const Vec3 c = vertices[triangle[2]];
            const float areaSquared = Vec3::Cross(b - a, c - a).LengthSq();
            if (areaSquared <= 0.0f || std::isfinite(areaSquared) == false)
            {
                return std::nullopt;
            }
            for (std::size_t i = 0; i < triangle.size(); ++i)
            {
                const auto from = triangle[i];
                const auto to = triangle[(i + 1) % triangle.size()];
                auto& edge = edges[std::minmax(from, to)];
                if (++edge.Count > 2)
                {
                    return std::nullopt;
                }
                edge.Direction += from < to ? 1 : -1;
            }
        }
    }
    // 有向辺が対にならない入力を拒否します。自己交差のないことは入力側の契約です。
    for (const auto& [indices, edge] : edges)
    {
        if (edge.Count != 2 || edge.Direction != 0)
        {
            return std::nullopt;
        }
    }
    source.m_TriangleIndices.assign(triangles.begin(), triangles.end());
    return source;
}

float DenseSDFSource::Evaluate(const Vec3& p) const
{
    if (IsFinite(p) == false)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (m_Mode == Mode::Sphere)
    {
        return (p - m_Center).Length() - m_Radius;
    }
    float minimum = std::numeric_limits<float>::infinity();
    double angle = 0.0;
    for (std::size_t i = 0; i < m_TriangleVertices.size(); i += 3)
    {
        const Vec3 a = m_TriangleVertices[i];
        const Vec3 b = m_TriangleVertices[i + 1];
        const Vec3 c = m_TriangleVertices[i + 2];
        minimum = std::min(minimum, TriangleDistanceSquared(p, a, b, c));
        if (m_Mode == Mode::ClosedMesh)
        {
            angle += SolidAngle(a - p, b - p, c - p);
        }
    }
    const float distance = std::sqrt(minimum);
    // 外側の立体角の総和は0、内側は±4πです。半分を閾値にし、面の全体反転を許容します。
    return distance > 0.0f && std::abs(angle) > 2.0 * std::numbers::pi ? -distance : distance;
}

DenseSDFSource::Mode DenseSDFSource::GetMode() const { return m_Mode; }
Vec3 DenseSDFSource::GetCenter() const { return m_Center; }
float DenseSDFSource::GetRadius() const { return m_Radius; }
std::span<const Vec3> DenseSDFSource::GetTriangleVertices() const { return m_TriangleVertices; }
std::span<const DenseSDFSource::Triangle> DenseSDFSource::GetTriangleIndices() const { return m_TriangleIndices; }

Vec3 DenseSDFGrid::GetCellCenter(std::uint32_t x, std::uint32_t y, std::uint32_t z) const
{
    return Origin + Vec3((static_cast<float>(x) + 0.5f) * Spacing.x,
        (static_cast<float>(y) + 0.5f) * Spacing.y, (static_cast<float>(z) + 0.5f) * Spacing.z);
}

std::size_t DenseSDFGrid::GetVoxelCount() const
{
    if (IsFinite(Origin) == false || IsFinite(Spacing) == false ||
        Spacing.x <= 0.0f || Spacing.y <= 0.0f || Spacing.z <= 0.0f)
    {
        return 0;
    }
    std::size_t count = 1;
    for (const auto dimension : Size)
    {
        // GPU側は32-bit byte offsetでReadback Bufferを参照します。
        if (dimension == 0 || count > (std::numeric_limits<std::uint32_t>::max() / sizeof(float)) / dimension)
        {
            return 0;
        }
        count *= dimension;
    }
    return IsFinite(GetCellCenter(Size[0] - 1, Size[1] - 1, Size[2] - 1)) ? count : 0;
}

bool DenseSDFGrid::TryGetNormal(std::span<const float> distances, const RHIComputeExtent& cell,
    Vec3& normal) const
{
    const auto count = GetVoxelCount();
    if (count == 0 || distances.size() != count)
    {
        return false;
    }
    Vec3 gradient{};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (cell[axis] == 0 || cell[axis] >= Size[axis] - 1)
        {
            return false;
        }
        auto lo = cell;
        auto hi = cell;
        --lo[axis];
        ++hi[axis];
        gradient[axis] = (distances[Index(Size, hi[0], hi[1], hi[2])] -
            distances[Index(Size, lo[0], lo[1], lo[2])]) / (2.0f * Spacing[axis]);
    }
    const float length = gradient.Length();
    if (std::isfinite(length) == false || length <= math::Epsilon)
    {
        return false;
    }
    normal = gradient / length;
    return true;
}

bool DenseSDFGenerator::Prepare(RHIComputeCommandEncoder& encoder, RHIComputePipelineSpecification shader,
    const DenseSDFSource& source, const DenseSDFGrid& grid)
{
    constexpr std::uint32_t groupWidth = 4;
    const auto voxels = grid.GetVoxelCount();
    const auto vertices = source.GetTriangleVertices();
    constexpr std::size_t headerWords = 4;
    if (voxels == 0 || vertices.size() > std::numeric_limits<std::uint32_t>::max() / sizeof(ShaderWord) - headerWords)
    {
        return false;
    }
    std::vector<ShaderWord> input;
    input.reserve(headerWords + vertices.size());
    input.push_back(Pack(grid.Origin, static_cast<float>(source.GetMode())));
    input.push_back(Pack(grid.Spacing, std::bit_cast<float>(static_cast<std::uint32_t>(vertices.size() / 3))));
    // サイズの各成分もuintビット列にしてfloat丸めを避けます。
    input.push_back({std::bit_cast<float>(grid.Size[0]), std::bit_cast<float>(grid.Size[1]),
        std::bit_cast<float>(grid.Size[2]), 0.0f});
    input.push_back(Pack(source.GetCenter(), source.GetRadius()));
    for (const auto& vertex : vertices)
    {
        input.push_back(Pack(vertex, 0.0f));
    }
    shader.LocalSize = {groupWidth, groupWidth, groupWidth};
    shader.Layout = {{0, RHIComputeBindingType::StorageBuffer, RHIComputeAccess::Read},
        {1, RHIComputeBindingType::StorageBuffer, RHIComputeAccess::Write},
        {2, RHIComputeBindingType::StorageImage3D, RHIComputeAccess::Write}};
    auto pipeline = encoder.CreatePipeline(shader);
    auto parameters = encoder.CreateStorageBuffer(input.size() * sizeof(ShaderWord), input.data());
    auto distances = encoder.CreateStorageBuffer(voxels * sizeof(float), nullptr);
    auto texture = encoder.CreateStorageImage3D(grid.Size);
    if (pipeline == nullptr || parameters == nullptr || distances == nullptr || texture == nullptr)
    {
        return false;
    }
    m_Pipeline = std::move(pipeline);
    m_Input = std::move(parameters);
    m_Distances = std::move(distances);
    m_Texture = std::move(texture);
    m_Grid = grid;
    m_Signed = source.GetMode() != DenseSDFSource::Mode::UnsignedMesh;
    m_Bytes = voxels * sizeof(float);
    for (std::size_t axis = 0; axis < m_Groups.size(); ++axis)
    {
        m_Groups[axis] = (grid.Size[axis] - 1) / groupWidth + 1;
    }
    return true;
}

Ref<RHIComputeReadback> DenseSDFGenerator::Record(RHIComputeCommandEncoder& encoder) const
{
    if (RecordTexture(encoder) == false || encoder.Barrier(RHIComputeConsumer::BufferReadback) == false)
    {
        return nullptr;
    }
    return encoder.RequestBufferReadback(m_Distances, 0, m_Bytes);
}

bool DenseSDFGenerator::RecordTexture(RHIComputeCommandEncoder& encoder) const
{
    const RHIComputeBinding bindings[] = {{0, m_Input}, {1, m_Distances}, {2, m_Texture}};
    return m_Pipeline != nullptr && encoder.Barrier(RHIComputeConsumer::Storage) &&
        encoder.Dispatch(m_Pipeline, bindings, m_Groups) && encoder.Barrier(RHIComputeConsumer::TextureSampling);
}

const Ref<RHIComputeResource>& DenseSDFGenerator::GetTexture() const { return m_Texture; }
const DenseSDFGrid& DenseSDFGenerator::GetGrid() const { return m_Grid; }
bool DenseSDFGenerator::IsSigned() const { return m_Signed; }

} // namespace Raven::ph
