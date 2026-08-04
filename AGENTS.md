# Repository Guidelines

## Before changing code

- The default development branch is `dev_bidi`. Verify the current branch and
  worktree state before editing; do not switch branches over unrelated local
  changes.
- Read `map.md`, `CLAUDE.md`, and `.github/copilot-instructions.md` first.
- Start discovery with `rg` or `rg --files`. Reuse or extend existing APIs
  instead of creating parallel implementations.
- Treat `map.md` as the architectural map, not as a changelog. Update it only
  when the user explicitly authorizes documentation changes.
- Preserve unrelated staged, unstaged, and untracked work.

## Architecture and API levels

- Core code lives in `src/`; public headers live in `include/` and
  `include/bidi/`.
- `bidi::AutomationSession` is the default API for application workflows.
- `bidi::Client` is the lower-level BiDi API. Use it when a feature is not
  exposed by `AutomationSession`, not to recreate lifecycle management.
- `asyncx::Async<T>` operations are lazy. A terminal operation such as
  `co_await` or `.finally()` must materialize them.
- Browser I/O is serialized by an Asio strand. Do not add mutexes around client
  calls or block an I/O handler with sleeps, CPU work, or `future.get()`.
- Maintain RAII ownership for the HTTP WebDriver session, BiDi client,
  subscriptions, timers, `io_context`, and worker threads.
- Never let arena-backed pointers, references, or `string_view` values escape a
  message handler.

## Project layout

- Tests: `tests/`; browser fixtures: `tests/html/`.
- Runnable examples: `examples/`; every example `.cpp` must be a CMake target.
- Documentation: `docs/`; repository map: `map.md`.
- Utilities and developer tools: `tools/`.
- `build/`, `conan-build/`, `Testing/Temporary/`, browser binaries, generated
  logs, and caches are build/runtime artifacts and must stay untracked.

## Build and test

Configure and build with Conan:

```bash
cmake -S . -B build -DWEBDRIVER_USE_CONAN=ON -G Ninja
cmake --build build -j $(nproc)
```

With system packages, set the intended `CC` and `CXX` and configure without the
Conan option.

Useful verification commands:

```bash
cmake --build build --target webdriverclientcpp_tests
ctest --test-dir build --output-on-failure
```

Browser-dependent tests require both services:

```bash
python3 -m http.server 8080 --directory tests/html
chromedriver --port=9515
```

Stop services started for testing. If LeakSanitizer cannot run during GoogleTest
discovery in a constrained debugger/sandbox environment, report that condition;
do not silently treat leak detection as passing.

## Code and documentation style

- Use C++20/23, four-space indentation, same-line braces, and project headers
  before external and standard-library headers.
- Use `snake_case` for files and free functions, and `PascalCase` for classes
  and RAII wrappers.
- Prefer explicit ownership and const-correct values. Pass small identifiers by
  value into coroutine frames when that makes lifetime unambiguous.
- Keep functions focused; extract named functions instead of adding deeply
  nested scopes or coroutine lambdas.
- Use `std::error_code` for normal asynchronous flow and exceptions at explicit
  synchronous/error boundaries.
- Do not use emojis or novelty glyphs in identifiers, filenames, branches,
  commits, or logs.
- Documentation must describe current behavior. Put historical investigation in
  Git history rather than permanent “final summary” or completed-checklist files.

## Examples

- Examples must have a successful default path and state external prerequisites.
- Prefer `AutomationSession` and typed extraction for common flows.
- When demonstrating `Client`, keep the associated `SessionGuard` alive until
  the BiDi connection and its I/O threads have finished.
- Use Asio timers or event completion for waiting; never busy-wait or use
  `std::this_thread::sleep_for` on an I/O path.
- Use `asyncx::all` only for independent lazy operations and keep returned RAII
  subscription handles alive for as long as events are needed.

## Formatting, commits, and PRs

- Run `./format-code.sh` when it will not rewrite unrelated user changes;
  otherwise format only the files in scope with `clang-format -i` and run
  `git diff --check`.
- Use short imperative commit subjects, approximately 50 characters.
- Do not commit or push without explicit user approval.
- PR descriptions should explain intent, relevant issues, tests run, external
  services used, skipped scenarios, and remaining risks.
