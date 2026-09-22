# VXR-Sandbox Operational & Contribution Protocols

VXR-Sandbox operates under strict WebAssembly compilation and zero-backend execution protocols. We do not accept arbitrary JavaScript bloat, heap-dependent C++ parsing, or network ingress logic. This repository is maintained for high-performance, client-side cybersecurity research.

If you intend to submit a Pull Request, you must adhere strictly to the following institutional directives.

## 1. Architectural Standards
All code submitted to VXR-Sandbox must meet our baseline security and performance metrics:
* **Zero-Heap Hot Path:** Submissions introducing `std::string` allocations or uncontrolled heap growth during the `analyze_prompt` execution will be instantly rejected. All heuristic scanning must rely on `constexpr` arrays and `std::string_view`.
* **Memory Isolation Contract:** The JS-to-C memory contract is absolute. Any strings allocated via `stringToNewUTF8` must be explicitly destroyed via `_free()`. Do not attempt to free the static `g_result_buffer`.
* **Zero-Network Ingress:** Under no circumstances should the kernel or JS bridge initiate outbound network requests (fetch, XHR, WebSockets). The sandbox is strictly an offline threat assessment layer.

## 2. Pull Request (PR) Governance
Before initiating a merge request, ensure your PR adheres to this exact structure:
1. **[METRIC] Benchmark Data:** You must provide before/after execution telemetry (e.g., Wasm binary size, prompt scan latency in ms, Emscripten compilation flags used).
2. **[LOGIC] State Transition:** Explicitly document the deterministic changes made to the heuristic detection engine or C ABI exports.
3. **[ISOLATION] Threat Model:** Prove that your modifications do not introduce buffer overflows within the WebAssembly linear memory that could result in sandbox escapes.

*Note: PRs failing to provide empirical telemetry or violating the zero-heap contract will be closed immediately without review.*

## 3. Vulnerability Disclosure
**DO NOT** open public issues for prompt-injection bypasses, linear memory buffer overflows, or Wasm sandbox escapes. Public disclosure of critical threats compromises the integrity of the defense layer.
* All security reports must be routed internally.
* Contact the Lead Architect directly for secure transmission protocols.

## 4. Code of Conduct
We evaluate algorithmic efficiency and memory safety, not intentions. Your submissions will be scrutinized ruthlessly based on C++17 compilation optimization and WebAssembly memory determinism. Keep discussions clinical, objective, and exclusively focused on system architecture.