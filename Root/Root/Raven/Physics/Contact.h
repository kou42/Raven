#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{
struct Contact
{
    Entity A;
    Entity B;
    math::Vec3 Point{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f }; // A から B へ向く法線
    float Penetration = 0.0f;
    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;
    bool IsTrigger = false;
};
}
