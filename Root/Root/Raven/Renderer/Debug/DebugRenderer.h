#pragma once

#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"

namespace Raven
{

// ============================================================================
// DebugLine
// ============================================================================
// DebugRendererへ積まれる最小描画単位です。
// Renderer/OpenGLへ直接Physics型を渡さず、最終的にはこのLine列へ変換します。
struct DebugLine
{
    math::Vec3 Start{};
    math::Vec3 End{};
    math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
};

// ============================================================================
// DebugRenderer
// ============================================================================
// Physics、RayCast、Camera Frustumなどから共通利用できるDebug描画キューです。
//
// 現段階ではCPU側にLineを蓄積するところまでを責務とします。
// OpenGLのGL_LINESバッチ描画は次段階でこのGetLines()を入力として実装します。
// この分離により、PhysicsDebugRendererをOpenGLへ依存させずテストできます。
class DebugRenderer
{
public:
    // 前フレームのDebug描画コマンドを破棄します。
    static void Clear();

    // ワールド空間の1本の線をキューへ追加します。
    static void DrawLine(
        const math::Vec3& start,
        const math::Vec3& end,
        const math::Vec3& color = math::Vec3{ 1.0f, 1.0f, 1.0f });

    // AABBの8頂点を12本の辺へ分解してLineキューへ追加します。
    // AABBごとにDraw Callを発行せず、後段でまとめてGL_LINES描画できる形にします。
    static void DrawAABB(
        const ph::AABB& bounds,
        const math::Vec3& color = math::Vec3{ 1.0f, 1.0f, 1.0f });

    // 球Meshを生成せず、X/Y/Z方向の3本の短い線で点を表示します。
    // Contact Pointの大量表示でも軽量に扱えることを優先しています。
    static void DrawPoint(
        const math::Vec3& position,
        float radius,
        const math::Vec3& color = math::Vec3{ 1.0f, 1.0f, 1.0f });

    static const std::vector<DebugLine>& GetLines();

private:
    static std::vector<DebugLine> s_Lines;
};

} // namespace Raven
