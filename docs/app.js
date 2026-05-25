/**
 * VXR-Sandbox — JavaScript ↔ WebAssembly bridge
 *
 * Expected Emscripten artifacts (same directory as this file):
 *   - vxr_kernel.js
 *   - vxr_kernel.wasm
 *
 * Recommended emcc flags for the build script:
 *   -s MODULARIZE=1 -s EXPORT_NAME=createVXRModule
 *   -s EXPORTED_FUNCTIONS=['_analyze_prompt','_free']
 *   -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToNewUTF8']
 */

'use strict';

const VXR_WASM_JS = 'vxr_kernel.js';
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
 * @property {number} threat_level
 * @property {string} flagged_reason
 * @property {string} status
 */

/**
 * Resolves the .wasm file path relative to the JS glue script.
 * @param {string} path
 * @returns {string}
 */
function locateWasmFile(path) {
  if (path.endsWith('.wasm')) {
    return VXR_WASM_BIN;
  }
  return path;
}

/**
 * Binds exported C functions after the runtime is ready.
 * @param {EmscriptenModule} module
 */
function bindKernelExports(module) {
  analyzePrompt = module.cwrap('analyze_prompt', 'number', ['number']);
}

/**
 * Loads and initializes the Emscripten module.
 * @returns {Promise<EmscriptenModule>}
 */
function loadVXRModule() {
  if (vxrModule) {
    return Promise.resolve(vxrModule);
  }

  if (initPromise) {
    return initPromise;
  }

  initPromise = new Promise((resolve, reject) => {
  /** @param {EmscriptenModule} module */
    const onReady = (module) => {
      vxrModule = module;
      bindKernelExports(module);
      resolve(module);
    };

    const script = document.createElement('script');
    script.src = VXR_WASM_JS;
    script.async = true;

    script.onload = () => {
      const modularFactory =
        typeof globalThis.createVXRModule === 'function'
          ? globalThis.createVXRModule
          : typeof globalThis.Module === 'function'
            ? globalThis.Module
            : null;

      if (modularFactory) {
        modularFactory({ locateFile: locateWasmFile })
          .then(onReady)
          .catch(reject);
        return;
      }

      if (typeof globalThis.Module === 'object' && globalThis.Module !== null) {
        globalThis.Module.locateFile = locateWasmFile;
        const previousInit = globalThis.Module.onRuntimeInitialized;

        globalThis.Module.onRuntimeInitialized = () => {
          if (typeof previousInit === 'function') {
            previousInit();
          }
          onReady(globalThis.Module);
        };

        if (globalThis.Module.calledRun) {
          onReady(globalThis.Module);
        }
        return;
      }

      reject(new Error(`No Emscripten module factory found in ${VXR_WASM_JS}`));
    };

    script.onerror = () => {
      reject(new Error(`Failed to load ${VXR_WASM_JS}`));
    };

    if (typeof globalThis.Module !== 'function') {
      globalThis.Module = {
        locateFile: locateWasmFile,
        onRuntimeInitialized() {
          onReady(globalThis.Module);
        },
      };
    }

    document.head.appendChild(script);
  });

  return initPromise;
}

/**
 * Ensures required Emscripten runtime helpers are present.
 * @param {EmscriptenModule} module
 */
function assertRuntimeMethods(module) {
  if (typeof module.stringToNewUTF8 !== 'function') {
    throw new Error('Emscripten runtime missing stringToNewUTF8');
  }
  if (typeof module.UTF8ToString !== 'function') {
    throw new Error('Emscripten runtime missing UTF8ToString');
  }
  if (typeof module._free !== 'function') {
    throw new Error('Emscripten runtime missing _free');
  }
  if (typeof analyzePrompt !== 'function') {
    throw new Error('analyze_prompt is not bound');
  }
}

/**
 * Scans user text locally via the C++ Wasm kernel.
 *
 * Memory contract:
 *   1. Copy JS string into Wasm linear memory with stringToNewUTF8.
 *   2. Pass that pointer to analyze_prompt.
 *   3. Read the returned static JSON buffer with UTF8ToString (do not free).
 *   4. _free() only the input pointer we allocated.
 *
 * @param {string} userText
 * @returns {Promise<ScanResult>}
 */
async function scanPromptLocal(userText) {
  if (!vxrModule) {
    await loadVXRModule();
  }

  const module = vxrModule;
  assertRuntimeMethods(module);

  const text = userText == null ? '' : String(userText);
  const inputPtr = module.stringToNewUTF8(text);

  try {
    const resultPtr = analyzePrompt(inputPtr);
    if (!resultPtr) {
      throw new Error('analyze_prompt returned a null pointer');
    }

    // Returned pointer references a static C++ buffer; never _free() it.
    const jsonText = module.UTF8ToString(resultPtr);
    /** @type {ScanResult} */
    const result = JSON.parse(jsonText);

    if (
      typeof result.is_safe !== 'boolean' ||
      typeof result.threat_level !== 'number' ||
      typeof result.status !== 'string'
    ) {
      throw new Error('Invalid response schema from Wasm kernel');
    }

    if (typeof result.flagged_reason !== 'string') {
      result.flagged_reason = '';
    }

    return result;
  } finally {
    module._free(inputPtr);
  }
}

/**
 * @returns {boolean}
 */
function isVXRReady() {
  return vxrModule !== null && typeof analyzePrompt === 'function';
}

globalThis.VXR = {
  loadVXRModule,
  scanPromptLocal,
  isVXRReady,
};

/** @type {HTMLTextAreaElement | null} */
let promptInput = null;

/** @type {HTMLButtonElement | null} */
let scanBtn = null;

/** @type {HTMLElement | null} */
let scanResult = null;

/** @type {HTMLElement | null} */
let kernelStatus = null;

/**
 * @param {'loading' | 'ready' | 'error'} state
 * @param {string} message
 */
function setKernelStatus(state, message) {
  if (!kernelStatus) {
    return;
  }

  kernelStatus.className = `kernel-status kernel-status--${state}`;
  const textEl = kernelStatus.querySelector('.kernel-status-text');
  if (textEl) {
    textEl.textContent = message;
  }
}

/**
 * @param {ScanResult} result
 */
function renderScanResult(result) {
  if (!scanResult) {
    return;
  }

  const status = result.status || (result.is_safe ? 'safe' : 'threat');
  let stateClass = 'safe';
  let verdictLabel = 'Safe';
  let verdictIcon = '✓';

  if (status === 'threat') {
    stateClass = 'threat';
    verdictLabel = 'Threat Detected';
    verdictIcon = '✕';
  } else if (status === 'moderate') {
    stateClass = 'moderate';
    verdictLabel = 'Caution: Educational / Borderline';
    verdictIcon = '⚠';
  }

  const reason =
    result.flagged_reason && result.flagged_reason.length > 0
      ? result.flagged_reason
      : '—';
  const meterPercent = Math.min(100, Math.max(0, (result.threat_level / 10) * 100));

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
        <div class="threat-meter" role="progressbar" aria-valuenow="${result.threat_level}" aria-valuemin="1" aria-valuemax="10" aria-label="Threat level">
          <div class="threat-meter-fill" style="width: ${meterPercent}%"></div>
        </div>
      </div>
      <div class="metric-card metric-card--full">
        <span class="metric-label">flagged_reason</span>
        <span class="metric-value">${escapeHtml(reason)}</span>
      </div>
    </div>
  `;
}

/**
 * @param {string} message
 */
function renderScanError(message) {
  if (!scanResult) {
    return;
  }

  scanResult.className = 'scan-result scan-result--error';
  scanResult.innerHTML = `
    <div class="result-verdict">
      <span class="result-verdict-icon" aria-hidden="true">✕</span>
      <p class="result-verdict-label">Scan Failed</p>
    </div>
    <p class="result-error-message">${escapeHtml(message)}</p>
  `;
}

/**
 * @param {string} value
 * @returns {string}
 */
function escapeHtml(value) {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

/**
 * @param {boolean} scanning
 */
function setScanning(scanning) {
  if (!scanBtn || !promptInput) {
    return;
  }

  scanBtn.disabled = scanning || !isVXRReady();
  scanBtn.classList.toggle('is-scanning', scanning);
  scanBtn.textContent = scanning ? 'Analyzing…' : 'Analyze locally';
  promptInput.disabled = scanning;
}

async function handleScan() {
  if (!promptInput || !scanBtn || !scanResult) {
    return;
  }

  const userText = promptInput.value;

  setScanning(true);

  try {
    const result = await scanPromptLocal(userText);
    renderScanResult(result);
  } catch (error) {
    const message = error instanceof Error ? error.message : 'Unknown scan error';
    renderScanError(message);
    console.error('[VXR] Scan failed:', error);
  } finally {
    setScanning(false);
  }
}

function wireUI() {
  promptInput = document.getElementById('prompt-input');
  scanBtn = document.getElementById('scan-btn');
  scanResult = document.getElementById('scan-result');
  kernelStatus = document.getElementById('kernel-status');

  if (!promptInput || !scanBtn || !scanResult) {
    console.error('[VXR] Required DOM elements not found.');
    return;
  }

  scanBtn.addEventListener('click', () => {
    void handleScan();
  });

  promptInput.addEventListener('keydown', (event) => {
    if (event.ctrlKey && event.key === 'Enter' && !scanBtn.disabled) {
      event.preventDefault();
      void handleScan();
    }
  });

  loadVXRModule()
    .then(() => {
      setKernelStatus('ready', 'Wasm kernel online');
      scanBtn.disabled = false;
    })
    .catch((error) => {
      const message = error instanceof Error ? error.message : 'Kernel init failed';
      setKernelStatus('error', message);
      renderScanError(`Wasm kernel failed to load: ${message}`);
      console.error('[VXR] Wasm module initialization failed:', error);
    });
}

document.addEventListener('DOMContentLoaded', wireUI);

console.log('[VXR] Wasm bridge loaded.');
