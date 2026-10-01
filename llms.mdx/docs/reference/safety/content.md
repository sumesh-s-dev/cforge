# Safety modes (/docs/reference/safety)



CForge prioritizes **control** over automatic memory safety.

## Release 0.1 [#release-01]

| Mode                                           | Status                                      |
| ---------------------------------------------- | ------------------------------------------- |
| Explicit pointers and slices                   | **Implemented**                             |
| Request arena reset                            | **Implemented**                             |
| `@unsafe` / `@checked` attributes on functions | **Warned and ignored** (no enforcement yet) |
| `@safe` mode                                   | **Planned**                                 |

## Planned compiler modes [#planned-compiler-modes]

### @unsafe (default) [#unsafe-default]

No extra checks. Programmer responsible for lifetimes, data races, and use-after-free.

### @checked (debug / test builds) [#checked-debug--test-builds]

* Slice index bounds checks where indexing is added to the language
* Optional arena generation counter to trap use-after-reset in tests
* Sanitizer-friendly build profiles (ASan / UBSan / TSan when linked)

### @safe (application tier lint) [#safe-application-tier-lint]

* Static checks for storing request `Slice` into globals
* Stricter parser limits enforced at compile time

These modes do **not** make CForge Rust. They reduce obvious footguns without a borrow checker.

## What we do not promise [#what-we-do-not-promise]

* Freedom from use-after-free or data races in `@unsafe`
* Cryptographic correctness of hand-written Forge crypto (use audited libraries via runtime)

See [Introduction](../learn/introduction.html) for philosophy.
