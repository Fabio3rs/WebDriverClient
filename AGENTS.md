# Repository Guidelines

## Onboarding & Required Reading
Start on branch `dev_bidi` and treat the library as pre-release. Read `map.md`, `CLAUDE.md`, and `.github/copilot-instructions.md` to absorb architecture, threading model, and workflow rules before attempting changes. Do not edit `map.md` without explicit approval; keep personal notes separately until instructed otherwise.

## Project Structure & Module Organization
Core BiDi automation code lives in `src/` with public headers in `include/` (grouped by `bidi/`, asyncx utilities, and networking RAII). Tests reside in `tests/` with fixtures under `tests/html`; CTest scratch data falls into `Testing/Temporary` and must stay untracked. Supporting docs sit at repo root and in `docs/`, while runnable demos and helper scripts land in `examples/` and `tools/`. Treat `build/` and `conan-build/` as disposable local outputs.

## Workflow Protocol
Follow the repository's verification loop before any implementation: 1) use `rg` (or `grep`) to confirm identifier availability and locate similar behavior; 2) read the existing code path; 3) extend instead of duplicating functionality; 4) rebuild and run tests. Enforce the single-source rule—one identifier per scope and no repeated logic. Run `./format-code.sh` after edits so clang-format and trailing-whitespace cleanup stay consistent.

## Build, Test & Tooling Commands
Standard development build:
```bash
cmake -S . -B build -DWEBDRIVER_USE_CONAN=ON -G Ninja
cmake --build build -j $(nproc)
```
For system packages, drop the Conan flag and export `CC`/`CXX` to clang-15. Rebuild focused targets via `cmake --build build --target <name>`. Format manually as needed with `clang-format -i <file>`.

## Coding Style & Naming Conventions
Target C++20/23 with 4-space indentation, same-line braces, and includes ordered from project headers to STL. Filenames stay `snake_case`; classes and RAII wrappers use `PascalCase`; free functions default to `snake_case`. Maintain const-correctness, prefer RAII, and never allow arena-backed pointers or `string_view`s to escape handlers. Emojis or novelty glyphs are prohibited in identifiers, commits, branches, or logs.

## Testing Workflow
Build the test runner with `cmake --build build --target webdriverclientcpp_tests` and execute `ctest --test-dir build --output-on-failure`. Browser-dependent suites require `python3 -m http.server 8080` inside `tests/html` and `chromedriver --port=9515`; stop both after use. For focused runs, invoke `./webdriverclientcpp_tests --gtest_filter=SuiteName.*` from `build/`. Add new regression files following the `*_test.cpp` pattern.

## Commit & Pull Request Guidelines
Use short imperative commit subjects (≈50 chars) mirroring the existing log (e.g., "Clang-tidy"). PRs must explain the intent, reference related issues, summarize local testing (include external services started), and attach logs or screenshots when behavior shifts. Ensure `ctest` passes before requesting review and call out any skipped scenarios or outstanding risks.
