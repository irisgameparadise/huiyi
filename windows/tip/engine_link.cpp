#include "engine_link.h"
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <vector>

namespace aqtip {
namespace {
std::vector<uint8_t> token_info(TOKEN_INFORMATION_CLASS c) {
    HANDLE tok = nullptr; std::vector<uint8_t> buf;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return buf;
    DWORD len = 0; GetTokenInformation(tok, c, nullptr, 0, &len);
    if (len) { buf.resize(len); if (!GetTokenInformation(tok, c, buf.data(), len, &len)) buf.clear(); }
    CloseHandle(tok); return buf;
}
// The engine runs with the rights of the app that starts it, so only an app at the user's ordinary level starts it: not
// an elevated one (the engine would run as administrator), nor a low-integrity or sandboxed one.  Those apps connect to
// the engine the user's other apps (or the sign-in start) brought up.
bool may_start_engine() {
    const auto elev = token_info(TokenElevation);
    if (elev.size() < sizeof(TOKEN_ELEVATION) || ((TOKEN_ELEVATION*)elev.data())->TokenIsElevated) return false;
    const auto il = token_info(TokenIntegrityLevel);
    if (il.empty()) return false;
    PSID sid = ((TOKEN_MANDATORY_LABEL*)il.data())->Label.Sid;
    const DWORD rid = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    return rid >= SECURITY_MANDATORY_MEDIUM_RID && rid < SECURITY_MANDATORY_HIGH_RID;
}
}  // namespace

std::wstring PipeEngine::default_pipe_name() {
    const auto user = token_info(TokenUser); LPWSTR sid = nullptr;
    if (user.empty() || !ConvertSidToStringSidW(((TOKEN_USER*)user.data())->User.Sid, &sid)) return {};   // no name: no engine
    std::wstring name = std::wstring(L"\\\\.\\pipe\\HuiyiEngine-") + sid; LocalFree(sid); return name;
}

// The pipe must be the engine's: owned by this user (another account could create a pipe of this name first and read
// the keys), and, where this app may look at the process serving it, that process must be the engine beside us.
bool PipeEngine::trusted(HANDLE h) const {
    const auto user = token_info(TokenUser); if (user.empty()) return false;
    PSID owner = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(h, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS) return false;
    const bool mine = owner && EqualSid(owner, ((TOKEN_USER*)user.data())->User.Sid); LocalFree(sd);
    if (!mine) return false;
    ULONG pid = 0; if (launch_.exe.empty() || !GetNamedPipeServerProcessId(h, &pid)) return true;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return true;                                                      // a sandboxed app may not look; the owner is checked
    wchar_t path[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
    const bool same = QueryFullProcessImageNameW(proc, 0, path, &n) && CompareStringOrdinal(path, (int)n, launch_.exe.c_str(), (int)launch_.exe.size(), TRUE) == CSTR_EQUAL;
    CloseHandle(proc); return same;
}

EngineLaunch default_launch(HMODULE self) {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir = path; dir = dir.substr(0, dir.find_last_of(L'\\'));
    EngineLaunch l; l.exe = dir + L"\\HuiyiEngine.exe";
    if (GetFileAttributesW(l.exe.c_str()) == INVALID_FILE_ATTRIBUTES) {      // a 32-bit copy of the text service sits in a subfolder
        std::wstring up = dir.substr(0, dir.find_last_of(L'\\'));
        if (GetFileAttributesW((up + L"\\HuiyiEngine.exe").c_str()) != INVALID_FILE_ATTRIBUTES) { dir = up; l.exe = dir + L"\\HuiyiEngine.exe"; }
    }
    std::wstring log; PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) { log = std::wstring(local) + L"\\Huiyi"; CreateDirectoryW(log.c_str(), nullptr); log += L"\\engine.log"; CoTaskMemFree(local); }
    const std::wstring d = dir + L"\\data";
    l.args = L"--command serve-pipe --decoder-dir \"" + d + L"\\decoder\" --char-to-keys \"" + d + L"\\char_to_keys.json\" --fallback-aqds \"" + d +
             L"\\dict_store\\dictionary_fallback.v1.aqds\" --fallback-manifest \"" + d + L"\\dict_store\\dictionary_fallback.v1.aqds.manifest.json\"";
    if (!log.empty()) l.args += L" --log \"" + log + L"\"";
    return l;
}

PipeEngine::PipeEngine(std::wstring pipe, EngineLaunch launch) : pipe_name_(std::move(pipe)), launch_(std::move(launch)), may_start_(may_start_engine()) {}

// The pipe is opened for overlapped I/O: a synchronous handle serialises a write behind the reader's pending read, and
// the read only completes when the engine answers the request that write is carrying.
PipeEngine::~PipeEngine() {
    HANDLE p; { std::lock_guard<std::mutex> g(m_); p = pipe_; pipe_ = INVALID_HANDLE_VALUE; notify_hwnd_ = nullptr; closing_ = true; }
    SetEvent(stop_);
    if (reader_.joinable()) reader_.join();
    if (p != INVALID_HANDLE_VALUE) CloseHandle(p);
    CloseHandle(stop_);
}
bool PipeEngine::connect(uint32_t wait_ms) {
    if (pipe_ != INVALID_HANDLE_VALUE) return true;
    if (pipe_name_.empty()) return false;                                        // no engine (the sign-in and lock screens)
    // A connection that just failed is not tried again for a few seconds: the keys of an app that can neither reach
    // nor start the engine must not each wait for it.
    if (GetTickCount64() - last_failed_ < 3000) return false;
    const ULONGLONG deadline = GetTickCount64() + wait_ms;
    while (true) {
        // SECURITY_IDENTIFICATION: whatever serves the pipe may learn who we are, never act as us
        HANDLE h = CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE && !trusted(h)) { CloseHandle(h); last_failed_ = GetTickCount64(); return false; }
        if (h != INVALID_HANDLE_VALUE) {
            if (reader_.joinable()) reader_.join();                              // the previous connection's reader has ended with it
            ResetEvent(stop_);
            { std::lock_guard<std::mutex> g(m_); pipe_ = h; }
            reader_ = std::thread(&PipeEngine::reader, this, h); return true;
        }
        const DWORD e = GetLastError();
        if (e == ERROR_PIPE_BUSY) { if (!WaitNamedPipeW(pipe_name_.c_str(), 200)) {} }
        else if (e == ERROR_FILE_NOT_FOUND && !launch_.exe.empty() && may_start_ && GetTickCount64() - last_launch_ > 10000) {
            // Not running: start it, at most once every ten seconds.  An app sandboxed so that it cannot start
            // processes simply finds no engine and composes bopomofo until one is running.
            last_launch_ = GetTickCount64();
            std::wstring cmd = L"\"" + launch_.exe + L"\" " + launch_.args; STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
            if (CreateProcessW(launch_.exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi)
                || CreateProcessW(launch_.exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
        }
        if (GetTickCount64() >= deadline) { last_failed_ = GetTickCount64(); return false; }
        Sleep(50);
    }
}

void PipeEngine::reader(HANDLE pipe) {
    std::string pending; char buf[16384]; DWORD got = 0;
    OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // Stopping waits on an event rather than cancelling from outside: a cancel sent while no read is pending (a reply
    // being handled) would be lost, and the next read would then wait forever.
    const HANDLE wait[2] = {ov.hEvent, stop_};
    while (true) {
        ResetEvent(ov.hEvent);
        if (!ReadFile(pipe, buf, sizeof buf, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;
        if (WaitForMultipleObjects(2, wait, FALSE, INFINITE) != WAIT_OBJECT_0) { CancelIoEx(pipe, &ov); GetOverlappedResult(pipe, &ov, &got, TRUE); break; }
        if (!GetOverlappedResult(pipe, &ov, &got, FALSE) || got == 0) break;
        pending.append(buf, got); size_t nl; bool any = false;
        { std::lock_guard<std::mutex> g(m_);
          while ((nl = pending.find('\n')) != std::string::npos) { lines_.push_back(pending.substr(0, nl)); pending.erase(0, nl + 1); any = true; }
          if (any && notify_hwnd_) PostMessageW(notify_hwnd_, notify_msg_, 0, 0); }
        if (any) cv_.notify_all();
    }
    CloseHandle(ov.hEvent);
    bool close;
    { std::lock_guard<std::mutex> g(m_); if (pipe_ == pipe) pipe_ = INVALID_HANDLE_VALUE; close = !closing_; }   // the engine went away: the next send reconnects (or restarts it)
    if (close) CloseHandle(pipe);                                                // on shutdown the destructor closes it
    cv_.notify_all();
}

bool PipeEngine::send(const std::string& line) {
    HANDLE p;
    { std::lock_guard<std::mutex> g(m_); p = pipe_; }
    if (p == INVALID_HANDLE_VALUE) { if (!connect(1000)) return false; std::lock_guard<std::mutex> g(m_); p = pipe_; }
    OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); DWORD w = 0;
    bool ok = WriteFile(p, line.data(), (DWORD)line.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING;
    if (ok) { if (WaitForSingleObject(ov.hEvent, 2000) != WAIT_OBJECT_0) { CancelIoEx(p, &ov); ok = false; } GetOverlappedResult(p, &ov, &w, TRUE); }
    CloseHandle(ov.hEvent);
    if (!ok || w != line.size()) { std::lock_guard<std::mutex> g(m_); if (pipe_ == p) { pipe_ = INVALID_HANDLE_VALUE; SetEvent(stop_); } return false; }   // the reader ends and closes it
    return true;
}
bool PipeEngine::next_line(std::string* line, uint32_t wait_ms) {
    std::unique_lock<std::mutex> g(m_);
    if (lines_.empty() && wait_ms > 0) cv_.wait_for(g, std::chrono::milliseconds(wait_ms), [&] { return !lines_.empty() || pipe_ == INVALID_HANDLE_VALUE; });
    if (lines_.empty()) return false;
    *line = std::move(lines_.front()); lines_.pop_front(); return true;
}

}  // namespace aqtip
