#include "Raven/Math/TransformMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raven::math
{
namespace
{
constexpr double BasisTolerance = 0.0001;
constexpr double MinimumScale = 0.000001;
}

bool IsFiniteAffine(const Mat4& matrix)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if (std::isfinite(matrix.m[row][column]) == false)
            {
                return false;
            }
        }
    }
    return matrix.m[3][0] == 0.0f && matrix.m[3][1] == 0.0f
        && matrix.m[3][2] == 0.0f && matrix.m[3][3] == 1.0f;
}

bool TryInverseAffine(const Mat4& matrix, Mat4& inverse)
{
    if (IsFiniteAffine(matrix) == false)
    {
        return false;
    }
    double a[3][3]{};
    double lengths[3]{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            a[row][column] = matrix.m[row][column];
            lengths[column] += a[row][column] * a[row][column];
        }
    }
    const double determinant = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
        - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
        + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    const double magnitude = std::sqrt(lengths[0] * lengths[1] * lengths[2]);
    if (magnitude == 0.0 || std::fabs(determinant) <= BasisTolerance * magnitude)
    {
        return false;
    }
    Mat4 result = Mat4::Identity();
    // 転置余因子を用い、Scaleの大きさによらず特異性を相対判定します。
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const int r1 = (column + 1) % 3;
            const int r2 = (column + 2) % 3;
            const int c1 = (row + 1) % 3;
            const int c2 = (row + 2) % 3;
            const double value = (a[r1][c1] * a[r2][c2] - a[r1][c2] * a[r2][c1]) / determinant;
            if (std::isfinite(value) == false || std::fabs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            result.m[row][column] = static_cast<float>(value);
        }
        const double translation = -(static_cast<double>(result.m[row][0]) * matrix.m[0][3]
            + static_cast<double>(result.m[row][1]) * matrix.m[1][3]
            + static_cast<double>(result.m[row][2]) * matrix.m[2][3]);
        if (std::isfinite(translation) == false || std::fabs(translation) > std::numeric_limits<float>::max())
        {
            return false;
        }
        result.m[row][3] = static_cast<float>(translation);
    }
    if (IsFiniteAffine(result) == false)
    {
        return false;
    }
    inverse = result;
    return true;
}

bool TryDecomposeTRS(const Mat4& matrix, Vec3& position, Vec3& rotation, Vec3& scale)
{
    if (IsFiniteAffine(matrix) == false)
    {
        return false;
    }
    double basis[3][3]{};
    double lengths[3]{};
    for (int column = 0; column < 3; ++column)
    {
        double squared = 0.0;
        for (int row = 0; row < 3; ++row)
        {
            const double value = matrix.m[row][column];
            squared += value * value;
        }
        lengths[column] = std::sqrt(squared);
        if (lengths[column] < MinimumScale)
        {
            return false;
        }
        for (int row = 0; row < 3; ++row)
        {
            basis[row][column] = matrix.m[row][column] / lengths[column];
        }
    }
    for (int left = 0; left < 3; ++left)
    {
        for (int right = left + 1; right < 3; ++right)
        {
            const double dot = basis[0][left] * basis[0][right]
                + basis[1][left] * basis[1][right] + basis[2][left] * basis[2][right];
            if (std::fabs(dot) > BasisTolerance)
            {
                return false;
            }
        }
    }
    const double determinant = basis[0][0] * (basis[1][1] * basis[2][2] - basis[1][2] * basis[2][1])
        - basis[0][1] * (basis[1][0] * basis[2][2] - basis[1][2] * basis[2][0])
        + basis[0][2] * (basis[1][0] * basis[2][1] - basis[1][1] * basis[2][0]);
    if (determinant < 0.0)
    {
        lengths[0] = -lengths[0];
        for (int row = 0; row < 3; ++row)
        {
            basis[row][0] = -basis[row][0];
        }
    }
    const double sineY = std::clamp(basis[0][2], -1.0, 1.0);
    const double cosineY = std::hypot(basis[0][0], basis[0][1]);
    const Vec3 angles{
        static_cast<float>(cosineY > BasisTolerance ? std::atan2(-basis[1][2], basis[2][2]) : std::atan2(basis[2][1], basis[1][1])),
        static_cast<float>(std::atan2(sineY, cosineY)),
        static_cast<float>(cosineY > BasisTolerance ? std::atan2(-basis[0][1], basis[0][0]) : 0.0)
    };
    for (double length : lengths)
    {
        if (std::fabs(length) > std::numeric_limits<float>::max())
        {
            return false;
        }
    }
    const Vec3 scales{static_cast<float>(lengths[0]), static_cast<float>(lengths[1]), static_cast<float>(lengths[2])};
    if (std::isfinite(scales.x) == false || std::isfinite(scales.y) == false || std::isfinite(scales.z) == false)
    {
        return false;
    }
    position = Vec3{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
    rotation = angles;
    scale = scales;
    return true;
}
}
