#include "Raven/Math/TransformMath.h"
#include "Raven/Math/MathUtility.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

namespace
{
using namespace Raven::math;
Mat4 Compose(const Vec3& p, const Vec3& r, const Vec3& s)
{
    Mat4 m = Translate(Mat4::Identity(), p);
    m = Rotate(m, r.x, Vec3{1, 0, 0});
    m = Rotate(m, r.y, Vec3{0, 1, 0});
    m = Rotate(m, r.z, Vec3{0, 0, 1});
    return Scale(m, s);
}
void CheckEqual(const Mat4& a, const Mat4& b)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            assert(std::fabs(a.m[row][column] - b.m[row][column]) < 0.0002f * (1.0f + std::fabs(a.m[row][column])));
        }
    }
}
}
int main()
{
    using namespace Raven::math;
    for (float y : {-1.57079632679f, -1.2f, 0.0f, 0.8f, 1.57079632679f})
    {
        for (float sign : {-1.0f, 1.0f})
        {
            const Mat4 input = Compose(Vec3{4, -8, 3}, Vec3{0.4f, y, -0.7f}, Vec3{sign * 2, 3, 4});
            Vec3 p, r, s;
            assert(TryDecomposeTRS(input, p, r, s) == true);
            CheckEqual(input, Compose(p, r, s));
            Mat4 inverse;
            assert(TryInverseAffine(input, inverse) == true);
            CheckEqual(input * inverse, Mat4::Identity());
            CheckEqual(inverse * input, Mat4::Identity());
        }
    }
    Mat4 shear = Mat4::Identity();
    shear.m[0][1] = 0.3f;
    Vec3 p{7, 8, 9}, r{1, 2, 3}, s{4, 5, 6};
    assert(TryDecomposeTRS(shear, p, r, s) == false);
    assert(p == Vec3(7, 8, 9) && r == Vec3(1, 2, 3) && s == Vec3(4, 5, 6));
    Mat4 inverse;
    assert(TryInverseAffine(shear, inverse) == true);
    CheckEqual(shear * inverse, Mat4::Identity());
    Mat4 singular = Mat4::Identity();
    singular.m[0][0] = 0;
    inverse = shear;
    assert(TryInverseAffine(singular, inverse) == false);
    CheckEqual(inverse, shear);
    assert(TryDecomposeTRS(singular, p, r, s) == false);
    singular.m[0][0] = std::numeric_limits<float>::infinity();
    assert(TryInverseAffine(singular, inverse) == false);
    assert(TryDecomposeTRS(singular, p, r, s) == false);
    Mat4 tiny = Mat4::Identity();
    tiny.m[0][0] = std::numeric_limits<float>::denorm_min();
    assert(TryInverseAffine(tiny, inverse) == false);
    CheckEqual(inverse, shear);
    Mat4 perspective = Mat4::Identity();
    perspective.m[3][2] = 1;
    assert(TryInverseAffine(perspective, inverse) == false);
}
