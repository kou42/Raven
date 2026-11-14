#pragma once

#include "Raven/Diagnostics/DiagnosticAPI.h"

#include <filesystem>
#include <fstream>
#include <ostream>

namespace Raven::Diagnostics
{
// FilterはSinkごとに固定し、Collector稼働中の設定変更による競合を避けます。
struct DiagnosticFilter
{
    DiagnosticSeverity MinimumSeverity = DiagnosticSeverity::Info;
    std::uint32_t CategoryMask = (1u << DiagnosticCategoryCount) - 1u;
    bool IncludeMeasurements = true;

    bool Accepts(const DiagnosticEvent& event) const noexcept;
};

// SinkはCollectorが排他的に所有し、Write/FlushはWorkerだけが呼びます。
// falseまたは例外はI/O失敗です。CollectorはそのSinkを停止し、他のSinkを継続します。
// Write/FlushからDiagnostics APIへ再入しないでください。故障通知はEmergency経路へ出します。
class IDiagnosticSink
{
public:
    virtual ~IDiagnosticSink() = default;
    virtual bool Write(const DiagnosticEvent& event) = 0;
    virtual bool Flush() = 0;
};

class ConsoleSink final : public IDiagnosticSink
{
public:
    // streamは借用です。CollectorのStop完了まで生存させてください。
    explicit ConsoleSink(std::ostream& output, DiagnosticFilter filter = {});
    bool Write(const DiagnosticEvent& event) override;
    bool Flush() override;

private:
    std::ostream& m_Output;
    DiagnosticFilter m_Filter;
};

class FileSink final : public IDiagnosticSink
{
public:
    // ファイルは新規セッションとして上書きします。親ディレクトリは呼出側で用意します。
    explicit FileSink(const std::filesystem::path& path, DiagnosticFilter filter = {});
    bool IsOpen() const noexcept;
    bool Write(const DiagnosticEvent& event) override;
    bool Flush() override;

private:
    std::ofstream m_Output;
    DiagnosticFilter m_Filter;
};
}
