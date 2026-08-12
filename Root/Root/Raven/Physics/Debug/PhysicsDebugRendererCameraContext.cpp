#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

namespace Raven::ph
{
// このファイルは過去のCamera Context互換コンストラクタを試みていた遺産です。
// 現在のPhysicsDebugRendererはScene参照のみを持ち、Renderer::GetCameraContext()で
// その時点のView/Projectionを取得する設計に統一されているため、ここには実装を置きません。
// 既存のScene側コードからこのコンストラクタを呼ぶ場合のみ、対応するヘッダー/呼び出し側を
// 修正してください。

} // namespace Raven::ph
