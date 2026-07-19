// Raven/Scene/Components.cpp
#include "Raven/Scene/Components.h"
#include "Raven/Math/MathUtility.h"

//#include <glm/gtc/matrix_transform.hpp>

namespace Raven
{

math::Mat4 TransformComponent::GetTransform() const
{
    math::Mat4 transform = math::Mat4().Identity();

    transform = math::Translate(transform, Position);

    transform = math::Rotate(transform, Rotation.x, math::Vec3{ 1.0f, 0.0f, 0.0f });
    transform = math::Rotate(transform, Rotation.y, math::Vec3{ 0.0f, 1.0f, 0.0f });
    transform = math::Rotate(transform, Rotation.z, math::Vec3{ 0.0f, 0.0f, 1.0f });

    transform = math::Scale(transform, Scale);

    return transform;
}

}