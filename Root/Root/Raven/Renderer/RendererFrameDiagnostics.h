#pragma once
#include <cstdint>

namespace Raven
{
// BeginFrame直後に生成し、正常終了・break・returnの全経路で同じFrameを閉じます。
class RendererFrameDiagnostics final
{
public:
    RendererFrameDiagnostics() noexcept;
    ~RendererFrameDiagnostics();
    RendererFrameDiagnostics(const RendererFrameDiagnostics&) = delete;
    RendererFrameDiagnostics& operator=(const RendererFrameDiagnostics&) = delete;
private:
    std::uint64_t m_FrameId;
};
}
