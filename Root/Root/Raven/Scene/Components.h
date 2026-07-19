// Raven/Scene/Components.h
#pragma once

#include <memory>
#include <string>

#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/Math.h"


namespace Raven
{

class Mesh;
class Material;

struct TagComponent
{
    std::string Tag = "Entity";
};

struct TransformComponent
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Rotation{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };

    math::Mat4 GetTransform() const;
};

struct MeshRendererComponent
{
    std::shared_ptr<Mesh> Mesh = nullptr;
    std::shared_ptr<Material> Material = nullptr;

    bool IsValid() const
    {
        return Mesh && Material;
    }
};

}