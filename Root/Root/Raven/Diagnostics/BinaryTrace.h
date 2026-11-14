#pragma once

#include "Raven/Diagnostics/DiagnosticSink.h"

#include <istream>

namespace Raven::Diagnostics
{
// v1.1: little endian / IEEE754 binary64。v1.0のLogは同じRecord配置で読めます。
// 計測Payloadをtag=5として追加します。旧Readerは未知minorを拒否します。
// Header=magic[8], major:u16, minor:u16, reserved:u32。
// Record=size:u32, id:u64, timestamp:u64, thread:u64, sequence:u64,
// category:u8, severity:u8, payloadType:u8, reserved:u8, payloadSize:u32, payload。
bool WriteBinaryTraceHeader(std::ostream& output);
bool WriteBinaryTraceEvent(std::ostream& output, const DiagnosticEvent& event);

enum class TraceReadResult
{
    Event, EndOfStream, InvalidData, UnsupportedVersion, IOError
};

class BinaryTraceReader final
{
public:
    // streamは借用。ReadNextがEvent以外を返した後は同じ結果を返します。
    explicit BinaryTraceReader(std::istream& input);
    TraceReadResult ReadNext(DiagnosticEvent& event);

private:
    std::istream& m_Input;
    bool m_HeaderRead = false;
    std::uint16_t m_MinorVersion = 0;
    TraceReadResult m_Result = TraceReadResult::Event;
};

class BinaryTraceSink final : public IDiagnosticSink
{
public:
    explicit BinaryTraceSink(const std::filesystem::path& path, DiagnosticFilter filter = {});
    bool IsOpen() const noexcept;
    bool Write(const DiagnosticEvent& event) override;
    bool Flush() override;

private:
    std::ofstream m_Output;
    DiagnosticFilter m_Filter;
};
}
