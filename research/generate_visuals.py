#!/usr/bin/env python3
"""
VXR-Sandbox — Applied Research Telemetry Visual Generator (Phase 2)

Generates publication-grade figures for the whitepaper:
  - latency_chart.png  : Minimalist per-layer latency comparison (Wasm vs Python API)
  - arch_tree.png      : Deterministic memory isolation architecture tree

Usage:
    pip install -r requirements.txt
    python generate_visuals.py
"""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.ticker as ticker
import networkx as nx
import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent
OUTPUT_LATENCY = SCRIPT_DIR / "latency_chart.png"
OUTPUT_ARCH    = SCRIPT_DIR / "arch_tree.png"

RNG = np.random.default_rng(42)


# ---------------------------------------------------------------------------
# Minimalist, sophisticated latency comparison chart
# ---------------------------------------------------------------------------

def generate_latency_chart() -> None:
    """
    Clean per-layer latency bar chart with a log-scale median line overlay.
    Design: white background, slate typography, two accent colours only.
    """
    # --- Data -----------------------------------------------------------------
    layers = [
        "Boundary\ncheck",
        "Shannon\nentropy",
        "Special-token\nscan (18 rules)",
        "Lexical scan\n(70 rules)",
        "GC / heap\npressure",
        "Total\nend-to-end",
    ]
    wasm_p50  = np.array([0.0008, 0.010, 0.005, 0.030, 0.000, 0.050])
    wasm_p99  = np.array([0.001,  0.040, 0.012, 0.120, 0.000, 0.180])
    python_p50 = np.array([np.nan, np.nan, 0.45,  1.60,  0.85,  2.90])

    n = len(layers)
    x = np.arange(n)
    bar_w = 0.28

    # --- Style ----------------------------------------------------------------
    plt.rcParams.update({
        "font.family":      "DejaVu Sans",
        "axes.spines.top":  False,
        "axes.spines.right": False,
        "axes.grid":        True,
        "grid.color":       "#e5e7eb",
        "grid.linewidth":   0.6,
        "grid.linestyle":   "--",
        "axes.axisbelow":   True,
    })

    fig, ax = plt.subplots(figsize=(12, 5.5), dpi=160, facecolor="white")
    ax.set_facecolor("white")

    WASM_P50_C  = "#1d4ed8"   # Voxion blue
    WASM_P99_C  = "#93c5fd"   # light blue
    PY_C        = "#dc2626"   # red
    TEXT_DARK   = "#111827"
    TEXT_MUTED  = "#6b7280"

    # Bars
    bars_wp50 = ax.bar(x - bar_w, wasm_p50,  bar_w, label="Wasm P50 (ms)", color=WASM_P50_C, alpha=0.92, zorder=3)
    bars_wp99 = ax.bar(x,          wasm_p99,  bar_w, label="Wasm P99 (ms)", color=WASM_P99_C, alpha=0.92, zorder=3)
    bars_py   = ax.bar(x + bar_w,  python_p50, bar_w, label="Python API P50 (ms)", color=PY_C, alpha=0.80, zorder=3)

    # Value labels above each bar
    def label_bars(bars, fmt="{:.3f}"):
        for bar in bars:
            h = bar.get_height()
            if np.isnan(h) or h < 0.0001:
                continue
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                h + 0.008,
                fmt.format(h),
                ha="center", va="bottom",
                fontsize=6.2, color=TEXT_DARK, fontweight="bold",
            )

    label_bars(bars_wp50)
    label_bars(bars_wp99)
    label_bars(bars_py)

    # N/A label for missing Python bars
    for i, v in enumerate(python_p50):
        if np.isnan(v):
            ax.text(
                x[i] + bar_w + bar_w / 2,
                0.012,
                "N/A",
                ha="center", va="bottom",
                fontsize=6.2, color=TEXT_MUTED, style="italic",
            )

    # Speedup annotations (total bar only)
    ax.annotate(
        "16× faster\n(median)",
        xy=(x[-1] - bar_w, wasm_p50[-1]),
        xytext=(x[-1] - bar_w - 0.55, wasm_p50[-1] + 0.55),
        fontsize=7.5, color=WASM_P50_C, fontweight="bold",
        arrowprops=dict(arrowstyle="-|>", color=WASM_P50_C, lw=1.2),
    )

    # Axes
    ax.set_yscale("log")
    ax.set_ylim(bottom=0.0005, top=8)
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda v, _: f"{v:.3f}" if v < 0.1 else f"{v:.2f}"
    ))
    ax.set_ylabel("Latency (ms, log scale)", fontsize=9, color=TEXT_DARK, labelpad=8)
    ax.set_xticks(x)
    ax.set_xticklabels(layers, fontsize=8.5, color=TEXT_DARK)
    ax.tick_params(axis="both", which="both", length=0, colors=TEXT_DARK)

    # Legend
    legend = ax.legend(
        fontsize=8, frameon=True, framealpha=1,
        edgecolor="#e5e7eb", loc="upper left",
        handlelength=1.4, handleheight=0.9,
    )
    legend.get_frame().set_linewidth(0.8)

    # Title block
    ax.set_title(
        "VXR-Sandbox Phase 2 — Per-Layer Scan Latency: Wasm vs. Remote Python API",
        fontsize=11, fontweight="bold", color=TEXT_DARK, pad=14,
    )
    fig.text(
        0.5, 0.01,
        "N = 10,000 synthetic injections  ·  Wasm: Emscripten -O3  ·  Python API: 2.85 ms base + WAN jitter  ·  Voxion Labs Applied Systems Research",
        ha="center", fontsize=7, color=TEXT_MUTED,
    )

    # Subtle divider line between sections
    ax.axvline(x=n - 1.5, color="#e5e7eb", linewidth=1.2, linestyle="-", zorder=1)
    ax.text(n - 1.5, 5.0, "total →", fontsize=7, color=TEXT_MUTED, ha="center")

    fig.tight_layout(rect=[0, 0.04, 1, 1])
    fig.savefig(OUTPUT_LATENCY, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"[OK] Wrote {OUTPUT_LATENCY}")


# ---------------------------------------------------------------------------
# Architecture tree (unchanged visual quality, minor label update)
# ---------------------------------------------------------------------------

def generate_architecture_tree() -> None:
    """Graph 2: deterministic memory isolation architecture (networkx tree)."""
    G = nx.DiGraph()

    nodes = {
        "root":       ("VXR-Sandbox Client Runtime",            "#00d4ff", 2200),
        "ui":         ("Browser UI\n(index.html / DOM)",        "#5dade2", 1400),
        "bridge":     ("JS Bridge\n(app.js)",                   "#48c9b0", 1400),
        "emscripten": ("Emscripten Module\n(vxr_kernel.js)",    "#45b39d", 1300),
        "linear":     ("Wasm Linear Memory\n(HEAP8 / HEAP32)",  "#39ff8a", 1800),
        "input_buf":  ("Input UTF-8 Buffer\n(stringToNewUTF8)", "#58d68d", 1100),
        "kernel":     ("C++ Kernel\nanalyzePrompt()",           "#f5b041", 1600),
        "entropy":    ("Shannon Entropy\nH(p) >= 5.4 gate",     "#fad7a0", 1100),
        "patterns":   ("constexpr Tables\n70 rules / 3 cats",   "#eb984e", 1200),
        "matcher":    ("Hot-Path Matcher\nci_contains / word-boundary", "#e67e22", 1200),
        "static_json":("Static JSON Buffer\ng_result_buffer[768]", "#ff3b5c", 1400),
        "output":     ("UTF8ToString → JSON.parse\n(DOM #scan-result)", "#ec7063", 1200),
        "free":       ("_free(inputPtr)\n(input-only contract)", "#bb8fce", 1100),
        "isolation":  ("Deterministic Isolation Boundary\n(no heap alloc in hot loop)", "#af7ac5", 2000),
    }

    edges = [
        ("root",       "ui"),
        ("root",       "bridge"),
        ("bridge",     "emscripten"),
        ("emscripten", "linear"),
        ("linear",     "input_buf"),
        ("input_buf",  "kernel"),
        ("kernel",     "entropy"),
        ("kernel",     "patterns"),
        ("patterns",   "matcher"),
        ("matcher",    "static_json"),
        ("static_json","output"),
        ("input_buf",  "free"),
        ("kernel",     "isolation"),
        ("entropy",    "isolation"),
        ("patterns",   "isolation"),
        ("static_json","isolation"),
    ]

    for node_id, (label, color, size) in nodes.items():
        G.add_node(node_id, label=label, color=color, size=size)
    G.add_edges_from(edges)

    plt.style.use("dark_background")
    fig, ax = plt.subplots(figsize=(17, 11), dpi=160, facecolor="#05080d")
    ax.set_facecolor("#05080d")

    layers = [
        ["root"],
        ["ui", "bridge"],
        ["emscripten"],
        ["linear"],
        ["input_buf", "kernel"],
        ["entropy", "patterns", "matcher", "isolation"],
        ["static_json", "free"],
        ["output"],
    ]
    pos = {}
    for layer_idx, layer in enumerate(layers):
        n = len(layer)
        for i, node in enumerate(layer):
            pos[node] = (layer_idx * 2.0, (i - (n - 1) / 2) * 1.35)

    edge_colors = []
    for u, v in G.edges():
        if v == "isolation" or u == "isolation":
            edge_colors.append("#af7ac5")
        elif u == "linear" or v == "linear":
            edge_colors.append("#39ff8a")
        elif u == "entropy" or v == "entropy":
            edge_colors.append("#fad7a0")
        else:
            edge_colors.append("#2a3f57")

    nx.draw_networkx_edges(G, pos, ax=ax, edge_color=edge_colors,
        arrows=True, arrowsize=16, arrowstyle="-|>",
        width=1.8, alpha=0.85, connectionstyle="arc3,rad=0.08")

    for node_id, data in G.nodes(data=True):
        x, y = pos[node_id]
        circle = plt.Circle((x, y), radius=0.38, color=data["color"], alpha=0.18, zorder=1)
        ax.add_patch(circle)
        nx.draw_networkx_nodes(G, pos, nodelist=[node_id],
            node_color=data["color"], node_size=data["size"],
            alpha=0.92, edgecolors="#e8f4ff", linewidths=1.2, ax=ax)

    labels = {n: G.nodes[n]["label"] for n in G.nodes()}
    nx.draw_networkx_labels(G, pos, labels=labels,
        font_size=7.2, font_color="#e8f4ff", font_weight="bold", ax=ax)

    ax.plot([4.0, 8.2], [-2.9, -2.9], color="#39ff8a", linewidth=1.5, linestyle="-")
    ax.text(6.1, -3.25,
        "LINEAR MEMORY SANDBOX — deterministic, zero-GC hot path",
        ha="center", fontsize=10, color="#39ff8a",
        fontweight="bold", family="monospace")

    ax.set_title("VXR-Sandbox Phase 2 — Deterministic Memory Isolation Architecture",
        fontsize=14, fontweight="bold", pad=16, color="#e8f4ff")
    ax.text(0.5, -0.08,
        "Applied Research · Voxion Labs · Client-side LLM prompt injection defense",
        transform=ax.transAxes, ha="center", fontsize=9, color="#7d8da3")
    ax.axis("off")
    ax.set_xlim(-1.2, 9.8)
    ax.set_ylim(-3.8, 3.2)

    legend_patches = [
        mpatches.Patch(color="#39ff8a", label="Wasm linear memory zone"),
        mpatches.Patch(color="#fad7a0", label="Shannon entropy gate"),
        mpatches.Patch(color="#f5b041", label="C++ kernel execution"),
        mpatches.Patch(color="#ff3b5c", label="Static output buffer (non-freeable)"),
        mpatches.Patch(color="#af7ac5", label="Isolation boundary"),
    ]
    ax.legend(handles=legend_patches, loc="upper left", fontsize=8, framealpha=0.25)

    fig.savefig(OUTPUT_ARCH, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f"[OK] Wrote {OUTPUT_ARCH}")


def main() -> None:
    print("VXR-Sandbox Phase 2 — telemetry visual generator")
    generate_latency_chart()
    generate_architecture_tree()
    print("Done.")


if __name__ == "__main__":
    main()
