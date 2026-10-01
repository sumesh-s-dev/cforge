#!/usr/bin/env python3
"""Generate static docs HTML from content/*.md with shared layout."""
import html
import os
import re

ROOT = os.path.dirname(os.path.abspath(__file__))
CONTENT = os.path.join(ROOT, "content")
OUT = ROOT

NAV = """
<a class="logo" href="{root}index.html">CForge</a>
<section><h2>Learn</h2><ul>
<li><a href="{root}learn/introduction.html">Introduction</a></li>
<li><a href="{root}learn/install.html">Installation</a></li>
<li><a href="{root}learn/first-program.html">First program</a></li>
<li><a href="{root}learn/language.html">Language reference</a></li>
<li><a href="{root}learn/memory.html">Memory &amp; slices</a></li>
<li><a href="{root}learn/compiler.html">Compiler</a></li>
<li><a href="{root}learn/runtime.html">Runtime &amp; HTTP</a></li>
<li><a href="{root}learn/machine.html">Machine backend</a></li>
</ul></section>
<section><h2>Specification</h2><ul>
<li><a href="{root}spec/index.html">Backend ecosystem</a></li>
<li><a href="{root}spec/boundary.html">Language boundary</a></li>
<li><a href="{root}spec/networking.html">Networking</a></li>
<li><a href="{root}spec/http-async.html">HTTP &amp; async</a></li>
<li><a href="{root}spec/memory.html">Memory</a></li>
<li><a href="{root}spec/database.html">Database &amp; cache</a></li>
<li><a href="{root}spec/libraries.html">Libraries</a></li>
<li><a href="{root}spec/toolchain.html">Toolchain &amp; IR</a></li>
<li><a href="{root}spec/production.html">Production</a></li>
</ul></section>
<section><h2>Reference</h2><ul>
<li><a href="{root}reference/toolchain.html">Toolchain commands</a></li>
<li><a href="{root}reference/runtime-api.html">Runtime API</a></li>
<li><a href="{root}reference/environment.html">Environment</a></li>
<li><a href="{root}reference/safety.html">Safety modes</a></li>
</ul></section>
<section><h2>Project</h2><ul>
<li><a href="{root}status.html">Implementation status</a></li>
<li><a href="https://github.com/sumesh-s-dev/cforge">GitHub</a></li>
</ul></section>
"""


def md_to_html(text):
    lines = text.splitlines()
    out = []
    in_pre = False
    in_ul = False
    in_table = False

    def close_ul():
        nonlocal in_ul
        if in_ul:
            out.append("</ul>")
            in_ul = False

    def close_table():
        nonlocal in_table
        if in_table:
            out.append("</table>")
            in_table = False

    def close_blocks():
        close_ul()
        close_table()

    for line in lines:
        if line.startswith("```"):
            close_blocks()
            if in_pre:
                out.append("</code></pre>")
                in_pre = False
            else:
                out.append("<pre><code>")
                in_pre = True
            continue
        if in_pre:
            out.append(html.escape(line))
            continue
        if line.startswith("### "):
            close_blocks()
            out.append("<h3>%s</h3>" % html.escape(line[4:]))
        elif line.startswith("## "):
            close_blocks()
            out.append("<h2>%s</h2>" % html.escape(line[3:]))
        elif line.startswith("# "):
            close_blocks()
            out.append("<h1>%s</h1>" % html.escape(line[2:]))
        elif line.strip() == "":
            continue
        elif line.startswith("|"):
            close_ul()
            if not in_table:
                out.append("<table>")
                in_table = True
            cells = [c.strip() for c in line.strip("|").split("|")]
            if all(re.match(r"^[-:]+$", c) for c in cells):
                continue
            tag = "th" if out[-1] == "<table>" else "td"
            out.append(
                "<tr>"
                + "".join("<%s>%s</%s>" % (tag, inline_md(c), tag) for c in cells)
                + "</tr>"
            )
        elif line.startswith("- "):
            close_table()
            if not in_ul:
                out.append("<ul>")
                in_ul = True
            out.append("<li>%s</li>" % inline_md(line[2:]))
        elif re.match(r"^\s*<", line):
            close_blocks()
            out.append(line.strip())
        else:
            close_blocks()
            out.append("<p>%s</p>" % inline_md(line))
    if in_pre:
        out.append("</code></pre>")
    close_blocks()
    return "\n".join(out)


def inline_md(s):
    s = html.escape(s)
    s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', s)
    return s


def page(rel_path, title, body_html, root_prefix):
    nav = NAV.format(root=root_prefix)
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%s — CForge</title>
<link rel="stylesheet" href="%sassets/site.css">
</head>
<body>
<div class="layout">
<nav class="sidebar">%s</nav>
<main>%s
<footer class="doc-footer">CForge documentation · <a href="https://github.com/sumesh-s-dev/cforge">Source</a></footer>
</main>
</div>
</body>
</html>""" % (
        html.escape(title),
        root_prefix,
        nav,
        body_html,
    )


def write_page(out_path, title, md_text, root_prefix):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    body = md_to_html(md_text)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(page(out_path.replace(OUT + os.sep, ""), title, body, root_prefix))


def main():
    for dirpath, _, files in os.walk(CONTENT):
        for name in files:
            if not name.endswith(".md"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), CONTENT)
            out_rel = rel[:-3] + ".html"
            out_path = os.path.join(OUT, out_rel)
            depth = out_rel.count("/")
            root_prefix = "../" * depth if depth else ""
            with open(os.path.join(dirpath, name), encoding="utf-8") as f:
                md = f.read()
            title = md.split("\n", 1)[0].lstrip("# ").strip()
            write_page(out_path, title, md, root_prefix)
    print("built docs")


if __name__ == "__main__":
    main()
