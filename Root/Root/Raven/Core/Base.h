#pragma once

#include <memory>

#if 0
Core
↑
Renderer
↑
Scene
↑
Game
みたいな依存方向にすると綺麗
#endif

namespace Raven
{

// Base.h は：

    //スマートポインタ
    //マクロ
    //アサート
    //プラットフォーム判定
    //DLL export
    //型定義

    //など、
    //「エンジン全体の土台」
    //を置く場所になります。
 
// エンジン側で STL を隠せるからです。
// 将来的に：
// custom allocator、メモリトラッキング、pooled allocator
// に切替できます。
// CreateScope<T>()を使っておけば、内部実装を変えることができる

// 単一所有権のスマートポインタ
template<typename T>
using Scope = std::unique_ptr<T>;

template<typename T, typename ... Args>
constexpr Scope<T> CreateScope(Args&& ... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

// 共有所有権のスマートポインタ
template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T, typename ... Args>
constexpr Ref<T> CreateRef(Args&& ... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

}