# CForge documentation site

[Fumadocs](https://fumadocs.dev) on **Next.js** (static export) — shadcn-style UI, search, light/dark theme.

## Develop

```bash
npm ci
npm run dev
```

Open http://localhost:3000/cforge/docs (base path matches GitHub Pages).

## Build

```bash
npm run build
```

`prebuild` syncs markdown from `../docs/content/` into `content/docs/`.

Output: `out/` → copied to `gh-pages` by CI.

## Edit docs

Change files under **`docs/content/`** in the repo root, not here. Re-run dev or build to refresh.
