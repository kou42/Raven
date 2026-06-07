// Raven/Scene/Components.cpp
#include "Components.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Raven
{

math::Mat4 TransformComponent::GetTransform() const
{
    math::Mat4 transform = math::Mat4().Identity();

    transform = glm::translate(transform, Position);

    transform = glm::rotate(transform, Rotation.x, math::Vec3{ 1.0f, 0.0f, 0.0f });
    transform = glm::rotate(transform, Rotation.y, math::Vec3{ 0.0f, 1.0f, 0.0f });
    transform = glm::rotate(transform, Rotation.z, math::Vec3{ 0.0f, 0.0f, 1.0f });

    transform = glm::scale(transform, Scale);

    return transform;
}

}