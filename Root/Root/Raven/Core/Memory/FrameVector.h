#pragma once

#include "Raven/Core/Memory/STLAllocatorAdapter.h"

#include <vector>

namespace Raven
{

// ============================================================================
// FrameVector
// ============================================================================
// FrameAllocatorなどRaven独自Allocatorをstd::vectorから使うための簡易エイリアスです。
//
// 重要:
// - コンテナ自身はAllocatorを所有しません。
// - backing AllocatorはFrameVectorより長生きする必要があります。
// - FrameAllocatorを使う場合、ResetFrame()後に要素やdata()を参照してはいけません。
// - Linear/FrameAllocatorではdeallocate()がno-opなので、vectorの容量変更で古い領域は
//   個別回収されません。フレーム終端の一括Resetを前提とします。
//
// PhysicsではBroadPhase Pairのような「Step内だけ必要な可変長配列」を最初の対象にし、
// 永続データやWarm Start用Manifoldには使用しません。
template<typename T>
using FrameVector = std::vector<T, STLAllocatorAdapter<T>>;

} // namespace Raven
