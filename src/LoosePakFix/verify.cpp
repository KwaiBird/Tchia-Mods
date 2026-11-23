#include "patch.h"
#include <x3daudio.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static std::vector<unsigned char> Read(const wchar_t* filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    Require(file.good(), "Cannot open copied executable");
    auto length = file.tellg();
    Require(length > 0, "Empty executable");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    Require(file.good(), "Cannot read executable");
    return bytes;
}

static void VerifyImage(const std::vector<unsigned char>& raw) {
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, tchia::kImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Require(image != nullptr, "Cannot allocate mapped-image fixture");
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    Require(dos->e_magic == IMAGE_DOS_SIGNATURE, "Bad copied EXE header");
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dos->e_lfanew);
    Require(nt->OptionalHeader.SizeOfImage == tchia::kImageSize, "Unexpected image size");
    std::memcpy(image, raw.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        Require(static_cast<std::size_t>(section[i].VirtualAddress) + section[i].SizeOfRawData <= tchia::kImageSize, "Section exceeds image");
        Require(static_cast<std::size_t>(section[i].PointerToRawData) + section[i].SizeOfRawData <= raw.size(), "Section exceeds file");
        std::memcpy(image + section[i].VirtualAddress, raw.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    }
    auto* mapped = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    ++mapped->FileHeader.TimeDateStamp;
    Require(tchia::Apply(image, tchia::kImageSize) == tchia::PatchResult::WrongImage, "Wrong build was accepted");
    --mapped->FileHeader.TimeDateStamp;
    auto debug = mapped->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    auto* entries = reinterpret_cast<IMAGE_DEBUG_DIRECTORY*>(image + debug.VirtualAddress);
    unsigned char* guid = nullptr;
    for (std::size_t i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i)
        if (entries[i].Type == IMAGE_DEBUG_TYPE_CODEVIEW) guid = image + entries[i].AddressOfRawData + 4;
    Require(guid != nullptr, "Missing CodeView fixture");
    *guid ^= 1;
    Require(tchia::Apply(image, tchia::kImageSize) == tchia::PatchResult::WrongSymbols, "Wrong PDB GUID was accepted");
    *guid ^= 1;
    image[tchia::kFunctionRva] ^= 1;
    Require(tchia::Apply(image, tchia::kImageSize) == tchia::PatchResult::WrongCode, "Changed native code was accepted");
    image[tchia::kFunctionRva] ^= 1;
    std::vector<unsigned char> before(image, image + tchia::kImageSize);
    DWORD previous;
    Require(VirtualProtect(image, tchia::kImageSize, PAGE_EXECUTE_READ, &previous) != FALSE, "Cannot protect image fixture");
    Require(tchia::Apply(image, tchia::kImageSize) == tchia::PatchResult::Applied, "Supported image patch failed");
    std::size_t changes = 0;
    for (std::size_t i = 0; i < before.size(); ++i) if (before[i] != image[i]) {
        ++changes;
        Require(i == tchia::kPatchRva && before[i] == 0x74 && image[i] == 0xeb, "Unexpected image change");
    }
    Require(changes == 1, "Patch must change exactly one byte");
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(image + tchia::kPatchRva, &memory, sizeof(memory));
    Require(memory.Protect == PAGE_EXECUTE_READ, "Code protection not restored");
    Require(tchia::Apply(image, tchia::kImageSize) == tchia::PatchResult::AlreadyApplied, "Patch is not idempotent");
    VirtualFree(image, 0, MEM_RELEASE);
}

static void VerifyNativeGate() {
    // Execute the actual native set-lookup/allowed-flag block in isolation.
    // Only its FName hash and unused EndsWith calls are stubbed. TLS/delegate
    // initialization is outside this block and is not claimed as game-tested.
    constexpr unsigned begin = 0x02507f69, end = 0x02507ff6;
    const unsigned char prefix[] = {0x53,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x68,
                                    0x48,0x8b,0xf1,0x48,0x89,0x54,0x24,0x50};
    const unsigned char suffix[] = {0x40,0x0f,0xb6,0xc7,0x48,0x83,0xc4,0x68,0x41,0x5e,0x5f,0x5e,0x5b,0xc3};
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Require(code != nullptr, "Cannot allocate gate fixture");
    std::memcpy(code, prefix, sizeof(prefix));
    std::memcpy(code + sizeof(prefix), tchia::kExpectedFunction + begin - tchia::kFunctionRva, end - begin);
    std::size_t tail = sizeof(prefix) + end - begin;
    std::memcpy(code + tail, suffix, sizeof(suffix));
    std::size_t stub = tail + sizeof(suffix);
    code[stub] = 0x31; code[stub + 1] = 0xc0; code[stub + 2] = 0xc3;
    for (unsigned call : {0x02507f82u, 0x02507ff1u}) {
        std::size_t offset = sizeof(prefix) + call - begin;
        Require(code[offset] == 0xe8, "Gate call fixture differs");
        auto displacement = static_cast<std::int32_t>(stub - offset - 5);
        std::memcpy(code + offset + 1, &displacement, sizeof(displacement));
    }
    auto gate = reinterpret_cast<bool(*)(void*, unsigned long long)>(code);
    alignas(16) unsigned char object[0x100]{}, element[16]{};
    void* elements = element;
    std::memcpy(object + 0x60, &elements, sizeof(elements));
    int count = 1, bucket = 0, buckets = 1, next = -1;
    std::memcpy(object + 0x68, &count, 4);
    std::memcpy(object + 0x98, &bucket, 4);
    std::memcpy(object + 0xa8, &buckets, 4);
    std::memcpy(element + 8, &next, 4);
    DWORD previous;
    VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous);
    for (unsigned long long key = 1; key <= 6; ++key) {
        std::memcpy(element, &key, 8);
        Require(!gate(object, key), "Original excluded-extension gate did not reject");
        Require(gate(object, 42), "Original unrelated extension was rejected");
    }
    VirtualProtect(code, 4096, PAGE_EXECUTE_READWRITE, &previous);
    code[sizeof(prefix) + tchia::kPatchRva - begin] = 0xeb;
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous);
    for (unsigned long long key = 1; key <= 6; ++key) {
        std::memcpy(element, &key, 8);
        Require(gate(object, key), "Patched cooked-extension gate still rejects");
        Require(gate(object, 42), "Patched unrelated extension changed");
    }
    VirtualFree(code, 0, MEM_RELEASE);
}

static void VerifyForwarding(const wchar_t* dll, bool failWorker) {
    wchar_t system[1024];
    UINT length = GetSystemDirectoryW(system, 900);
    Require(length && length < 900, "Cannot resolve System32");
    const wchar_t suffix[] = L"\\X3DAudio1_7.dll";
    std::memcpy(system + length, suffix, sizeof(suffix));
    HMODULE original = LoadLibraryExW(system, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE proxy = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    Require(original && proxy && original != proxy, "Cannot load distinct system/proxy audio modules");
    using Initialize = void(WINAPI*)(UINT32, float, BYTE*);
    using Calculate = void(WINAPI*)(const BYTE*, const X3DAUDIO_LISTENER*, const X3DAUDIO_EMITTER*, UINT32, X3DAUDIO_DSP_SETTINGS*);
    auto init = reinterpret_cast<Initialize>(GetProcAddress(original, "X3DAudioInitialize"));
    auto calc = reinterpret_cast<Calculate>(GetProcAddress(original, "X3DAudioCalculate"));
    auto initProxy = reinterpret_cast<Initialize>(GetProcAddress(proxy, "X3DAudioInitialize"));
    auto calcProxy = reinterpret_cast<Calculate>(GetProcAddress(proxy, "X3DAudioCalculate"));
    Require(init && calc && initProxy && calcProxy, "Missing proxy exports");
    Require(GetProcAddress(proxy, MAKEINTRESOURCEA(1)) == reinterpret_cast<FARPROC>(calcProxy), "Ordinal 1 differs");
    Require(GetProcAddress(proxy, MAKEINTRESOURCEA(2)) == reinterpret_cast<FARPROC>(initProxy), "Ordinal 2 differs");
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        for (unsigned iteration = 0; iteration < 100; ++iteration) {
            X3DAUDIO_HANDLE direct{}, forwarded{};
            float speed = 343.5f + worker;
            init(SPEAKER_STEREO, speed, direct);
            initProxy(SPEAKER_STEREO, speed, forwarded);
            if (std::memcmp(direct, forwarded, sizeof(direct)) != 0) ++failures;
            X3DAUDIO_LISTENER listener{};
            listener.OrientFront.z = 1; listener.OrientTop.y = 1;
            X3DAUDIO_EMITTER emitter{};
            emitter.OrientFront.z = 1; emitter.OrientTop.y = 1;
            emitter.Position.x = 2; emitter.Position.z = 4;
            emitter.ChannelCount = 1; emitter.CurveDistanceScaler = 1; emitter.DopplerScaler = 1;
            float matrixA[2]{}, matrixB[2]{};
            X3DAUDIO_DSP_SETTINGS a{}, b{};
            a.SrcChannelCount = b.SrcChannelCount = 1;
            a.DstChannelCount = b.DstChannelCount = 2;
            a.pMatrixCoefficients = matrixA; b.pMatrixCoefficients = matrixB;
            calc(direct, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &a);
            calcProxy(forwarded, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &b);
            for (unsigned channel = 0; channel < 2; ++channel)
                if (!std::isfinite(matrixB[channel]) || std::fabs(matrixA[channel] - matrixB[channel]) > 0.000001f) ++failures;
        }
        if (failWorker && worker == 0) ++failures;
    });
    for (auto& worker : workers) worker.join();
    Require(failures.load() == 0, "Audio forwarding worker failure");
    FreeLibrary(proxy);
    FreeLibrary(original);
}

int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc >= 3, "Usage: verify.exe <copied-game.exe> <absolute-proxy.dll> [--fail-worker]");
        auto raw = Read(argv[1]);
        VerifyImage(raw);
        VerifyNativeGate();
        VerifyForwarding(argv[2], argc >= 4 && lstrcmpW(argv[3], L"--fail-worker") == 0);
        std::cout << "{\"passed\":true,\"image_patch_bytes\":1,\"negative_image_checks\":3,\"native_gate_cases\":6,\"audio_worker_calls\":400}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
