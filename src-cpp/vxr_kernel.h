#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define VXR_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define VXR_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Scans `prompt` for jailbreak heuristics and returns a JSON payload:
// {"is_safe":bool,"threat_level":int,"flagged_reason":string}
// The returned pointer is valid until the next call to analyze_prompt.
VXR_EXPORT const char* analyze_prompt(const char* prompt);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <cstddef>
#include <string_view>

struct VXRAnalysisResult {
    bool is_safe;
    int threat_level;
    const char* flagged_reason;
};

// Pure C++ entry point (no JSON serialization). Useful for native unit tests.
VXRAnalysisResult vxr_analyze_prompt(std::string_view prompt);

#endif
