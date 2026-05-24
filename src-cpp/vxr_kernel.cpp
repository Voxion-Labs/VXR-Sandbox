// Voxion eXperimental Research (VXR)
// Core Sandbox Kernel (C++)

#include "vxr_kernel.h"

#include <cctype>
#include <cstdio>
#include <string_view>

namespace {

constexpr int kThreatMin = 1;
constexpr int kThreatMax = 10;
constexpr std::size_t kResultBufferSize = 512;

struct HeuristicPattern {
    std::string_view text;
    int threat_level;
    const char* reason;
    bool whole_word;
};

// Static pattern table — no runtime allocation.
constexpr HeuristicPattern kPatterns[] = {
    {"ignore previous instructions", 9, "ignore_previous_instructions", false},
    {"ignore all previous", 9, "ignore_previous_instructions", false},
    {"ignore prior instructions", 9, "ignore_previous_instructions", false},
    {"disregard previous instructions", 8, "disregard_instructions", false},
    {"disregard all prior", 8, "disregard_instructions", false},
    {"forget your instructions", 8, "forget_instructions", false},
    {"forget all instructions", 8, "forget_instructions", false},
    {"system override", 10, "system_override", false},
    {"override system prompt", 10, "system_override", false},
    {"override the system", 10, "system_override", false},
    {"bypass security", 9, "bypass_security", false},
    {"bypass safety", 9, "bypass_security", false},
    {"bypass filter", 8, "bypass_attempt", false},
    {"bypass", 7, "bypass_attempt", false},
    {"you are a", 5, "persona_redefinition", false},
    {"you are now a", 6, "persona_redefinition", false},
    {"act as if you are", 6, "persona_redefinition", false},
    {"pretend you are", 6, "persona_redefinition", false},
    {"simulate being", 5, "persona_redefinition", false},
    {"do anything now", 9, "dan_jailbreak", false},
    {"dan mode", 8, "dan_jailbreak", false},
    {"enable dan", 8, "dan_jailbreak", false},
    {"jailbreak", 8, "jailbreak_keyword", false},
    {"developer mode", 7, "developer_mode", false},
    {"dev mode enabled", 7, "developer_mode", false},
    {"no restrictions", 7, "restriction_removal", false},
    {"without restrictions", 7, "restriction_removal", false},
    {"without any restrictions", 8, "restriction_removal", false},
    {"reveal system prompt", 9, "prompt_exfiltration", false},
    {"show system prompt", 9, "prompt_exfiltration", false},
    {"print your instructions", 8, "prompt_exfiltration", false},
    {"dan", 8, "dan_jailbreak", true},
};

char g_result_buffer[kResultBufferSize];

inline char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

inline bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool ci_equal_char(char a, char b) {
    return to_lower_ascii(a) == to_lower_ascii(b);
}

bool ci_contains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }

    const std::size_t limit = haystack.size() - needle.size() + 1;
    for (std::size_t i = 0; i < limit; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (!ci_equal_char(haystack[i + j], needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool ci_contains_word(std::string_view haystack, std::string_view word) {
    if (word.empty() || haystack.size() < word.size()) {
        return false;
    }

    const std::size_t limit = haystack.size() - word.size() + 1;
    for (std::size_t i = 0; i < limit; ++i) {
        if ((i > 0 && is_word_char(haystack[i - 1])) ||
            (i + word.size() < haystack.size() && is_word_char(haystack[i + word.size()]))) {
            continue;
        }

        bool match = true;
        for (std::size_t j = 0; j < word.size(); ++j) {
            if (!ci_equal_char(haystack[i + j], word[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool pattern_matches(std::string_view prompt, const HeuristicPattern& pattern) {
    if (pattern.whole_word) {
        return ci_contains_word(prompt, pattern.text);
    }
    return ci_contains(prompt, pattern.text);
}

int clamp_threat(int level) {
    if (level < kThreatMin) {
        return kThreatMin;
    }
    if (level > kThreatMax) {
        return kThreatMax;
    }
    return level;
}

VXRAnalysisResult analyze_impl(std::string_view prompt) {
    VXRAnalysisResult result{};
    result.is_safe = true;
    result.threat_level = kThreatMin;
    result.flagged_reason = "";

    if (prompt.empty()) {
        return result;
    }

    bool matched = false;
    int max_threat = kThreatMin;
    const char* best_reason = "";

    for (const HeuristicPattern& pattern : kPatterns) {
        if (!pattern_matches(prompt, pattern)) {
            continue;
        }

        matched = true;
        if (pattern.threat_level > max_threat) {
            max_threat = pattern.threat_level;
            best_reason = pattern.reason;
        }
    }

    if (matched) {
        result.is_safe = false;
        result.threat_level = clamp_threat(max_threat);
        result.flagged_reason = best_reason;
    }

    return result;
}

const char* serialize_result(const VXRAnalysisResult& result) {
    const char* reason = result.flagged_reason != nullptr ? result.flagged_reason : "";

    std::snprintf(
        g_result_buffer,
        kResultBufferSize,
        R"({"is_safe":%s,"threat_level":%d,"flagged_reason":"%s"})",
        result.is_safe ? "true" : "false",
        result.threat_level,
        reason);

    g_result_buffer[kResultBufferSize - 1] = '\0';
    return g_result_buffer;
}

}  // namespace

VXRAnalysisResult vxr_analyze_prompt(std::string_view prompt) {
    return analyze_impl(prompt);
}

extern "C" const char* analyze_prompt(const char* prompt) {
    if (prompt == nullptr) {
        const VXRAnalysisResult safe_default{true, kThreatMin, ""};
        return serialize_result(safe_default);
    }

    const VXRAnalysisResult result = analyze_impl(std::string_view(prompt));
    return serialize_result(result);
}
