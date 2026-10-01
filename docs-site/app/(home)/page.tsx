import Link from 'next/link';

export default function HomePage() {
  return (
    <div className="mx-auto flex max-w-3xl flex-1 flex-col justify-center px-6 py-16">
      <p className="mb-3 text-sm font-medium uppercase tracking-widest text-fd-muted-foreground">
        Backend DSL &amp; systems-language experiment
      </p>
      <h1 className="mb-4 text-4xl font-semibold tracking-tight text-fd-foreground md:text-5xl">
        CForge
      </h1>
      <p className="mb-8 max-w-xl text-lg leading-relaxed text-fd-muted-foreground">
        A low-level backend DSL and systems-language experiment. CForge compiles a small typed
        language to C11 today, with an experimental native x86-64 backend. Its runtime provides
        bounded-memory HTTP/TLS/SQLite services on Linux — not a Rust/C competitor yet.
      </p>
      <div className="flex flex-wrap gap-3">
        <Link
          href="/docs"
          className="inline-flex h-10 items-center rounded-lg bg-fd-primary px-5 text-sm font-medium text-fd-primary-foreground transition-opacity hover:opacity-90"
        >
          Read the docs
        </Link>
        <a
          href="https://github.com/sumesh-s-dev/cforge"
          className="inline-flex h-10 items-center rounded-lg border border-fd-border px-5 text-sm font-medium text-fd-foreground transition-colors hover:bg-fd-accent"
          rel="noreferrer"
          target="_blank"
        >
          GitHub
        </a>
      </div>
    </div>
  );
}
