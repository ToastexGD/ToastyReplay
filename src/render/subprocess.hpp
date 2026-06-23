#pragma once

#ifdef GEODE_IS_WINDOWS

#include <windows.h>
#include <string>
#include <string_view>
#include <cstdint>

class Subprocess {
    struct AttributeList {
        LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;

        bool init(HANDLE* inheritHandles, DWORD count) {
            SIZE_T bytes = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
            list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, bytes));
            if (!list) return false;
            if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) { release(); return false; }
            if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritHandles, count * sizeof(HANDLE), nullptr, nullptr)) { release(); return false; }
            return true;
        }

        void release() {
            if (list) {
                DeleteProcThreadAttributeList(list);
                HeapFree(GetProcessHeap(), 0, list);
                list = nullptr;
            }
        }

        ~AttributeList() { release(); }
    };

public:
    Subprocess() = default;

    Subprocess(const std::string& command, const std::wstring& stderrLogPath = L"") {
        if (!initPipe()) return;
        std::wstring cmd = widenCommand(command);
        if (cmd.empty()) return;
        if (!spawnChild(cmd, stderrLogPath)) return;
        attachToJob();
    }

    ~Subprocess() { releaseAll(); }

    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    Subprocess(Subprocess&& other) noexcept
        : m_stdinRead(other.m_stdinRead)
        , m_stdinWrite(other.m_stdinWrite)
        , m_pi(other.m_pi)
        , m_job(other.m_job) {
        other.m_stdinRead = nullptr;
        other.m_stdinWrite = nullptr;
        other.m_pi = {};
        other.m_job = nullptr;
    }

    Subprocess& operator=(Subprocess&& other) noexcept {
        if (this != &other) {
            releaseAll();
            m_stdinRead = other.m_stdinRead;
            m_stdinWrite = other.m_stdinWrite;
            m_pi = other.m_pi;
            m_job = other.m_job;
            other.m_stdinRead = nullptr;
            other.m_stdinWrite = nullptr;
            other.m_pi = {};
            other.m_job = nullptr;
        }
        return *this;
    }

    bool isRunning() const { return m_pi.hProcess != nullptr; }

    // Raw kernel handles, valid for as long as this object lives. Exposed so a
    // watchdog on another thread can force-kill a hung ffmpeg without owning the
    // object: TerminateProcess on the process handle, or CloseHandle on the job
    // (which carries JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) both terminate the child.
    HANDLE processHandle() const { return m_pi.hProcess; }
    HANDLE jobHandle() const { return m_job; }

    void kill() {
        if (m_pi.hProcess) TerminateProcess(m_pi.hProcess, 1);
    }

    bool writeStdin(const uint8_t* data, size_t size) {
        if (!m_stdinWrite) return false;
        size_t offset = 0;
        while (offset < size) {
            DWORD written = 0;
            if (!WriteFile(m_stdinWrite, data + offset, static_cast<DWORD>(size - offset), &written, nullptr)) return false;
            offset += written;
        }
        return true;
    }

    int close() {
        if (m_stdinWrite) {
            FlushFileBuffers(m_stdinWrite);
            CloseHandle(m_stdinWrite);
            m_stdinWrite = nullptr;
        }
        if (!m_pi.hProcess) return -1;
        WaitForSingleObject(m_pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(m_pi.hProcess, &exitCode);
        CloseHandle(m_pi.hProcess);
        CloseHandle(m_pi.hThread);
        m_pi.hProcess = nullptr;
        m_pi.hThread = nullptr;
        return static_cast<int>(exitCode);
    }

private:
    HANDLE m_stdinRead = nullptr;
    HANDLE m_stdinWrite = nullptr;
    PROCESS_INFORMATION m_pi = {};
    HANDLE m_job = nullptr;

    static std::wstring widenCommand(std::string_view command) {
        if (command.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, command.data(), static_cast<int>(command.size()), nullptr, 0);
        if (len <= 0) return {};
        std::wstring result(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, command.data(), static_cast<int>(command.size()), result.data(), len);
        return result;
    }

    bool initPipe() {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        if (!CreatePipe(&m_stdinRead, &m_stdinWrite, &sa, 0)) return false;
        SetHandleInformation(m_stdinWrite, HANDLE_FLAG_INHERIT, 0);
        return true;
    }

    bool spawnChild(std::wstring& command, const std::wstring& stderrLogPath) {
        HANDLE stderrFile = INVALID_HANDLE_VALUE;
        if (!stderrLogPath.empty()) {
            SECURITY_ATTRIBUTES fsa{};
            fsa.nLength = sizeof(fsa);
            fsa.bInheritHandle = TRUE;
            stderrFile = CreateFileW(stderrLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                     &fsa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (stderrFile == INVALID_HANDLE_VALUE) return false;
        }

        HANDLE inheritHandles[2];
        DWORD inheritCount = 0;
        inheritHandles[inheritCount++] = m_stdinRead;
        if (stderrFile != INVALID_HANDLE_VALUE) inheritHandles[inheritCount++] = stderrFile;

        AttributeList attrs;
        if (!attrs.init(inheritHandles, inheritCount)) {
            if (stderrFile != INVALID_HANDLE_VALUE) CloseHandle(stderrFile);
            return false;
        }

        STARTUPINFOEXW siex{};
        siex.StartupInfo.cb = sizeof(siex);
        siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        siex.StartupInfo.hStdInput = m_stdinRead;
        siex.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        siex.StartupInfo.hStdError = (stderrFile != INVALID_HANDLE_VALUE)
            ? stderrFile : GetStdHandle(STD_ERROR_HANDLE);
        siex.StartupInfo.wShowWindow = SW_HIDE;
        siex.lpAttributeList = attrs.list;

        BOOL ok = CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr, nullptr, &siex.StartupInfo, &m_pi
        );

        CloseHandle(m_stdinRead);
        m_stdinRead = nullptr;
        if (stderrFile != INVALID_HANDLE_VALUE) CloseHandle(stderrFile);

        return ok != FALSE;
    }

    void attachToJob() {
        m_job = CreateJobObjectW(nullptr, nullptr);
        if (!m_job) return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(m_job, m_pi.hProcess);
    }

    void releaseAll() noexcept {
        if (m_stdinWrite) { CloseHandle(m_stdinWrite); m_stdinWrite = nullptr; }
        if (m_stdinRead) { CloseHandle(m_stdinRead); m_stdinRead = nullptr; }
        if (m_pi.hProcess) { CloseHandle(m_pi.hProcess); m_pi.hProcess = nullptr; }
        if (m_pi.hThread) { CloseHandle(m_pi.hThread); m_pi.hThread = nullptr; }
        if (m_job) { CloseHandle(m_job); m_job = nullptr; }
    }
};

#endif
