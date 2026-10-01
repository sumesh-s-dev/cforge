## GitHub: fix 404 and empty About (2 minutes)

The **pages** workflow publishes docs to the `gh-pages` branch. GitHub will **not** serve them until you turn Pages on once.

### Step 1 — Enable Pages

1. Open: **https://github.com/sumesh-s-dev/cforge/settings/pages**
2. **Build and deployment → Source:** choose **Deploy from a branch**
3. **Branch:** `gh-pages` · **Folder:** `/ (root)` · click **Save**

### Step 2 — Fill About (right sidebar on repo home)

Click the **gear** next to “About” on https://github.com/sumesh-s-dev/cforge and set:

| Field | Value |
|---|---|
| **Description** | Low-level CForge language: dual compiler, epoll HTTP/TLS/SQLite server, full backend specification. |
| **Website** | https://sumesh-s-dev.github.io/cforge/ |
| **Topics** | `c`, `compiler`, `systems-programming`, `programming-language` |

(CI tries to set description automatically; GitHub often requires you to save About once as repo admin.)

### Step 3 — Wait and test

After 1–3 minutes:

```text
https://sumesh-s-dev.github.io/cforge/
```

Should show the CForge documentation home (not GitHub’s 404 page).

### If Deployments still show red

Open **Actions → pages → latest run**. A green run means `gh-pages` was updated. A red run is usually before Step 1; after Pages is enabled, re-run: **Actions → pages → Run workflow**.
