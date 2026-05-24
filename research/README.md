# VXR-Sandbox Research Assets

## Generate figures

```bash
pip install -r requirements.txt
python generate_visuals.py
```

Outputs:
- `latency_chart.png` — OHLC + distribution telemetry (10,000 injections)
- `arch_tree.png` — deterministic memory isolation tree

## Compile IEEE paper

Requires a LaTeX distribution with `IEEEtran` (TeX Live / MiKTeX).

```bash
pdflatex VXR_Sandbox_Paper.tex
pdflatex VXR_Sandbox_Paper.tex
```

Copy the PDF for GitHub Pages:

```bash
mkdir -p ../docs/whitepaper
cp VXR_Sandbox_Paper.pdf ../docs/whitepaper/VXR_Sandbox_Research.pdf
```

## Assets

| File | Description |
| --- | --- |
| `Voxion_Labs_Logo.png` | Official Voxion Labs broken-cube logo (paper header) |
| `rudranarayan_jena.png` | Author portrait (README) |
