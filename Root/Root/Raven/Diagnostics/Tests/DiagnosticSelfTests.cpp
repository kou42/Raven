#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/DiagnosticAPI.h"

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

namespace Raven::tests
{
void RunDiagnosticSelfTests()
{
    using namespace Diagnostics;
    static_assert(EventIds::LegacyRHIInitializationFailed != EventIds::PhysicsGravityUpdated);
    static_assert(EventIds::LegacyRHIInitializationFailed != 0);
    static_assert(EventIds::PhysicsGravityUpdated != 0);

    DiagnosticEvent event;
    event.Id = EventIds::LegacyRHIInitializationFailed;
    event.Category = DiagnosticCategory::RHI;
    event.Severity = DiagnosticSeverity::Error;
    event.TimestampNanoseconds = 123;
    event.ThreadId = 42;
    event.Sequence = 7;
    event.Payload = std::string("backend unavailable");

    std::ostringstream output;
    WriteConsoleEvent(output, event);
    assert(output.str() ==
        "[Diagnostics][ERROR][RHI] id=4097 seq=7 ts_ns=123 thread=42 payload=backend unavailable\n");

    event.Id = EventIds::PhysicsGravityUpdated;
    event.Category = DiagnosticCategory::Physics;
    event.Severity = DiagnosticSeverity::Info;
    event.Payload = std::int64_t(-5);
    output.str("");
    output.clear();
    WriteConsoleEvent(output, event);
    assert(output.str().find("[INFO][Physics] id=8193") != std::string::npos);
    assert(output.str().find("payload=-5\n") != std::string::npos);

    event.Payload = true;
    output.str("");
    output.clear();
    WriteConsoleEvent(output, event);
    assert(output.str().find("payload=1\n") != std::string::npos);

    event.Payload = 1.5;
    output.str("");
    output.clear();
    WriteConsoleEvent(output, event);
    assert(output.str().find("payload=1.5\n") != std::string::npos);
}
}
