// demo_render: the README's demo, made without anyone's screen.  The real composer types a script against the real
// engine; after each step it writes the state (text, composition, caret, the candidate window drawn by its own code)
// to <out>/states.jsonl and <out>/cand_NNN.png, on a timeline of its own.  windows/tools/demo_compose.py turns that
// into the animation.
//   demo_render <engine.exe> <payload dir> <char_to_keys.json> <out dir>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "candwin.h"
#include "composer.h"
#include "engine_link.h"
#include <nlohmann/json.hpp>
namespace aqtsf { HINSTANCE g_module = GetModuleHandleW(nullptr); std::atomic<long> g_locks{0}; }
using namespace aqtip;

namespace {
std::string u8(const std::wstring& w) { if (w.empty()) return {}; int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }
std::wstring wide(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w; }

struct Editor : Host {             // a plain text box: what the app would show
    std::wstring text, preedit; int caret = -1;
    void set_preedit(const std::wstring& t, int c) override { preedit = t; caret = c; }
    void commit(const std::wstring& t) override { text += t; preedit.clear(); caret = -1; }
    void beep() override {}
    std::string context_before_caret() override { return u8(text); }
    bool secure_input() override { return false; }
};

uint32_t vk_of(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A'; if (c >= '0' && c <= '9') return c;
    switch (c) { case ' ': return VK_SPACE; case ';': return VK_OEM_1; case ',': return VK_OEM_COMMA; case '-': return VK_OEM_MINUS; case '.': return VK_OEM_PERIOD; case '/': return VK_OEM_2; }
    return 0;
}

struct Recorder {
    Composer& c; Editor& ed; aqtsf::CandidateWindow& win; std::wstring out; std::ofstream log; long t = 0; int n = 0; std::string caption, keys;
    void snap(long hold) {
        nlohmann::json j = {{"t", t}, {"text", u8(ed.text)}, {"preedit", u8(ed.preedit)}, {"caret", ed.caret}, {"caption", caption}, {"keys", keys}};
        int off = 0, len = 0;
        if (c.panel_visible()) {
            const PanelView v = c.panel();
            if (v.cands && !v.rows.empty()) {
                wchar_t name[32]; swprintf(name, 32, L"cand_%03d.png", n++);
                win.render_png(v, (out + L"\\" + name).c_str(), 192, false);
                j["cand"] = u8(name);
                if (c.target_span(&off, &len)) { j["target"] = {off, len}; }
            }
        }
        log << j.dump() << "\n"; t += hold;
    }
    void settle() { for (int i = 0; i < 150; i++) { c.pump(); Sleep(20); } c.refresh_preedit(); }
    void key(uint32_t vk, bool shift = false, long hold = 130, const char* shown = nullptr) {
        Key k; k.vk = vk; k.shift = shift;
        if (c.key(k) == Result::Pass && vk == VK_RETURN) ed.text += L"\n";        // Enter with nothing composed: a new line
        for (int i = 0; i < 10; i++) { c.pump(); Sleep(15); } c.refresh_preedit();
        if (shown) keys += shown;
        snap(hold);
    }
    void type(const std::string& ks, const std::string& shown_keys) {   // shown: the bopomofo of each key, for the overlay
        std::vector<std::string> sym; for (size_t i = 0; i < shown_keys.size();) { size_t l = (shown_keys[i] & 0x80) ? ((shown_keys[i] & 0xE0) == 0xC0 ? 2 : (shown_keys[i] & 0xF0) == 0xE0 ? 3 : 4) : 1; sym.push_back(shown_keys.substr(i, l)); i += l; }
        for (size_t i = 0; i < ks.size(); i++) key(vk_of(ks[i]), false, 110, i < sym.size() ? sym[i].c_str() : nullptr);
    }
};
}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 5) { std::fprintf(stderr, "usage: demo_render <engine.exe> <payload dir> <char_to_keys.json> <out dir>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp); const std::wstring state = std::wstring(tmp) + L"huiyi-demo-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(state.c_str(), nullptr); DeleteFileW((state + L"\\personal_memory.v2.tsv").c_str());   // nothing learnt: what a new install does
    const std::wstring pipe = L"\\\\.\\pipe\\HuiyiEngine-demo-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring P = wide(argv[2]);
    std::wstring cmd = L"\"" + wide(argv[1]) + L"\" --command serve-pipe --pipe " + pipe + L" --decoder-dir \"" + P + L"\\decoder\" --char-to-keys \"" + wide(argv[3]) + L"\" --state-dir \"" + state +
                       L"\" --fallback-aqds \"" + P + L"\\dict_store\\dictionary_fallback.v1.aqds\" --fallback-manifest \"" + P + L"\\dict_store\\dictionary_fallback.v1.aqds.manifest.json\"";
    _putenv_s("AQ_FUZZY", Settings{}.fuzzy_pairs().c_str()); _putenv_s("AQ_LEXICON_WEIGHT", "4");   // the defaults, whatever this machine's settings
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { std::fprintf(stderr, "cannot start the engine\n"); return 1; }
    { HANDLE h; while ((h = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)) == INVALID_HANDLE_VALUE) Sleep(100); CloseHandle(h); }

    const std::wstring out = wide(argv[4]); CreateDirectoryW(out.c_str(), nullptr);
    Editor ed; PipeEngine engine(pipe, EngineLaunch{}); Composer c(ed, engine); aqtsf::CandidateWindow win;
    { Key warm; warm.vk = 'C'; c.key(warm); for (int i = 0; i < 300 && c.composing(); i++) { c.pump(); Sleep(20); } Key esc; esc.vk = VK_ESCAPE; c.key(esc); }   // the model loaded
    Recorder r{c, ed, win, out, std::ofstream(out + L"\\states.jsonl", std::ios::binary)};
    r.snap(300);
    // 1. a slip: ㄐㄧㄣ typed with one ㄣ too many
    r.caption = u8(L"打錯也讀得懂：「今」多按了一個 ㄣ");
    r.type("rupp wu0 wu0 fu4cp3cl3", u8(L"ㄐㄧㄣㄣˉㄊㄧㄢˉㄊㄧㄢˉㄑㄧˋㄏㄣˇㄏㄠˇ"));
    r.settle(); r.snap(900);
    r.key(VK_RETURN, false, 400, "⏎");
    r.key(VK_OEM_COMMA, true, 400, "，");
    // 2. an accent: 應該 typed as ㄧㄣ ㄍㄞ (因該)
    r.keys.clear(); r.caption = u8(L"發音容錯：打成「因該」也找得到「應該」");
    r.type("ji3ap7up e9 tj fm4y.3y.3", u8(L"ㄨㄛˇㄇㄣˊㄧㄣˉㄍㄞˉㄔㄨˉㄑㄩˋㄗㄡˇㄗㄡˇ"));
    r.settle(); r.snap(900);
    r.key(VK_RETURN, false, 300, "⏎");
    r.key(VK_OEM_PERIOD, true, 300, "。");
    r.key(VK_RETURN, false, 500, "⏎");
    // 3. going back: ← to 你, ↓ for the list, 妳
    r.keys.clear(); r.caption = u8(L"整句直出：← 回到前面的字，↓ 改選");
    r.type("su3cl3", u8(L"ㄋㄧˇㄏㄠˇ"));
    r.settle(); r.snap(700);
    r.key(VK_LEFT, false, 350, "←"); r.key(VK_LEFT, false, 350, "←");
    r.key(VK_DOWN, false, 0, "↓"); r.settle(); r.snap(700);
    { const PanelView v = c.panel(); int at = -1, hi = v.highlighted;
      for (int i = 0; v.cands && i < (int)v.cands->size(); i++) if ((*v.cands)[i].wtext == L"妳") { at = i; break; }
      if (at < 0) { std::fprintf(stderr, "no 妳 in the list\n"); }
      for (int i = hi; at > 0 && i < at; i++) r.key(VK_DOWN, false, 300, nullptr); }
    r.key(VK_RETURN, false, 600, "⏎");
    r.key(VK_RETURN, false, 400, "⏎");
    r.key('1', true, 1200, "！");
    TerminateProcess(pi.hProcess, 0); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    std::printf("%s\n", u8(L"text: " + ed.text).c_str());
    return 0;
}
