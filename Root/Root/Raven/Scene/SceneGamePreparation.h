#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{

// Game Scene生成前にWorkerで構築できるCPU Resource群です。
// GPU Resource / Material / Entityは含めず、Main Thread制約を持つ処理と明確に分離します。
struct SceneGamePreparedResources
{
    Ref<MeshGeometry> SphereGeometry;
    Ref<MeshGeometry> BoxGeometry;

    bool IsValid() const
    {
        return SphereGeometry != nullptr && BoxGeometry != nullptr;
    }
};

} // namespace Raven
