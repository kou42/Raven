#pragma once

namespace Raven::tests
{

// FlatHashSetの基本操作、衝突Probe、erase後のTombstone、reserve時の
// Allocation回数を確認する軽量Self Testです。
void RunFlatHashSetSelfTests();

} // namespace Raven::tests
