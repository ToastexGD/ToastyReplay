#pragma once

#ifdef GEODE_IS_WINDOWS

#include <windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace {
    inline std::wstring utf8ToWide(std::string_view text) {
        if (text.empty()) {
            return {};
        }

        int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (size <= 0) {
            return {};
        }

        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }
}

class Subprocess {
public:
    Subprocess() = default;

    Subprocess(const std::string& command) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        if (!CreatePipe(&m_stdinRead, &m_stdinWrite, &sa, 0)) return;
        SetHandleInformation(m_stdinWrite, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si = {};
        si.cb = sizeof(STARTUPINFOW);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdInput = m_stdinRead;
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.wShowWindow = SW_HIDE;

        std::wstring cmd = utf8ToWide(command);
        if (cmd.empty()) return;

        m_running = CreateProcessW(
            nullptr, cmd.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &m_pi
        );
    }

    ~Subprocess() {
        if (m_stdinRead) CloseHandle(m_stdinRead);
        if (m_stdinWrite) CloseHandle(m_stdinWrite);
        if (m_pi.hProcess) CloseHandle(m_pi.hProcess);
        if (m_pi.hThread) CloseHandle(m_pi.hThread);
    }

    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    Subprocess(Subprocess&& other) noexcept {
        m_stdinRead = other.m_stdinRead;
        m_stdinWrite = other.m_stdinWrite;
        m_pi = other.m_pi;
        m_running = other.m_running;
        other.m_stdinRead = nullptr;
        other.m_stdinWrite = nullptr;
        other.m_pi = {};
        other.m_running = false;
    }

    Subprocess& operator=(Subprocess&& other) noexcept {
        if (this != &other) {
            if (m_stdinRead) CloseHandle(m_stdinRead);
            if (m_stdinWrite) CloseHandle(m_stdinWrite);
            if (m_pi.hProcess) CloseHandle(m_pi.hProcess);
            if (m_pi.hThread) CloseHandle(m_pi.hThread);
            m_stdinRead = other.m_stdinRead;
            m_stdinWrite = other.m_stdinWrite;
            m_pi = other.m_pi;
            m_running = other.m_running;
            other.m_stdinRead = nullptr;
            other.m_stdinWrite = nullptr;
            other.m_pi = {};
            other.m_running = false;
        }
        return *this;
    }

    bool isRunning() const { return m_running; }

    void writeStdin(const uint8_t* data, size_t size) {
        if (!m_stdinWrite) return;
        DWORD written = 0;
        WriteFile(m_stdinWrite, data, static_cast<DWORD>(size), &written, nullptr);
    }

    int close() {
        if (m_stdinWrite) {
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
        m_running = false;
        return static_cast<int>(exitCode);
    }

private:
    HANDLE m_stdinRead = nullptr;
    HANDLE m_stdinWrite = nullptr;
    PROCESS_INFORMATION m_pi = {};
    bool m_running = false;
};

#endif
