# Specification Constitution — cplot

Version: 2.2.0  
Date: 2025-09-14  
Scope: Applies to this repository and all specification‑driven work created under .specify/

Purpose
- Make specifications the source of truth. Plans and code are generated from specs and remain aligned through regeneration.
- Keep the system simple, testable, observable, and CLI‑driven, consistent with a C (GNU99) + Makefile project.

Article I — Library‑First
- Every feature begins as a reusable C module (header + source) with clear interfaces; the CLI links to these modules.
- No monolithic implementations: keep code in small, testable units under src/; expose only required headers.
- Avoid hidden global state; allow testability via explicit parameters.

Article II — CLI Interface Mandate
- All core capabilities are available through a text‑first CLI (stdin/args/files in; stdout/stderr out; exit codes).
- Commands must be scriptable and deterministic; user‑facing output is human‑readable by default.
- JSON output is optional and not required for v1.

Article III — Test‑First Imperative
- Write tests (golden/integration/unit) before implementing behavior. Red → Green → Refactor.
- Tests must be reproducible and runnable locally via Makefile targets (e.g., make tests). cmocka is optional but preferred.
- For new features, include quickstart validation that mirrors acceptance scenarios.

Article IV — Integration Testing Priority
- Prefer integration/e2e tests that exercise real flows (Markdown → layout → preview/plot planning) over isolated units when feasible.
- For device operations, provide a serial stub for CI while enabling real‑device runs locally.
- Maintain preview↔plot parity checks as a top‑level validation.

Article V — Observability
- Provide concise logs and warnings to stderr; keep stdout for primary CLI results.
- Surface non‑fatal degradations (e.g., missing glyph fallbacks) and fatal errors with actionable messages.
- Support verbosity flags (quiet/verbose) where useful; record run settings with preview artifacts when parity matters.

Article VI — Versioning & Change Control
- Follow semver for the CLI; store version in project.conf (VERSION := x.y.z).
- When behavior visible to users changes, update README and specs; add release notes and bump VERSION.
- Keep Makefile as the authoritative build; do not introduce alternative build systems without explicit approval.

Article VII — Simplicity
- Minimize moving parts: single project, single build, no new frameworks or heavy deps.
- Prefer the standard library and small, focused modules; avoid premature abstractions and YAGNI.
- Keep configuration simple (key=value and CLI flags). Defaults must be sensible and documented.

Article VIII — Anti‑Abstraction
- Use platforms directly (serial, file I/O, geometry ops) rather than layering wrappers without clear benefit.
- Prefer simple data models (Point/Path/Paths) over framework‑driven architectures.
- No plugin systems, DI frameworks, or code‑gen layers beyond spec‑kit without explicit need.

Article IX — Integration‑First Validation
- Define contracts (CLI commands, inputs/outputs, tolerances) before implementation.
- Include a quickstart guide per feature that exercises key acceptance scenarios.
- Parity checks (preview vs plot) are required where applicable; failures abort with clear guidance.

Enforcement Gates (applied during /plan)
- Simplicity Gate (Art. VII): ≤1 project; no new build systems; no speculative features.
- Anti‑Abstraction Gate (Art. VIII): direct use of platform features; single model representation for geometry.
- Test‑First Gate (Art. III): contracts and tests exist before implementation; quickstart defined.
- Integration Gate (Art. IV & IX): at least one integration test or quickstart scenario per feature.

Defaults for This Repository
- Language/Std: C (GNU99); Build: Makefile; Binary: bin/cplot.
- Preview defaults: PNG, 300 DPI, white background; parity check enabled where relevant.
- Device profile: AxiDraw MiniKit2 by default; safety bounds enforced; overflow default is fail (scale‑to‑fit/clip only on explicit override).

C Coding Standards (Linux‑friendly)
- Formatting: obey repository settings (.clang-format, .editorconfig). 4‑space indent, 100‑col limit, LF, UTF‑8, trim trailing whitespace.
- Headers: always use include guards (`#ifndef FOO_H` … `#define FOO_H` … `#endif`). Source includes its own header first, then system, then local headers. Use angle brackets for system headers and quotes for project headers.
- Naming:
  - files: lower_snake_case (e.g., `text_shape.c`, `fontreg.h`).
  - functions/variables: lower_snake_case.
  - macros and constants: UPPER_SNAKE_CASE with optional project prefix (e.g., `CPLOT_MAX_PATHS`).
  - types: project types may use `_t` suffix (e.g., `options_t`) for consistency with existing code; opaque structs are preferred over typedefs to pointer types.
- API design: prefer `int` return (0 success, non‑zero error); use `errno` semantics where appropriate in CLI; library modules return negative `errno` values or a project enum. Inputs are `const` where possible; outputs via pointer parameters.
- Memory: single clear owner per allocation; matching `init/free` pairs; on error paths, free owned resources before returning. Avoid hidden globals; prefer passing explicit context structs.
- Safety: compile with warnings enabled (Makefile sets strict warnings); keep builds warning‑free. Use `size_t` for sizes/lengths, `ssize_t` for signed counts; check for overflows when computing sizes.
- Inline utilities: use `static inline` in headers for tiny helpers; otherwise keep function definitions in `.c` files.
- Logging: stdout for primary CLI results; stderr for warnings/errors. Do not mix user output with diagnostics.
- Portability: target macOS/Linux; avoid non‑portable APIs unless guarded with `#ifdef` and documented.

Amendments
- Record version bumps here and in .specify/memory/constitution_update_checklist.md.
- When updating this constitution, also update template footers referencing the version.

History
- 2.2.0 (2025‑09‑14): Tailored to C project; clarified CLI/test/observability; added parity and overflow defaults.
- 2.1.1: Prior baseline referenced by templates.
