#include "patch.h"
#include "build_version.h"
#include <cstring>

extern "C" {
void* RealX3DAudioCalculate = nullptr;
void* RealX3DAudioInitialize = nullptr;
}
static INIT_ONCE gSystemOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK LoadSystemAudio(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[1024];
    UINT length = GetSystemDirectoryW(path, 900);
    if (!length || length >= 900) return FALSE;
    const wchar_t suffix[] = L"\\X3DAudio1_7.dll";
    std::memcpy(path + length, suffix, sizeof(suffix));
    // This resolver runs on the first forwarded audio call, outside DllMain.
    HMODULE library = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return FALSE;
    RealX3DAudioCalculate = reinterpret_cast<void*>(GetProcAddress(library, "X3DAudioCalculate"));
    RealX3DAudioInitialize = reinterpret_cast<void*>(GetProcAddress(library, "X3DAudioInitialize"));
    return RealX3DAudioCalculate && RealX3DAudioInitialize;
}

extern "C" void EnsureSystemAudio() {
    if (!InitOnceExecuteOnce(&gSystemOnce, LoadSystemAudio, nullptr, nullptr)) {
        OutputDebugStringA("TchiaLoosePakFix: cannot resolve system X3DAudio1_7.dll\n");
        TerminateProcess(GetCurrentProcess(), ERROR_MOD_NOT_FOUND);
    }
}

extern "C" const char* TchiaLoosePakFixBuild() { return TCHIA_BUILD_ID; }

static void WriteStatus(HMODULE self, const char* message) {
    wchar_t path[1024];
    DWORD length = GetModuleFileNameW(self, path, 1024);
    if (!length || length >= 1024) return;
    while (length && path[length - 1] != L'\\' && path[length - 1] != L'/') --length;
    const wchar_t name[] = L"TchiaLoosePakFix.log";
    if (length + sizeof(name) / sizeof(wchar_t) > 1024) return;
    std::memcpy(path + length, name, sizeof(name));
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    const char version[] = TCHIA_BUILD_ID "\r\n" TCHIA_SOURCE_COMMIT "\r\n";
    WriteFile(file, version, sizeof(version) - 1, &written, nullptr);
    WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    CloseHandle(file);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t filename[1024];
    DWORD length = GetModuleFileNameW(nullptr, filename, 1024);
    if (!length || length >= 1024) return FALSE;
    const wchar_t* basename = filename;
    for (DWORD i = 0; i < length; ++i) if (filename[i] == L'\\' || filename[i] == L'/') basename = filename + i + 1;
    if (lstrcmpiW(basename, L"ProjetCaillou-Win64-Shipping.exe") != 0) return TRUE;
    // The DLL is a direct PE import, so this runs before the game's entry point.
    // No worker thread, waiting or LoadLibrary is used under the loader lock.
    auto result = tchia::Apply(GetModuleHandleW(nullptr), tchia::kImageSize);
    WriteStatus(self, tchia::Describe(result));
    return result == tchia::PatchResult::Applied || result == tchia::PatchResult::AlreadyApplied;
}
