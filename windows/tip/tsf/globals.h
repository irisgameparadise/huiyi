// 會意注音 (Huiyi, based on AquaType): the text service for Windows (TSF): identities and module-wide state.  The structure follows Microsoft's
// SampleIME (Windows-classic-samples, MIT); the composition logic is windows/tip/composer.*.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msctf.h>
#include <atomic>

namespace aqtsf {

// {A532764A-4ED6-44D9-A303-F8EA759DACA6}
inline constexpr CLSID CLSID_Huiyi = {0xa532764a, 0x4ed6, 0x44d9, {0xa3, 0x03, 0xf8, 0xea, 0x75, 0x9d, 0xac, 0xa6}};
// {35C74BD7-A144-441B-AD5F-C8E0D321969A}
inline constexpr GUID GUID_Profile = {0x35c74bd7, 0xa144, 0x441b, {0xad, 0x5f, 0xc8, 0xe0, 0xd3, 0x21, 0x96, 0x9a}};
// {89E12845-5755-4449-BD66-8437BB0003F9}: the composition's display attribute (a dotted underline)
inline constexpr GUID GUID_DisplayAttributeInput = {0x89e12845, 0x5755, 0x4449, {0xbd, 0x66, 0x84, 0x37, 0xbb, 0x00, 0x03, 0xf9}};
// {E3131275-C1B2-4533-AD77-50B8BF89ACE9}: Shift released on its own (the preserved key that switches 中/英)
inline constexpr GUID GUID_PreservedShift = {0xe3131275, 0xc1b2, 0x4533, {0xad, 0x77, 0x50, 0xb8, 0xbf, 0x89, 0xac, 0xe9}};
inline constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);   // zh-TW
inline constexpr wchar_t kDescription[] = L"會意注音";

extern HINSTANCE g_module;
extern std::atomic<long> g_locks;      // objects alive + server locks: DllCanUnloadNow
inline void lock_module() { g_locks++; }
inline void unlock_module() { g_locks--; }

}  // namespace aqtsf
