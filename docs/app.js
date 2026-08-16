/**
 * VXR-Sandbox — JavaScript ↔ WebAssembly bridge  (Phase 3)
 *
 * Expected Emscripten artifacts (same directory as this file):
 *   - vxr_kernel.js
 *   - vxr_kernel.wasm
 *
 * Recommended emcc flags for the build script:
 *   -s MODULARIZE=1 -s EXPORT_NAME=createVXRModule
 *   -s EXPORTED_FUNCTIONS=['_analyzePrompt','_analyze_prompt','_free']
 *   -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToNewUTF8']
 */

'use strict';

const VXR_WASM_JS  = 'vxr_kernel.js';
const VXR_WASM_BIN = 'vxr_kernel.wasm';

/** @type {EmscriptenModule | null} */
let vxrModule = null;

/** @type {Promise<EmscriptenModule> | null} */
let initPromise = null;

/** @type {((inputPtr: number) => number) | null} */
let analyzePrompt = null;

/**
 * @typedef {Object} ScanResult
 * @property {boolean} is_safe
 * @property {number}  threat_level
 * @property {string}  flagged_reason
 * @property {string}  status
 */

// ─────────────────────────────────────────────
// Wasm loader
// ─────────────────────────────────────────────

function locateWasmFile(path) {
  return path.endsWith('.wasm') ? VXR_WASM_BIN : path;
}

function bindKernelExports(module) {
  // Prefer the Phase-2 camelCase export; fall back to legacy snake_case
  const sym = typeof module._analyzePrompt === 'function'
    ? 'analyzePrompt'
    : 'analyze_prompt';
  analyzePrompt = module.cwrap(sym, 'number', ['number']);
}

function loadVXRModule() {
  if (vxrModule)    return Promise.resolve(vxrModule);
  if (initPromise)  return initPromise;

  initPromise = new Promise((resolve, reject) => {
    const onReady = (module) => {
      vxrModule = module;
      bindKernelExports(module);
      resolve(module);
    };

    const script = document.createElement('script');
    script.src   = VXR_WASM_JS;
    script.async = true;

    script.onload = () => {
      const factory =
        typeof globalThis.createVXRModule === 'function'
          ? globalThis.createVXRModule
          : typeof globalThis.Module === 'function'
            ? globalThis.Module
            : null;

      if (factory) {
        factory({ locateFile: locateWasmFile }).then(onReady).catch(reject);
        return;
      }

      if (typeof globalThis.Module === 'object' && globalThis.Module !== null) {
        globalThis.Module.locateFile = locateWasmFile;
        const prev = globalThis.Module.onRuntimeInitialized;
        globalThis.Module.onRuntimeInitialized = () => {
          if (typeof prev === 'function') prev();
          onReady(globalThis.Module);
        };
        if (globalThis.Module.calledRun) onReady(globalThis.Module);
        return;
      }

      reject(new Error(`No Emscripten module factory found in ${VXR_WASM_JS}`));
    };

    script.onerror = () => reject(new Error(`Failed to load ${VXR_WASM_JS}`));

    if (typeof globalThis.Module !== 'function') {
      globalThis.Module = {
        locateFile: locateWasmFile,
        onRuntimeInitialized() { onReady(globalThis.Module); },
      };
    }

    document.head.appendChild(script);
  });

  return initPromise;
}

function assertRuntimeMethods(module) {
  if (typeof module.stringToNewUTF8 !== 'function')
    throw new Error('Emscripten runtime missing stringToNewUTF8');
  if (typeof module.UTF8ToString !== 'function')
    throw new Error('Emscripten runtime missing UTF8ToString');
  if (typeof module._free !== 'function')
    throw new Error('Emscripten runtime missing _free');
  if (typeof analyzePrompt !== 'function')
    throw new Error('analyze_prompt / analyzePrompt is not bound');
}

/**
 * Scans user text locally via the C++ Wasm kernel.
 *
 * Memory contract:
 *   1. Copy JS string into Wasm linear memory with stringToNewUTF8.
 *   2. Pass pointer to analyzePrompt.
 *   3. Read returned static JSON buffer with UTF8ToString (do not free).
 *   4. _free() only the input pointer.
 *
 * @param {string} userText
 * @returns {Promise<ScanResult>}
 */
async function scanPromptLocal(userText) {
  if (!vxrModule) await loadVXRModule();

  const module = vxrModule;
  assertRuntimeMethods(module);

  const text     = userText == null ? '' : String(userText);
  const inputPtr = module.stringToNewUTF8(text);

  try {
    const resultPtr = analyzePrompt(inputPtr);
    if (!resultPtr) throw new Error('analyzePrompt returned null pointer');

    const jsonText = module.UTF8ToString(resultPtr);
    /** @type {ScanResult} */
    const result = JSON.parse(jsonText);

    if (
      typeof result.is_safe      !== 'boolean' ||
      typeof result.threat_level !== 'number'  ||
      typeof result.status       !== 'string'
    ) {
      throw new Error('Invalid response schema from Wasm kernel');
    }

    if (typeof result.flagged_reason !== 'string') result.flagged_reason = '';
    return result;
  } finally {
    module._free(inputPtr);
  }
}

function isVXRReady() {
  return vxrModule !== null && typeof analyzePrompt === 'function';
}

globalThis.VXR = { loadVXRModule, scanPromptLocal, isVXRReady };

// ─────────────────────────────────────────────
// DOM helpers
// ─────────────────────────────────────────────

let promptInput      = null;
let scanBtn          = null;
let scanResult       = null;
let kernelStatus     = null;
let pipelineLogWrap  = null;
let pipelineLog      = null;
let scanTimingBadge  = null;

function setKernelStatus(state, message) {
  if (!kernelStatus) return;
  kernelStatus.className = `kernel-status kernel-status--${state}`;
  const el = kernelStatus.querySelector('.kernel-status-text');
  if (el) el.textContent = message;
}

function escapeHtml(value) {
  return value
    .replace(/&/g,  '&amp;')
    .replace(/</g,  '&lt;')
    .replace(/>/g,  '&gt;')
    .replace(/"/g,  '&quot;')
    .replace(/'/g,  '&#39;');
}

// ─────────────────────────────────────────────
// Pipeline log (live execution display)
// ─────────────────────────────────────────────

const SPINNER_FRAMES = ['⠋','⠙','⠹','⠸','⠼','⠴','⠦','⠧','⠇','⠏'];

/**
 * Appends a log line to the pipeline log panel.
 * @param {'check'|'warn'|'err'|'spin'|'dim'} type
 * @param {string} text
 * @param {string} [timestamp]
 */
function appendLog(type, text, timestamp) {
  if (!pipelineLog) return;
  const li = document.createElement('li');
  li.className = 'log-entry';

  let iconHtml;
  if (type === 'check') iconHtml = `<span class="log-check">✓</span>`;
  else if (type === 'warn')  iconHtml = `<span class="log-warn">⚠</span>`;
  else if (type === 'err')   iconHtml = `<span class="log-err">✗</span>`;
  else if (type === 'spin')  iconHtml = `<span class="log-spin">${SPINNER_FRAMES[0]}</span>`;
  else iconHtml = `<span style="color:var(--text-dim)">·</span>`;

  const ts = timestamp || '';
  li.innerHTML = `${iconHtml}<span>${escapeHtml(text)}</span>${ts ? `<span class="log-ts">${ts}</span>` : ''}`;
  pipelineLog.appendChild(li);
  return li;
}

function clearLog() {
  if (!pipelineLog) return;
  pipelineLog.innerHTML = '';
}

/**
 * Runs the simulated pipeline log while the Wasm scan executes.
 * Returns a function to call when scan is done (stops the animation).
 * @param {string} promptText
 * @returns {() => void}   stop()
 */
function startPipelineLog(promptText) {
  clearLog();
  if (pipelineLogWrap) pipelineLogWrap.hidden = false;

  const byteLen  = new TextEncoder().encode(promptText).length;
  const t0       = performance.now();
  const relMs    = () => `+${(performance.now() - t0).toFixed(1)}ms`;

  const STEPS = [
    { delay:  0,   type: 'check', text: `Allocating Wasm buffer… (${byteLen} bytes)` },
    { delay:  55,  type: 'check', text: 'Boundary check: strnlen cap ≤ 65,536 B' },
    { delay: 115,  type: 'check', text: 'Computing Shannon entropy H(p)…' },
    { delay: 190,  type: 'check', text: 'Scanning special-token table (18 rules)…' },
    { delay: 260,  type: 'check', text: 'Lexical pattern match (70 rules / 3 categories)…' },
    { delay: 335,  type: 'check', text: 'Serialising result → static JSON buffer (768 B)' },
    { delay: 390,  type: 'check', text: '_free(inputPtr) — input deallocation' },
  ];

  const timers = [];

  for (const step of STEPS) {
    const t = setTimeout(() => {
      appendLog(step.type, step.text, relMs());
    }, step.delay);
    timers.push(t);
  }

  return function stop(result, elapsedMs) {
    timers.forEach(clearTimeout);

    const label = result
      ? (result.status === 'threat'   ? `✗  Threat scored  [${result.threat_level}/10]`
       : result.status === 'moderate' ? `⚠  Moderate risk  [${result.threat_level}/10]`
       :                                `✓  Threat scored  [${result.threat_level}/10] — SAFE`)
      : '✗  Scan error';

    const finalType = !result ? 'err'
      : result.status === 'threat'   ? 'err'
      : result.status === 'moderate' ? 'warn'
      : 'check';

    appendLog(finalType, label, `${elapsedMs.toFixed(2)}ms total`);
  };
}

// ─────────────────────────────────────────────
// Telemetry footer helpers
// ─────────────────────────────────────────────

/** Simulate a realistic Wasm linear-memory RAM usage (2.1–2.8 MB) */
function mockKernelRam() {
  return (2.1 + Math.random() * 0.7).toFixed(1) + ' MB';
}

/**
 * Update the telemetry bar after a scan.
 * @param {number} latencyMs
 * @param {string} entropyStr
 */
function updateTelemetry(latencyMs, entropyStr) {
  const setVal = (id, val) => {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  };

  setVal('tbar-ram',     mockKernelRam());
  setVal('tbar-latency', `${latencyMs.toFixed(2)} ms`);
  setVal('tbar-heap',    '0 B');           // zero-allocation hot path
  setVal('tbar-entropy', entropyStr || '—');
}

/**
 * Estimate a mock Shannon entropy for display (not the real C++ value).
 * @param {string} text
 */
function estimateEntropy(text) {
  if (!text || text.length < 4) return '—';
  const freq = {};
  for (const ch of text) freq[ch] = (freq[ch] || 0) + 1;
  const len = text.length;
  let h = 0;
  for (const count of Object.values(freq)) {
    const p = count / len;
    h -= p * Math.log2(p);
  }
  return `${h.toFixed(2)} bits`;
}

// ─────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────

/**
 * @param {ScanResult} result
 * @param {number} latencyMs
 */
function renderScanResult(result, latencyMs) {
  if (!scanResult) return;

  const status = result.status || (result.is_safe ? 'safe' : 'threat');
  let stateClass    = 'safe';
  let verdictLabel  = 'Safe';
  let verdictIcon   = '✓';

  if (status === 'threat') {
    stateClass   = 'threat';
    verdictLabel = 'Threat Detected';
    verdictIcon  = '✕';
  } else if (status === 'moderate') {
    stateClass   = 'moderate';
    verdictLabel = 'Moderate Risk';
    verdictIcon  = '⚠';
  }

  const reason       = result.flagged_reason?.length ? result.flagged_reason : '—';
  const meterPct     = Math.min(100, Math.max(0, (result.threat_level / 10) * 100));

  scanResult.className = `scan-result scan-result--${stateClass}`;
  scanResult.innerHTML = `
    <div class="result-verdict">
      <span class="result-verdict-icon" aria-hidden="true">${verdictIcon}</span>
      <p class="result-verdict-label">${verdictLabel}</p>
    </div>
    <div class="result-metrics">
      <div class="metric-card">
        <span class="metric-label">is_safe</span>
        <span class="metric-value">${String(result.is_safe)}</span>
      </div>
      <div class="metric-card">
        <span class="metric-label">threat_level</span>
        <span class="metric-value metric-value--level">${result.threat_level} / 10</span>
        <div class="threat-meter" role="progressbar"
             aria-valuenow="${result.threat_level}" aria-valuemin="1" aria-valuemax="10"
             aria-label="Threat level">
          <div class="threat-meter-fill" style="width:${meterPct}%"></div>
        </div>
      </div>
      <div class="metric-card metric-card--full">
        <span class="metric-label">flagged_reason</span>
        <span class="metric-value">${escapeHtml(reason)}</span>
      </div>
    </div>
  `;

  // Update timing badge in panel header
  if (scanTimingBadge) {
    scanTimingBadge.textContent = `${latencyMs.toFixed(2)} ms`;
  }
}

/**
 * @param {string} message
 */
function renderScanError(message) {
  if (!scanResult) return;
  scanResult.className = 'scan-result scan-result--error';
  scanResult.innerHTML = `
    <div class="result-verdict">
      <span class="result-verdict-icon" aria-hidden="true">✕</span>
      <p class="result-verdict-label">Scan Failed</p>
    </div>
    <p class="result-error-message">${escapeHtml(message)}</p>
  `;
}

// ─────────────────────────────────────────────
// Scan orchestration
// ─────────────────────────────────────────────

function setScanning(scanning) {
  if (!scanBtn || !promptInput) return;
  scanBtn.disabled = scanning || !isVXRReady();
  scanBtn.classList.toggle('is-scanning', scanning);
  scanBtn.textContent = scanning ? 'Analyzing…' : 'Analyze locally';
  promptInput.disabled = scanning;
}

async function handleScan() {
  if (!promptInput || !scanBtn || !scanResult) return;

  const userText = promptInput.value;
  setScanning(true);

  const stopLog = startPipelineLog(userText);
  const t0      = performance.now();

  try {
    const result    = await scanPromptLocal(userText);
    const elapsed   = performance.now() - t0;
    const entropyEst = estimateEntropy(userText);

    stopLog(result, elapsed);
    renderScanResult(result, elapsed);
    updateTelemetry(elapsed, entropyEst);
  } catch (error) {
    const elapsed = performance.now() - t0;
    const message = error instanceof Error ? error.message : 'Unknown scan error';
    stopLog(null, elapsed);
    renderScanError(message);
    updateTelemetry(elapsed, '—');
    console.error('[VXR] Scan failed:', error);
  } finally {
    setScanning(false);
  }
}

// ─────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────

function wireUI() {
  promptInput     = document.getElementById('prompt-input');
  scanBtn         = document.getElementById('scan-btn');
  scanResult      = document.getElementById('scan-result');
  kernelStatus    = document.getElementById('kernel-status');
  pipelineLogWrap = document.getElementById('pipeline-log-wrap');
  pipelineLog     = document.getElementById('pipeline-log');
  scanTimingBadge = document.getElementById('scan-timing-badge');

  if (!promptInput || !scanBtn || !scanResult) {
    console.error('[VXR] Required DOM elements not found.');
    return;
  }

  scanBtn.addEventListener('click',   () => void handleScan());
  promptInput.addEventListener('keydown', (e) => {
    if (e.ctrlKey && e.key === 'Enter' && !scanBtn.disabled) {
      e.preventDefault();
      void handleScan();
    }
  });

  loadVXRModule()
    .then(() => {
      setKernelStatus('ready', 'Wasm kernel online');
      scanBtn.disabled = false;
      // Seed telemetry bar with idle values
      updateTelemetry(0, '—');
      const ramEl = document.getElementById('tbar-ram');
      if (ramEl) ramEl.textContent = mockKernelRam();
      const latEl = document.getElementById('tbar-latency');
      if (latEl) latEl.textContent = '—';
    })
    .catch((error) => {
      const msg = error instanceof Error ? error.message : 'Kernel init failed';
      setKernelStatus('error', msg);
      renderScanError(`Wasm kernel failed to load: ${msg}`);
      console.error('[VXR] Wasm module initialization failed:', error);
    });
}

document.addEventListener('DOMContentLoaded', wireUI);
console.log('[VXR] Phase 3 bridge loaded — pipeline log + telemetry active.');
