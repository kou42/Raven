// 1 Groupにつき4×4×4サンプル。同じセルへ複数のInvocationが書かないためAtomicは不要です。
void EvaluateTile(uint tile, UVEC3 localCell)
{
    UVEC4 size = ReadWord(2u);
    uint triangleCount = ReadWord(1u).w;
    uint description = 5u + 3u * triangleCount + 2u * tile;
    UVEC4 entry = ReadWord(description);
    UVEC3 cell = entry.xyz + localCell;
    uint index = tile * 64u + localCell.x + 4u * (localCell.y + 4u * localCell.z);
    float missing = 3.402823466e+38;
    if (cell.x >= size.x || cell.y >= size.y || cell.z >= size.z)
    {
        WriteDistance(index, missing);
        return;
    }
    UVEC4 origin = ReadWord(0u);
    VEC3 p = AS_FLOAT3(origin.xyz) +
        (VEC3(cell) + VEC3(0.5, 0.5, 0.5)) * AS_FLOAT3(ReadWord(1u).xyz);
    float distance = missing;
    float support = AS_FLOAT(ReadWord(4u).x);
    if (origin.w == 0u)
    {
        UVEC4 sphere = ReadWord(3u);
        distance = length(p - AS_FLOAT3(sphere.xyz)) - AS_FLOAT(sphere.w);
    }
    else
    {
        uint candidates = ReadWord(description + 1u).x;
        float minimum = missing;
        for (uint i = 0u; i < candidates; ++i)
        {
            uint firstVertex = 5u + 3u * ReadWord(entry.w + i).x;
            minimum = min(minimum, TriangleDistanceSquared(p, AS_FLOAT3(ReadWord(firstVertex).xyz),
                AS_FLOAT3(ReadWord(firstVertex + 1u).xyz), AS_FLOAT3(ReadWord(firstVertex + 2u).xyz)));
        }
        distance = sqrt(minimum);
        // 距離の候補を絞っても、符号には閉じた面全体の立体角が必要です。
        if (origin.w == 2u && distance > 0.0 && distance <= support)
        {
            float angle = 0.0;
            for (uint i = 0u; i < triangleCount; ++i)
            {
                uint firstVertex = 5u + 3u * i;
                angle += SolidAngle(AS_FLOAT3(ReadWord(firstVertex).xyz) - p,
                    AS_FLOAT3(ReadWord(firstVertex + 1u).xyz) - p,
                    AS_FLOAT3(ReadWord(firstVertex + 2u).xyz) - p);
            }
            if (abs(angle) > 6.28318531)
            {
                distance = -distance;
            }
        }
    }
    WriteDistance(index, abs(distance) <= support ? distance : missing);
}
