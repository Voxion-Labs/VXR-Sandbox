# Deploy VXR-Sandbox to GitHub Pages (Public)

Everything is pre-built in `docs/` for immediate hosting. Follow these steps once.

## 1. Create GitHub repository

1. Go to [github.com/new](https://github.com/new)
2. Name it **`VXR-Sandbox`** (or update URLs in `README.md` if you use another name)
3. Do **not** add a README (this repo already has one)

## 2. Push this project

```powershell
cd "E:\Projects\Research\VXR - Sandbox"
git init
git add .
git commit -m "VXR-Sandbox: complete public release with Wasm kernel, UI, and IEEE paper"
git branch -M main
git remote add origin https://github.com/YOUR_ORG/VXR-Sandbox.git
git push -u origin main
```

Replace `YOUR_ORG` with your GitHub username or organization (e.g. `voxionlabs`).

## 3. Enable GitHub Pages

1. Open **Settings → Pages**
2. Under **Build and deployment**, set **Source** to **GitHub Actions**
3. The workflow `.github/workflows/deploy-pages.yml` runs on every push to `main`

First deploy builds figures, PDF, and Wasm in CI (optional refresh). Your committed `docs/` artifacts work immediately.

## 4. Live URLs

After deploy completes (~3–5 min):

| Resource | URL |
|----------|-----|
| **Dashboard** | `https://YOUR_ORG.github.io/VXR-Sandbox/` |
| **Research PDF** | `https://YOUR_ORG.github.io/VXR-Sandbox/whitepaper/VXR_Sandbox_Research.pdf` |

Update `README.md` badge links if your org name differs from `voxionlabs`.

## 5. Local preview

```powershell
cd docs
npx serve . -p 8080
```

Open http://localhost:8080 — wait for **Wasm kernel online**, then scan a prompt.

## Rebuild locally (optional)

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_all.ps1
```

This regenerates figures, PDF, and Wasm.

## What's included in `docs/`

- `index.html`, `style.css`, `app.js` — Cyber-Defense Dashboard
- `vxr_kernel.js`, `vxr_kernel.wasm` — compiled C++ kernel
- `whitepaper/VXR_Sandbox_Research.pdf` — IEEE-format applied research paper
