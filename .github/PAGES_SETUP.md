# Enable GitHub Pages (one time)

Your docs are built in CI. The site files are on the **`gh-pages`** branch. GitHub must be told to serve them.

## Option A — Recommended (GitHub Actions)

1. Open **https://github.com/sumesh-s-dev/cforge/settings/pages**
2. Under **Build and deployment**, set **Source** to **GitHub Actions**.
3. Re-run the **pages** workflow: **Actions** → **pages** → **Run workflow**.
4. After it goes green, open **https://sumesh-s-dev.github.io/cforge/** (wait 1–3 minutes).

## Option B — Branch deploy

1. Same **Settings → Pages** page.
2. Set **Source** to **Deploy from a branch**.
3. **Branch:** `gh-pages` · **Folder:** `/ (root)` · **Save**.
4. Wait 1–3 minutes, then open **https://sumesh-s-dev.github.io/cforge/**

## Repository About box

The **pages** workflow sets description and homepage via API on each push. If the About section is still empty, run workflow once after Option A is enabled.

**Description (copy if needed):**

```text
Low-level CForge language: dual compiler, epoll HTTP/TLS/SQLite server, full backend specification.
```

**Website:** `https://sumesh-s-dev.github.io/cforge/`

## Verify

```bash
curl -sI https://sumesh-s-dev.github.io/cforge/ | head -3
# expect HTTP/2 200
```
