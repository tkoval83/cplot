# AGENTS.md

This file gives repo‑specific guidance to AI agents and contributors. It applies to the entire repository unless a more specific AGENTS.md is added in a subdirectory.

## Project Summary
- Name: cplot (C CLI template)
- Language/Std: C (GNU99)
- Build: Makefile (no external build system required)
- Layout: `src/` sources, `lib/` object files, `bin/` binaries, `test/` cmocka tests

## Build, Run, Test
- Build (dev): `make -j`
- Build (release): `make release` (optimized, stripped)
- Run: `./bin/cplot --help`
- Tests: `make tests` (disabled; no third-party deps)
- Valgrind: `make valgrind` (log at `log/valgrind.log`)
- Clean: `make clean`

## Packaging & Distribution
- Version lives in `project.conf` as `VERSION := x.y.z`.
- Install: `make install [PREFIX=/usr/local] [DESTDIR=…]`
- Uninstall: `make uninstall`
- Dist tarball: `make dist` → `dist/cplot-<VERSION>-<os>-<arch>.tar.gz` and `.sha256`
- Release workflow: GitHub Actions (`.github/workflows/release.yml`) builds and uploads artifacts for tags like `v0.1.0`.
- If you change the binary name or versioning, update `project.conf`, `.zed/tasks.json`, `.zed/debug.json`, and README accordingly.

## Editor & Zed
- Project Zed configs live under `.zed/`:
  - `tasks.json`: build, run, tests, valgrind, clean
  - `debug.json`: CodeLLDB launch for `bin/cplot`
  - `settings.json`: per‑project language settings (format on save, indent, wrap)
- Inline AI completions:
  - Project setting prefers GitHub Copilot: `features.edit_prediction_provider = "copilot"`.
  - Authentication and Agent Panel provider selection are user‑level; do not commit tokens or secrets.

## Style & Formatting
- Code formatting is governed by:
  - `.clang-format`: GNU base, 4 spaces, 100 column limit
  - `.editorconfig`: LF, UTF‑8, trim trailing whitespace, 4‑space indent for C/C headers
- Language standard: GNU99, with strict warnings and stack protection (see `Makefile`).
- Keep changes minimal and consistent with existing style; prefer small, focused diffs.

### Pre‑commit checklist (обов’язково)
- Документація: усі публічні та внутрішні API мають бути задокументовані в `.h` і `.c` (Doxygen‑стиль, див. нижче).
- Форматування: запустіть `make fmt` і переконайтеся, що дифи лише за суттю.
- Якість збірки: `make -j` без попереджень (див. Portability & Quality).
- README/docs: оновіть опис і приклади, якщо зміни впливають на CLI чи публічні API.
- Пам’ять/валідність (за можливості): прогон `make valgrind` без витоків/помилок.

### Документація та коментарі (обов'язково)
- Усі публічні та внутрішні функції МАЮТЬ бути задокументовані як у заголовках (`.h`), так і у реалізаціях (`.c`).
- Стиль документації — Doxygen: короткий опис, далі теги `@param` для кожного параметра та `@return` для значень, за потреби `@note`, `@warning`.
- Однорядкові коментарі — `//` або `///` (для коротких док‑рядків над деклараціями).
- Багаторядкові док‑блоки — `/** ... */`, причому текст починається з нового рядка після `/**` і кожен рядок починається з ` *` (без тексту поруч із `/**`).
- Увесь супровідний текст — українською (див. Політику мови нижче). Ідентифікатори коду (імена файлів/функцій/змінних) англійською.
- Повідомлення журналу (лог), повідомлення про помилки й текст CLI — українською, без винятків.
- При додаванні/зміні API оновіть коментарі у відповідних `.h` і `.c`. PR без належної документації не приймаються.

### Language Policy (UA‑only)
- Всі коментарі у коді, повідомлення журналу (log) та повідомлення про помилки повинні бути українською мовою.
- Документація (README, spec, docs/*) також ведеться українською.
- Дозволяється використовувати англійські ідентифікатори (імена файлів/функцій/змінних), але супровідний текст — лише українською.

## Clangd / LSP
- `.clangd` forces C mode and adds common flags and include paths for good indexing.
- If migrating to CMake, enable `CMAKE_EXPORT_COMPILE_COMMANDS=ON` and consider simplifying `.clangd` flags.
- Do not commit `compile_commands.json` unless explicitly requested.

## Code Organization
- Place implementation in `src/`, headers in `src/` as appropriate.
- The Makefile automatically discovers `src/*.c` and writes `lib/*.o`, linking to `bin/cplot`.
- Keep public interfaces in headers; avoid one‑letter variable names; avoid unnecessary macros.
- Prefer standard library; avoid introducing new dependencies without approval.

## Portability & Quality
- Target macOS and Linux.
- Use `-Wall -Wextra` and keep builds warning‑free.
- Ensure memory is freed appropriately; consider adding tests for new functionality.
- If you add features that need OS‑specific behavior, guard with `#ifdef` and document in README.

## README & Docs
- Update `README.md` when:
  - You add tasks, change build steps, or rename the binary.
  - You change packaging/release steps or versioning.
- Keep instructions concise and accurate.
 - Дотримуйтеся політики мови: усі описи, примітки та довідка — українською.
 - Додатково: при зміні поведінки CLI або публічних API — оновіть README та відповідні файли у `docs/`.

## Release Process (Summary)
1. Update `project.conf` `VERSION := x.y.z`.
2. `make dist` locally (optional) and verify.
3. Tag and push: `git tag vx.y.z && git push --tags`.
4. GitHub Actions will build artifacts for macOS/Linux and attach them to the release.

## Git & GitHub CLI
- Prefer the GitHub CLI `gh` for GitHub operations (PRs, issues, releases).
- Authenticate once with `gh auth login` (user-level; do not commit tokens/secrets).
- Common flows:
  - Pull requests: `gh pr create --fill` and `gh pr view --web`.
  - Repo status: `gh status` and `gh run list` (CI checks).
  - Releases: after tagging (see Release Process), optionally run
    `gh release create vx.y.z --generate-notes` if you want a manual release;
    CI already attaches built artifacts on tag push.
- Continue using plain `git` for local commits, branches, and tag creation.

## Do Nots
- Do not commit secrets, tokens, or machine‑specific paths.
- Do not change directory layout or binary name without updating configs and docs.
- Do not add unrelated tools or formatters.

## Contact Points
- If you need to extend build, prefer enhancing `Makefile` over introducing a new system.
- If adding CMake, keep Makefile intact unless instructed otherwise, and document migration clearly.
