// Raven/Character/Tests/CharacterCeilingCollisionSelfTests.h
#pragma once

namespace Raven::tests
{

// Characterの天井衝突とWalk / Run / Sprint速度選択をassertで検証します。
// Debug起動時にmain.cppから1回だけ実行し、Gameplay速度規約の退行を早期検出します。
void RunCharacterCeilingCollisionSelfTests();

} // namespace Raven::tests
