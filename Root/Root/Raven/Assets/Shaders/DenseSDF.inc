// CPU側DenseSDFGeneratorと共有する配置。Header 4ワード、三角形1枚につき頂点3ワード。
float SegmentDistanceSquared(VEC3 p, VEC3 a, VEC3 b)
{
    VEC3 edge = b - a;
    float squared = dot(edge, edge);
    float t = squared > 0.0 ? clamp(dot(p - a, edge) / squared, 0.0, 1.0) : 0.0;
    VEC3 delta = p - (a + t * edge);
    return dot(delta, delta);
}

float TriangleDistanceSquared(VEC3 p, VEC3 a, VEC3 b, VEC3 c)
{
    VEC3 n = cross(b - a, c - a);
    float area = dot(n, n);
    if (area > 0.0 && dot(cross(b - a, p - a), n) >= 0.0 &&
        dot(cross(c - b, p - b), n) >= 0.0 && dot(cross(a - c, p - c), n) >= 0.0)
    {
        float height = dot(p - a, n);
        return height * height / area;
    }
    return min(SegmentDistanceSquared(p, a, b),
        min(SegmentDistanceSquared(p, b, c), SegmentDistanceSquared(p, c, a)));
}

float SolidAngle(VEC3 a, VEC3 b, VEC3 c)
{
    float la = length(a);
    float lb = length(b);
    float lc = length(c);
    if (la == 0.0 || lb == 0.0 || lc == 0.0)
    {
        return 0.0;
    }
    a /= la;
    b /= lb;
    c /= lc;
    float numerator = dot(a, cross(b, c));
    float denominator = 1.0 + dot(a, b) + dot(b, c) + dot(c, a);
    return denominator == 0.0 ? sign(numerator) * 3.14159265 : 2.0 * ATAN2(numerator, denominator);
}

#ifndef RAVEN_SPARSE
void EvaluateCell(UVEC3 cell)
{
    UVEC3 size = AS_UINT3(ReadWord(2).xyz);
    if (cell.x >= size.x || cell.y >= size.y || cell.z >= size.z)
    {
        return;
    }
    VEC4 origin = ReadWord(0);
    VEC4 spacing = ReadWord(1);
    VEC3 p = origin.xyz + (VEC3(cell) + VEC3(0.5, 0.5, 0.5)) * spacing.xyz;
    float distance;
    if (origin.w == 0.0)
    {
        VEC4 sphere = ReadWord(3);
        distance = length(p - sphere.xyz) - sphere.w;
    }
    else
    {
        uint triangleCount = AS_UINT(spacing.w);
        float minimum = 3.402823466e+38;
        float angle = 0.0;
        for (uint i = 0u; i < triangleCount; ++i)
        {
            uint index = 4u + 3u * i;
            VEC3 a = ReadWord(index).xyz;
            VEC3 b = ReadWord(index + 1u).xyz;
            VEC3 c = ReadWord(index + 2u).xyz;
            minimum = min(minimum, TriangleDistanceSquared(p, a, b, c));
            if (origin.w == 2.0)
            {
                angle += SolidAngle(a - p, b - p, c - p);
            }
        }
        distance = sqrt(minimum);
        if (distance > 0.0 && abs(angle) > 6.28318531)
        {
            distance = -distance;
        }
    }
    WriteDistance(cell, cell.x + size.x * (cell.y + size.y * cell.z), distance);
}
#endif
