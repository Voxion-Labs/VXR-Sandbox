// Voxion eXperimental Research (VXR)
// Core Sandbox Kernel — Public API Header
// C++17 | WASM/Emscripten compatible

#pragma once

// ---------------------------------------------------------------------------
// WASM export macro
// ---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define VXR_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define VXR_EXPORT
#endif

// ---------------------------------------------------------------------------
// C linkage — exported WASM surface
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

/**
 * analyzePrompt — Primary WASM export (camelCase, matches JS contract).
 *
 * Performs layered cybersecurity heuristic analysis on `prompt`:
 *   1. Strict memory boundary enforcement via std::string_view.
 *   2. Shannon entropy check for obfuscated/base64 payloads.
 *   3. Special-token pattern matching (<|endoftext|>, system overrides...).
 *   4. Weighted keyword/phrase pattern table scan.
 *
 * Returns a pointer to a *static* JSON buffer valid until the next call.
 * Caller MUST NOT free this pointer.
 *
 * JSON schema:
 *   { "ThreatScore": <int 1-10>,
 *     "ThreatType":  <string>,
 *     "is_safe":     <bool>,
 *     "status":      <"safe"|"moderate"|"threat">,
 *     "flagged_reason": <string>  }
 *
 * Performance target: < 5 ms for prompts up to kMaxPromptBytes.
 */
VXR_EXPORT const char* analyzePrompt(const char* prompt);

/**
 * analyze_prompt — Legacy alias kept for backward compatibility with
 * existing JS glue code (app.js, unit tests).
 * Delegates directly to analyzePrompt().
 */
VXR_EXPORT const char* analyze_prompt(const char* prompt);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// C++ internal API — usable in native unit tests without JSON serialization
// ---------------------------------------------------------------------------
#ifdef __cplusplus

#include <cstddef>
#include <string_view>

namespace vxr {

/// Maximum accepted prompt length in bytes (hard upper bound).
/// Inputs exceeding this are rejected immediately — O(1) boundary check.
inline constexpr std::size_t kMaxPromptBytes = 65'536;   // 64 KiB

/// Threat level range.
inline constexpr int kThreatMin = 1;
inline constexpr int kThreatMax = 10;

/// High-entropy threshold above which a payload is flagged as obfuscated.
/// Shannon entropy of pure base64 ~= 6.0 bits/byte; typical English ~= 4.0.
inline constexpr double kEntropyThreshold = 5.4;

/**
 * Full analysis result returned by the C++ internal API.
 * All const char* fields point to static string literals — never freed.
 */
struct AnalysisResult {
    int         ThreatScore;    ///< 1-10 composite threat score
    const char* ThreatType;    ///< Primary threat category string
    bool        is_safe;       ///< true when ThreatScore < 4 and no pattern match
    const char* status;        ///< "safe" | "moderate" | "threat"
    const char* flagged_reason;///< Matched pattern reason key (may be "")
    double      entropy;       ///< Computed Shannon entropy of the input
};

/**
 * Pure C++ entry point — no JSON serialization overhead.
 * Safe to call from native unit tests and benchmarks.
 *
 * Enforces kMaxPromptBytes boundary: if prompt.size() > kMaxPromptBytes the
 * function returns immediately with ThreatScore = kThreatMax and
 * ThreatType = "oversized_input" without reading beyond the limit.
 */
AnalysisResult analyzePromptCpp(std::string_view prompt) noexcept;

/**
 * Computes the Shannon entropy (bits/byte) of a byte sequence.
 * Used internally to detect base64/obfuscated payloads.
 * Exposed here for unit-test access.
 */
double computeShannonEntropy(std::string_view data) noexcept;

}  // namespace vxr

// ---------------------------------------------------------------------------
// Legacy C++ shim — keeps old call-sites building without modification
// ---------------------------------------------------------------------------
struct VXRAnalysisResult {
    bool        is_safe;
    int         threat_level;
    const char* flagged_reason;
    const char* status;
};

/// Wraps vxr::analyzePromptCpp() for call-sites using the old struct type.
VXRAnalysisResult vxr_analyze_prompt(std::string_view prompt) noexcept;

#endif  // __cplusplus
