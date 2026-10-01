#!/usr/bin/env node
/** Copy docs/content → content/docs with frontmatter for Fumadocs. */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const srcRoot = path.join(root, '..', 'docs', 'content');
const dstRoot = path.join(root, 'content', 'docs');

function walk(dir, out = []) {
  for (const name of fs.readdirSync(dir)) {
    const p = path.join(dir, name);
    if (fs.statSync(p).isDirectory()) walk(p, out);
    else if (name.endsWith('.md')) out.push(p);
  }
  return out;
}

function toFrontmatter(rel, raw) {
  let body = raw.replace(/\r\n/g, '\n');
  body = body.replace(/<p class="hero">([\s\S]*?)<\/p>\n?/g, '$1\n\n');
  let title = 'Untitled';
  if (body.startsWith('# ')) {
    const nl = body.indexOf('\n');
    title = body.slice(2, nl === -1 ? undefined : nl).trim();
    body = nl === -1 ? '' : body.slice(nl + 1).replace(/^\n+/, '');
  }
  const desc = rel === 'index' ? 'CForge language and backend specification' : '';
  const fm = desc
    ? `---\ntitle: ${JSON.stringify(title)}\ndescription: ${JSON.stringify(desc)}\n---\n\n`
    : `---\ntitle: ${JSON.stringify(title)}\n---\n\n`;
  body = body.replace(/```forge/g, '```c');
  return fm + body;
}

function writeMeta(dir, title, pages) {
  fs.writeFileSync(
    path.join(dir, 'meta.json'),
    JSON.stringify({ title, pages }, null, 2) + '\n',
  );
}

if (fs.existsSync(dstRoot)) {
  fs.rmSync(dstRoot, { recursive: true, force: true });
}
fs.mkdirSync(dstRoot, { recursive: true });

for (const file of walk(srcRoot)) {
  const rel = path.relative(srcRoot, file).replace(/\.md$/, '');
  const dst = path.join(dstRoot, rel + '.md');
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.writeFileSync(dst, toFrontmatter(rel.replace(/\\/g, '/'), fs.readFileSync(file, 'utf8')));
}

writeMeta(dstRoot, 'CForge', ['index', 'status', 'learn', 'spec', 'reference']);
writeMeta(path.join(dstRoot, 'learn'), 'Learn', [
  'introduction',
  'install',
  'first-program',
  'language',
  'memory',
  'compiler',
  'runtime',
  'machine',
]);
writeMeta(path.join(dstRoot, 'spec'), 'Specification', [
  'index',
  'boundary',
  'networking',
  'http-async',
  'memory',
  'database',
  'libraries',
  'toolchain',
  'production',
]);
writeMeta(path.join(dstRoot, 'reference'), 'Reference', [
  'toolchain',
  'runtime-api',
  'environment',
  'safety',
]);

console.log('synced docs/content → docs-site/content/docs');
