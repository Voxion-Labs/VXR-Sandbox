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
    {"nsfw", 9, "sensitive_content", true},
    {"pornographic", 9, "sensitive_content", false},
    {"pornography", 9, "sensitive_content", false},
    {"porn", 9, "sensitive_content", true},
    {"explicit violence", 10, "sensitive_content", false},
    {"extreme violence", 10, "sensitive_content", false},
    {"gore", 10, "sensitive_content", true},
    {"erotic", 8, "sensitive_content", false},
    {"nudity", 8, "sensitive_content", true},
    {"adult content", 8, "sensitive_content", false},
    {"kill people", 10, "sensitive_content", false},
    {"murder", 10, "sensitive_content", true},
    {"penis", 8, "sensitive_content", false},
    {"vagina", 8, "sensitive_content", false},
    {"hentai", 9, "sensitive_content", false},
    {"ass", 8, "sensitive_content", true},
    {"sex", 8, "sensitive_content", true},
    {"rape", 10, "sensitive_content", true},
    {"genocide", 10, "sensitive_content", false},
    {"hacking", 8, "sensitive_content", false},
    {"hack", 8, "sensitive_content", true},
    {"suicide", 10, "sensitive_content", false},
    {"bomb", 10, "sensitive_content", true},
    {"terrorist", 10, "sensitive_content", false},
    {"terrorism", 10, "sensitive_content", false},
    {"weapon", 8, "sensitive_content", true},
    {"weapons", 8, "sensitive_content", false},
    {"adult", 8, "sensitive_content", true},
    {"cyberattack", 8, "sensitive_content", false},
    {"cyber-attack", 8, "sensitive_content", false},
    {"exploit vulnerability", 9, "sensitive_content", false},
    {"malware", 9, "sensitive_content", false},
    {"phishing", 8, "sensitive_content", false},
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
    result.status = "safe";

    if (prompt.empty()) {
        return result;
    }

    // Convert the prompt to lowercase in-place on the staged mutable Wasm heap buffer.
    // This provides complete protection against capitalization bypass attempts.
    char* mutable_prompt = const_cast<char*>(prompt.data());
    for (std::size_t i = 0; i < prompt.size(); ++i) {
        mutable_prompt[i] = to_lower_ascii(mutable_prompt[i]);
    }

    // Establish context exception checks for educational queries
    bool has_educational_context =
        ci_contains_word(prompt, "explain") ||
        ci_contains_word(prompt, "educational") ||
        ci_contains_word(prompt, "medical") ||
        ci_contains_word(prompt, "anatomy") ||
        ci_contains_word(prompt, "biology") ||
        ci_contains_word(prompt, "health");

    bool matched = false;
    int max_threat = kThreatMin;
    const char* best_reason = "";

    for (const HeuristicPattern& pattern : kPatterns) {
        if (!pattern_matches(prompt, pattern)) {
            continue;
        }

        matched = true;
        int level = pattern.threat_level;

        // Context-aware negation: Downgrade sensitive content severity in educational context
        if (has_educational_context && std::string_view(pattern.reason) == "sensitive_content") {
            level = level - 4; // Map 8-10 down to 4-6 (Moderate tier)
        }

        if (level > max_threat) {
            max_threat = level;
            best_reason = pattern.reason;
        }
    }

    int final_threat = clamp_threat(max_threat);
    result.threat_level = final_threat;

    // Map the final threat level into our 3-tier status system
    if (matched && final_threat >= 7) {
        result.is_safe = false;
        result.status = "threat";
        result.flagged_reason = best_reason;
    } else if (matched && final_threat >= 4) {
        result.is_safe = false;
        result.status = "moderate";
        result.flagged_reason = best_reason;
    } else {
        result.is_safe = true;
        result.status = "safe";
        result.flagged_reason = matched ? best_reason : "";
    }

    return result;
}

const char* serialize_result(const VXRAnalysisResult& result) {
    const char* reason = result.flagged_reason != nullptr ? result.flagged_reason : "";
    const char* status = result.status != nullptr ? result.status : "safe";

    std::snprintf(
        g_result_buffer,
        kResultBufferSize,
        R"({"is_safe":%s,"threat_level":%d,"flagged_reason":"%s","status":"%s"})",
        result.is_safe ? "true" : "false",
        result.threat_level,
        reason,
        status);

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
