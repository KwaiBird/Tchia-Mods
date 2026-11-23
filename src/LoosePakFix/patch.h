#pragma once
#include <windows.h>
#include <cstddef>

namespace tchia {
constexpr DWORD kImageSize = 88223744;
constexpr DWORD kTimestamp = 1713985068;
constexpr DWORD kFunctionRva = 0x02507f10;
constexpr DWORD kPatchRva = 0x02507f77;
constexpr DWORD kPatchOffset = kPatchRva - kFunctionRva;
inline constexpr unsigned char kExpectedFunction[] = {
#include "expected_function.inc"
};
enum class PatchResult { Applied, AlreadyApplied, WrongImage, WrongSymbols, WrongCode, ProtectFailed };
PatchResult Apply(void* module, std::size_t available);
const char* Describe(PatchResult result);
}
