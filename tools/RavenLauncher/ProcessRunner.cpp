#include "ProcessRunner.h"

#include <stdexcept>
#include <system_error>
#include <utility>
#include <winrt/base.h>

namespace RavenLauncher
{
    namespace
    {
        std::wstring Quote(const std::wstring& value)
        {
            std::wstring quoted = L"\"";
            size_t slashes = 0;
            for (const wchar_t character : value)
            {
                if (character == L'\\')
                {
                    ++slashes;
                    continue;
                }
                quoted.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                slashes = 0;
                quoted += character;
            }
            quoted.append(slashes * 2, L'\\');
            return quoted + L'"';
        }

        std::wstring CommandLine(const std::filesystem::path& executable,
            const std::vector<std::wstring>& arguments)
        {
            std::wstring command = Quote(executable.native());
            for (const auto& argument : arguments)
            {
                command += L' ' + Quote(argument);
            }
            return command;
        }

        struct Pipe
        {
            winrt::handle reader;
            winrt::handle writer;

            Pipe()
            {
                SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
                HANDLE read = nullptr;
                HANDLE write = nullptr;
                if (CreatePipe(&read, &write, &attributes, 0) == FALSE)
                {
                    ThrowWindowsError("CreatePipe");
                }
                reader.attach(read);
                writer.attach(write);
                if (SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0) == FALSE)
                {
                    ThrowWindowsError("SetHandleInformation");
                }
            }

            // 片方の出力が連続していても、もう片方とキャンセルを毎周確認する。
            bool Drain(std::string& destination, const ProcessOutput& output, bool standardError) const
            {
                DWORD available = 0;
                if (PeekNamedPipe(reader.get(), nullptr, 0, nullptr, &available, nullptr) == FALSE)
                {
                    if (GetLastError() == ERROR_BROKEN_PIPE)
                    {
                        return false;
                    }
                    ThrowWindowsError("PeekNamedPipe");
                }
                if (available == 0)
                {
                    return false;
                }
                char buffer[16384];
                DWORD count = 0;
                if (ReadFile(reader.get(), buffer, (std::min)(available, DWORD{sizeof(buffer)}),
                    &count, nullptr) == FALSE)
                {
                    ThrowWindowsError("ReadFile");
                }
                if (output)
                {
                    output(std::string_view(buffer, count), standardError);
                }
                else
                {
                    destination.append(buffer, count);
                }
                return true;
            }
        };
    }

    [[noreturn]] void ThrowWindowsError(const char* operation)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
    }

    ProcessResult RunProcess(const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments, const std::filesystem::path& directory,
        const std::stop_token stop, const std::chrono::milliseconds timeout, const ProcessOutput& onOutput)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Operation cancelled.");
        }
        Pipe output;
        Pipe errors;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        winrt::handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (input.get() == INVALID_HANDLE_VALUE)
        {
            input.detach();
            ThrowWindowsError("Open NUL");
        }

        winrt::handle job{CreateJobObjectW(nullptr, nullptr)};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job.get() == nullptr || SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
            &limits, sizeof(limits)) == FALSE)
        {
            ThrowWindowsError("Configure process job");
        }
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.get();
        startup.hStdOutput = output.writer.get();
        startup.hStdError = errors.writer.get();
        PROCESS_INFORMATION information{};
        auto command = CommandLine(executable, arguments);
        if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &information) == FALSE)
        {
            ThrowWindowsError("CreateProcess");
        }
        winrt::handle process{information.hProcess};
        winrt::handle thread{information.hThread};

        // 実行前に Job に入れ、キャンセルと例外時に子孫プロセスも終了させる。
        if (AssignProcessToJobObject(job.get(), process.get()) == FALSE)
        {
            const DWORD error = GetLastError();
            TerminateProcess(process.get(), ERROR_CANCELLED);
            SetLastError(error);
            ThrowWindowsError("AssignProcessToJobObject");
        }
        output.writer.close();
        errors.writer.close();
        if (ResumeThread(thread.get()) == DWORD(-1))
        {
            ThrowWindowsError("ResumeThread");
        }

        ProcessResult result;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Operation cancelled.");
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                throw std::runtime_error("Process timed out.");
            }
            const bool readOutput = output.Drain(result.output, onOutput, false);
            const bool readErrors = errors.Drain(result.errors, onOutput, true);
            switch (WaitForSingleObject(process.get(), readOutput || readErrors ? 0 : 10))
            {
            case WAIT_OBJECT_0:
                if (readOutput == false && readErrors == false)
                {
                    // 待機中に書かれた最後のバイトも読む。
                    if (output.Drain(result.output, onOutput, false) | errors.Drain(result.errors, onOutput, true))
                    {
                        break;
                    }
                    if (GetExitCodeProcess(process.get(), &result.exitCode) == FALSE)
                    {
                        ThrowWindowsError("GetExitCodeProcess");
                    }
                    return result;
                }
                break;
            case WAIT_TIMEOUT:
                break;
            default:
                ThrowWindowsError("WaitForSingleObject");
            }
        }
    }

    void LaunchProcess(const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments, const std::filesystem::path& directory)
    {
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION information{};
        auto command = CommandLine(executable, arguments);
        // IDE の生存期間は、ランチャーや診断コマンドの Job から独立させる。
        if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            0, nullptr, directory.c_str(), &startup, &information) == FALSE)
        {
            ThrowWindowsError("Launch Visual Studio");
        }
        winrt::handle process{information.hProcess};
        winrt::handle thread{information.hThread};
    }
}
