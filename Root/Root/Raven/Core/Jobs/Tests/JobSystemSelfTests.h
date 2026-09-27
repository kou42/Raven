#pragma once

namespace Raven::tests
{

// GPU/Windowへ依存せず、Job投入・戻り値・複数Worker実行を確認する軽量Self Testです。
void RunJobSystemSelfTests();

} // namespace Raven::tests
