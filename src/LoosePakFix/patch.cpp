#include "patch.h"
#include <cstring>

namespace tchia {
PatchResult Apply(void* module, std::size_t available) {
    auto* image = static_cast<unsigned char*>(module);
    if (!image || available < sizeof(IMAGE_DOS_HEADER)) return PatchResult::WrongImage;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > available)
        return PatchResult::WrongImage;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage != kImageSize || nt->FileHeader.TimeDateStamp != kTimestamp ||
        available < kImageSize) return PatchResult::WrongImage;
    auto debug = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!debug.VirtualAddress || debug.Size < sizeof(IMAGE_DEBUG_DIRECTORY) ||
        static_cast<std::size_t>(debug.VirtualAddress) + debug.Size > available)
        return PatchResult::WrongSymbols;
    const GUID expected = {0xa20b7c2d, 0x46a3, 0x4de6, {0xb6, 0xbb, 0xa8, 0x44, 0x1e, 0x59, 0xb1, 0x8b}};
    bool matched = false;
    auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(image + debug.VirtualAddress);
    for (std::size_t i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
        const auto& item = entries[i];
        if (item.Type != IMAGE_DEBUG_TYPE_CODEVIEW || item.SizeOfData < 24 ||
            static_cast<std::size_t>(item.AddressOfRawData) + item.SizeOfData > available) continue;
        auto* data = image + item.AddressOfRawData;
        DWORD age;
        std::memcpy(&age, data + 20, sizeof(age));
        if (std::memcmp(data, "RSDS", 4) == 0 && std::memcmp(data + 4, &expected, sizeof(expected)) == 0 && age == 1)
            matched = true;
    }
    if (!matched) return PatchResult::WrongSymbols;
    auto* function = image + kFunctionRva;
    for (std::size_t i = 0; i < sizeof(kExpectedFunction); ++i) {
        if (i == kPatchOffset && function[i] == 0xeb) continue;
        if (function[i] != kExpectedFunction[i]) return PatchResult::WrongCode;
    }
    if (function[kPatchOffset] == 0xeb) return PatchResult::AlreadyApplied;
    DWORD protection;
    if (!VirtualProtect(image + kPatchRva, 1, PAGE_EXECUTE_READWRITE, &protection))
        return PatchResult::ProtectFailed;
    // JE empty_set -> JMP empty_set. The native EBX=-1, allowed flag,
    // ini handling and filename-security delegate all retain their code.
    image[kPatchRva] = 0xeb;
    BOOL flushed = FlushInstructionCache(GetCurrentProcess(), image + kPatchRva, 1);
    DWORD ignored;
    BOOL restored = VirtualProtect(image + kPatchRva, 1, protection, &ignored);
    return flushed && restored ? PatchResult::Applied : PatchResult::ProtectFailed;
}

const char* Describe(PatchResult result) {
    switch (result) {
    case PatchResult::Applied: return "APPLIED: loose cooked assets remain readable with pak overrides\r\n";
    case PatchResult::AlreadyApplied: return "APPLIED: patch already present\r\n";
    case PatchResult::WrongImage: return "REFUSED: executable version or image size differs\r\n";
    case PatchResult::WrongSymbols: return "REFUSED: CodeView GUID/age differs from supported PDB\r\n";
    case PatchResult::WrongCode: return "REFUSED: native function preimage differs\r\n";
    default: return "REFUSED: executable memory protection or cache flush failed\r\n";
    }
}
}
