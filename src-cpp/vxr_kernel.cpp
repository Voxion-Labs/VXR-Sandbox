// Voxion eXperimental Research (VXR)
// Core Sandbox Kernel (C++) — Cybersecurity Heuristics Engine
//
// Design goals:
//   * Zero heap allocation in the hot analysis path.
//   * Strict memory boundary enforcement via std::string_view.
//   * Shannon entropy detection for obfuscated / base64 payloads.
//   * Regex-free special-token pattern matching (no std::regex overhead).
//   * Weighted keyword / phrase table for common prompt-injection vectors.
//   * analyzePrompt() exported via EMSCRIPTEN_KEEPALIVE; target < 5 ms.

#include "vxr_kernel.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

// ============================================================================
// Internal implementation — anonymous namespace (no external linkage)
// ============================================================================
namespace {

// ---------------------------------------------------------------------------
// Buffer sizes
// ---------------------------------------------------------------------------
/// Output JSON buffer.  All fields are small static strings, so 768 bytes is
/// comfortably sufficient and avoids any dynamic allocation.
constexpr std::size_t kResultBufferSize = 768;

/// Per-call scratchpad for case-folded copy of the input prompt.
/// Sized to vxr::kMaxPromptBytes so we never overflow.
constexpr std::size_t kScratchSize = vxr::kMaxPromptBytes;

// ---------------------------------------------------------------------------
// Static storage (lives for the duration of the Wasm module instance)
// ---------------------------------------------------------------------------
char g_result_buffer[kResultBufferSize];
char g_scratch[kScratchSize];

// ---------------------------------------------------------------------------
// Heuristic pattern table
// ---------------------------------------------------------------------------
struct HeuristicPattern {
    std::string_view text;
    int              threat_level;
    const char*      reason;
    bool             whole_word;
};

/// Special-token patterns — model boundary markers and system-prompt tokens
/// that have no legitimate presence in user-generated text.
constexpr HeuristicPattern kSpecialTokens[] = {
    // OpenAI / GPT family boundary tokens
    {"<|endoftext|>",          10, "special_token_injection",  false},
    {"<|im_start|>",           10, "special_token_injection",  false},
    {"<|im_end|>",             10, "special_token_injection",  false},
    {"<|system|>",             10, "special_token_injection",  false},
    {"<|user|>",                9, "special_token_injection",  false},
    {"<|assistant|>",           9, "special_token_injection",  false},
    // Llama / Meta family
    {"<s>",                     8, "special_token_injection",  false},
    {"</s>",                    8, "special_token_injection",  false},
    {"[inst]",                  8, "special_token_injection",  false},
    {"[/inst]",                 8, "special_token_injection",  false},
    {"<<sys>>",                10, "special_token_injection",  false},
    {"<</sys>>",               10, "special_token_injection",  false},
    // Mistral / Mixtral
    {"[system]",                9, "special_token_injection",  false},
    {"[/system]",               9, "special_token_injection",  false},
    // Claude / Anthropic
    {"\n\nhuman:",              8, "special_token_injection",  false},
    {"\n\nassistant:",          8, "special_token_injection",  false},
    // Generic role-injection via markdown fencing
    {"```system",               9, "special_token_injection",  false},
    {"```prompt",               8, "special_token_injection",  false},
};

/// System-prompt override and instruction-hijacking patterns.
constexpr HeuristicPattern kOverridePatterns[] = {
    {"ignore previous instructions",   9, "ignore_previous_instructions", false},
    {"ignore all previous",            9, "ignore_previous_instructions", false},
    {"ignore prior instructions",      9, "ignore_previous_instructions", false},
    {"disregard previous instructions",8, "disregard_instructions",       false},
    {"disregard all prior",            8, "disregard_instructions",       false},
    {"forget your instructions",       8, "forget_instructions",          false},
    {"forget all instructions",        8, "forget_instructions",          false},
    {"new instructions:",              8, "instruction_override",         false},
    {"updated instructions:",          8, "instruction_override",         false},
    {"your new task is",               7, "instruction_override",         false},
    {"your real instructions",         8, "instruction_override",         false},
    {"system override",               10, "system_override",              false},
    {"override system prompt",        10, "system_override",              false},
    {"override the system",           10, "system_override",              false},
    {"print system prompt",            9, "prompt_exfiltration",          false},
    {"reveal system prompt",           9, "prompt_exfiltration",          false},
    {"show system prompt",             9, "prompt_exfiltration",          false},
    {"print your instructions",        8, "prompt_exfiltration",          false},
    {"output your instructions",       8, "prompt_exfiltration",          false},
    {"what are your instructions",     7, "prompt_exfiltration",          false},
    {"bypass security",                9, "bypass_security",              false},
    {"bypass safety",                  9, "bypass_security",              false},
    {"bypass filter",                  8, "bypass_attempt",               false},
    {"bypass",                         7, "bypass_attempt",               false},
};

/// Persona-hijacking and jailbreak patterns.
constexpr HeuristicPattern kJailbreakPatterns[] = {
    {"do anything now",    9, "dan_jailbreak",       false},
    {"dan mode",           8, "dan_jailbreak",       false},
    {"enable dan",         8, "dan_jailbreak",       false},
    {"dan",                8, "dan_jailbreak",        true},
    {"jailbreak",          8, "jailbreak_keyword",   false},
    {"developer mode",     7, "developer_mode",      false},
    {"dev mode enabled",   7, "developer_mode",      false},
    {"god mode",           8, "god_mode",             false},
    {"unrestricted mode",  8, "restriction_removal", false},
    {"no restrictions",    7, "restriction_removal", false},
    {"without restrictions",7,"restriction_removal", false},
    {"without any restrictions",8,"restriction_removal",false},
    {"you are a",          5, "persona_redefinition", false},
    {"you are now a",      6, "persona_redefinition", false},
    {"act as if you are",  6, "persona_redefinition", false},
    {"pretend you are",    6, "persona_redefinition", false},
    {"pretend to be",      6, "persona_redefinition", false},
    {"roleplay as",        5, "persona_redefinition", false},
    {"simulate being",     5, "persona_redefinition", false},
    {"act as a",           5, "persona_redefinition", false},
    {"you must act as",    6, "persona_redefinition", false},
};

/// Sensitive / harmful content keywords.
constexpr HeuristicPattern kSensitivePatterns[] = {
    {"nsfw",             9, "sensitive_content", true},
    {"pornographic",     9, "sensitive_content", false},
    {"pornography",      9, "sensitive_content", false},
    {"porn",             9, "sensitive_content", true},
    {"explicit violence",10,"sensitive_content", false},
    {"extreme violence", 10,"sensitive_content", false},
    {"gore",            10, "sensitive_content", true},
    {"erotic",           8, "sensitive_content", false},
    {"nudity",           8, "sensitive_content", true},
    {"adult content",    8, "sensitive_content", false},
    {"hentai",           9, "sensitive_content", false},
    {"sex",              8, "sensitive_content", true},
    {"rape",            10, "sensitive_content", true},
    {"murder",          10, "sensitive_content", true},
    {"kill people",     10, "sensitive_content", false},
    {"genocide",        10, "sensitive_content", false},
    {"suicide",         10, "sensitive_content", false},
    {"bomb",            10, "sensitive_content", true},
    {"terrorist",       10, "sensitive_content", false},
    {"terrorism",       10, "sensitive_content", false},
    {"weapon",           8, "sensitive_content", true},
    {"cyberattack",      8, "sensitive_content", false},
    {"cyber-attack",     8, "sensitive_content", false},
    {"exploit vulnerability",9,"sensitive_content",false},
    {"malware",          9, "sensitive_content", false},
    {"phishing",         8, "sensitive_content", false},
    {"ransomware",       9, "sensitive_content", false},
    {"hacking",          8, "sensitive_content", false},
    {"hack",             8, "sensitive_content", true},
};

// ---------------------------------------------------------------------------
// Utility functions — all inlined, branch-free where possible
// ---------------------------------------------------------------------------

/// Branchless ASCII lower-case fold (portable, no locale).
[[nodiscard]] inline constexpr char toLowerAscii(char c) noexcept {
    // Bit 0x20 converts upper to lower for A-Z.
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 0x20) : c;
}

[[nodiscard]] inline bool isWordChar(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] inline bool ciEqualChar(char a, char b) noexcept {
    return toLowerAscii(a) == toLowerAscii(b);
}

[[nodiscard]] inline int clampThreat(int level) noexcept {
    if (level < vxr::kThreatMin) return vxr::kThreatMin;
    if (level > vxr::kThreatMax) return vxr::kThreatMax;
    return level;
}

// ---------------------------------------------------------------------------
// Case-insensitive substring search (Boyer-Moore-Horspool lite)
//
// For the small needles in our pattern tables, a simple linear scan is fast
// enough and avoids the preprocessing cost of a full BMH table.  The
// compiler auto-vectorises the inner loop under -O3.
// ---------------------------------------------------------------------------

/// Returns true if `haystack` contains `needle` (case-insensitive).
[[nodiscard]] bool ciContains(std::string_view haystack,
                               std::string_view needle) noexcept {
    if (needle.empty()) return true;
    const std::size_t hlen = haystack.size();
    const std::size_t nlen = needle.size();
    if (hlen < nlen) return false;

    // Pre-fold the first character of needle to avoid redundant toLower calls
    // inside the inner loop.
    const char n0 = toLowerAscii(needle[0]);
    const std::size_t limit = hlen - nlen;

    for (std::size_t i = 0; i <= limit; ++i) {
        if (toLowerAscii(haystack[i]) != n0) continue;
        bool match = true;
        for (std::size_t j = 1; j < nlen; ++j) {
            if (!ciEqualChar(haystack[i + j], needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/// Returns true if `haystack` contains `word` as a whole word (non-word chars
/// or string boundaries on both sides), case-insensitive.
[[nodiscard]] bool ciContainsWord(std::string_view haystack,
                                   std::string_view word) noexcept {
    if (word.empty() || haystack.size() < word.size()) return false;

    const std::size_t hlen  = haystack.size();
    const std::size_t nlen  = word.size();
    const std::size_t limit = hlen - nlen;
    const char w0 = toLowerAscii(word[0]);

    for (std::size_t i = 0; i <= limit; ++i) {
        // Check word boundary before match position.
        if (i > 0 && isWordChar(haystack[i - 1])) continue;

        if (toLowerAscii(haystack[i]) != w0) continue;

        bool match = true;
        for (std::size_t j = 1; j < nlen; ++j) {
            if (!ciEqualChar(haystack[i + j], word[j])) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        // Check word boundary after match position.
        const std::size_t end = i + nlen;
        if (end < hlen && isWordChar(haystack[end])) continue;

        return true;
    }
    return false;
}

/// Dispatches to the appropriate search function based on pattern flags.
[[nodiscard]] inline bool patternMatches(std::string_view prompt,
                                          const HeuristicPattern& p) noexcept {
    return p.whole_word ? ciContainsWord(prompt, p.text)
                        : ciContains(prompt, p.text);
}

// ---------------------------------------------------------------------------
// Shannon entropy calculator
//
// H = -sum( p(x) * log2(p(x)) )  for each unique byte value x.
//
// Implementation:
//   * 256-element frequency table on the stack — zero heap allocation.
//   * Single pass over the data.
//   * Early-exit for trivially short inputs.
//   * Uses std::log (natural log) / ln2 instead of std::log2 to stay in the
//     fast-math FPU path on most toolchains.
// ---------------------------------------------------------------------------

/// ln(2) — used to convert natural log to log base-2.
constexpr double kLn2 = 0.6931471805599453;

}  // anonymous namespace

namespace vxr {

double computeShannonEntropy(std::string_view data) noexcept {
    if (data.size() < 2) return 0.0;

    // Frequency table for all 256 byte values.
    std::array<std::size_t, 256> freq{};
    freq.fill(0);

    for (unsigned char c : data) {
        ++freq[c];
    }

    const double inv_n = 1.0 / static_cast<double>(data.size());
    double entropy = 0.0;

    for (std::size_t count : freq) {
        if (count == 0) continue;
        const double p = static_cast<double>(count) * inv_n;
        entropy -= p * (std::log(p) / kLn2);
    }

    return entropy;
}

// ---------------------------------------------------------------------------
// Core analysis implementation
// ---------------------------------------------------------------------------

AnalysisResult analyzePromptCpp(std::string_view prompt) noexcept {
    AnalysisResult result{};
    result.ThreatScore    = kThreatMin;
    result.ThreatType     = "none";
    result.is_safe        = true;
    result.status         = "safe";
    result.flagged_reason = "";
    result.entropy        = 0.0;

    // -----------------------------------------------------------------------
    // 1. STRICT MEMORY BOUNDARY CHECK
    //    std::string_view carries a (data, size) pair — no null-terminator
    //    scan required.  We reject oversized inputs without touching a single
    //    byte past kMaxPromptBytes.
    // -----------------------------------------------------------------------
    if (prompt.size() > kMaxPromptBytes) {
        result.ThreatScore    = kThreatMax;
        result.ThreatType     = "oversized_input";
        result.is_safe        = false;
        result.status         = "threat";
        result.flagged_reason = "oversized_input";
        return result;
    }

    if (prompt.empty()) {
        return result;
    }

    // -----------------------------------------------------------------------
    // 2. SHANNON ENTROPY CHECK
    //    High entropy (>= kEntropyThreshold) indicates base64 encoding,
    //    homoglyph substitution, or other obfuscation techniques.
    //    We still run the full pattern scan afterward — entropy alone is a
    //    signal, not a verdict, so we increase the score but continue.
    // -----------------------------------------------------------------------
    result.entropy = computeShannonEntropy(prompt);
    bool entropyFlagged = false;
    if (result.entropy >= kEntropyThreshold) {
        entropyFlagged = true;
        // Entropy alone maps to score 6 (moderate).  It may be overridden
        // upward by a pattern match below.
        result.ThreatScore    = 6;
        result.ThreatType     = "high_entropy_payload";
        result.flagged_reason = "high_entropy_payload";
    }

    // -----------------------------------------------------------------------
    // 3. CASE-FOLD INTO SCRATCH BUFFER
    //    We work on a mutable copy so the original caller-owned memory is
    //    never mutated.  The scratch buffer is bounded by kMaxPromptBytes,
    //    so the copy is safe-by-construction.
    // -----------------------------------------------------------------------
    const std::size_t len = prompt.size();
    for (std::size_t i = 0; i < len; ++i) {
        g_scratch[i] = toLowerAscii(prompt[i]);
    }
    const std::string_view folded(g_scratch, len);

    // -----------------------------------------------------------------------
    // 4. PATTERN TABLE SCANS
    //    We scan three categorised tables in priority order:
    //      a) Special tokens   — highest severity, checked verbatim.
    //      b) Override phrases — system prompt hijacking.
    //      c) Jailbreak phrases.
    //      d) Sensitive content.
    //    Educational context check is applied to (d) only.
    // -----------------------------------------------------------------------

    // --- 4a. Special tokens (case-insensitive substring match) ---
    // Special tokens like <|endoftext|> must also be matched in the original
    // prompt because angle-bracket characters survive case-folding intact.
    for (const HeuristicPattern& p : kSpecialTokens) {
        if (ciContains(folded, p.text)) {
            if (p.threat_level > result.ThreatScore) {
                result.ThreatScore    = p.threat_level;
                result.ThreatType     = p.reason;
                result.flagged_reason = p.reason;
            }
        }
    }

    // --- 4b. System-prompt override phrases ---
    for (const HeuristicPattern& p : kOverridePatterns) {
        if (patternMatches(folded, p)) {
            if (p.threat_level > result.ThreatScore) {
                result.ThreatScore    = p.threat_level;
                result.ThreatType     = p.reason;
                result.flagged_reason = p.reason;
            }
        }
    }

    // --- 4c. Jailbreak phrases ---
    for (const HeuristicPattern& p : kJailbreakPatterns) {
        if (patternMatches(folded, p)) {
            if (p.threat_level > result.ThreatScore) {
                result.ThreatScore    = p.threat_level;
                result.ThreatType     = p.reason;
                result.flagged_reason = p.reason;
            }
        }
    }

    // --- 4d. Sensitive content — with educational context downgrade ---
    // Educational context: downgrade severity of flagged sensitive keywords
    // by 4 threat points when the prompt contains clear pedagogical signals.
    const bool hasEducationalContext =
        ciContainsWord(folded, "explain")    ||
        ciContainsWord(folded, "educational")||
        ciContainsWord(folded, "medical")    ||
        ciContainsWord(folded, "anatomy")    ||
        ciContainsWord(folded, "biology")    ||
        ciContainsWord(folded, "health");

    for (const HeuristicPattern& p : kSensitivePatterns) {
        if (!patternMatches(folded, p)) continue;
        int level = p.threat_level;
        if (hasEducationalContext) {
            level -= 4;  // Map 8-10 down to 4-6 (moderate tier)
        }
        if (level > result.ThreatScore) {
            result.ThreatScore    = level;
            result.ThreatType     = p.reason;
            result.flagged_reason = p.reason;
        }
    }

    // -----------------------------------------------------------------------
    // 5. CLAMP AND DERIVE VERDICT
    // -----------------------------------------------------------------------
    result.ThreatScore = clampThreat(result.ThreatScore);

    const bool anyFlagged =
        entropyFlagged                        ||
        (result.ThreatScore > kThreatMin)     ||
        (result.flagged_reason[0] != '\0');

    if (anyFlagged && result.ThreatScore >= 7) {
        result.is_safe = false;
        result.status  = "threat";
    } else if (anyFlagged && result.ThreatScore >= 4) {
        result.is_safe = false;
        result.status  = "moderate";
    } else {
        result.is_safe = true;
        result.status  = "safe";
        // Keep flagged_reason if something was detected at low level.
    }

    return result;
}

}  // namespace vxr

// ============================================================================
// JSON serialization — writes into the static result buffer
// ============================================================================
namespace {

/// Serializes a vxr::AnalysisResult to `g_result_buffer` and returns it.
///
/// Output fields (matches the original JS schema + new ThreatScore/ThreatType):
///   { "ThreatScore": N,
///     "ThreatType":  "...",
///     "is_safe":     true|false,
///     "threat_level": N,        // backward-compat alias for ThreatScore
///     "flagged_reason": "...",
///     "status":      "...",
///     "entropy":     N.NN }
const char* serializeResult(const vxr::AnalysisResult& r) noexcept {
    const char* reason = (r.flagged_reason != nullptr) ? r.flagged_reason : "";
    const char* type   = (r.ThreatType     != nullptr) ? r.ThreatType     : "none";
    const char* status = (r.status         != nullptr) ? r.status         : "safe";

    std::snprintf(
        g_result_buffer,
        kResultBufferSize,
        "{"
        "\"ThreatScore\":%d,"
        "\"ThreatType\":\"%s\","
        "\"is_safe\":%s,"
        "\"threat_level\":%d,"
        "\"flagged_reason\":\"%s\","
        "\"status\":\"%s\","
        "\"entropy\":%.3f"
        "}",
        r.ThreatScore,
        type,
        r.is_safe ? "true" : "false",
        r.ThreatScore,   // backward-compat alias
        reason,
        status,
        r.entropy);

    // Guarantee null termination even if snprintf truncates.
    g_result_buffer[kResultBufferSize - 1] = '\0';
    return g_result_buffer;
}

/// Returns the JSON for a null/oversized-input guard failure.
const char* nullInputResult() noexcept {
    constexpr vxr::AnalysisResult safe{
        vxr::kThreatMin, "none", true, "safe", "", 0.0};
    return serializeResult(safe);
}

}  // anonymous namespace

// ============================================================================
// Legacy C++ shim
// ============================================================================

VXRAnalysisResult vxr_analyze_prompt(std::string_view prompt) noexcept {
    const vxr::AnalysisResult r = vxr::analyzePromptCpp(prompt);
    VXRAnalysisResult legacy{};
    legacy.is_safe        = r.is_safe;
    legacy.threat_level   = r.ThreatScore;
    legacy.flagged_reason = r.flagged_reason;
    legacy.status         = r.status;
    return legacy;
}

// ============================================================================
// WASM-exported C functions
// ============================================================================

extern "C" {

// Primary export: analyzePrompt (camelCase — matches updated JS contract).
VXR_EXPORT const char* analyzePrompt(const char* prompt) {
    if (prompt == nullptr) {
        return nullInputResult();
    }

    // Build a bounded string_view: strnlen caps traversal at kMaxPromptBytes,
    // so we never read past the allocation even if the caller forgot to
    // null-terminate or passed a massive buffer.
    const std::size_t len =
        std::strnlen(prompt, vxr::kMaxPromptBytes + 1);  // +1 to detect oversize

    const vxr::AnalysisResult result =
        vxr::analyzePromptCpp(std::string_view(prompt, len));

    return serializeResult(result);
}

// Legacy alias: analyze_prompt (snake_case — keeps app.js working as-is).
VXR_EXPORT const char* analyze_prompt(const char* prompt) {
    return analyzePrompt(prompt);
}

}  // extern "C"
