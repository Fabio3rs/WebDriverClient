# Examples

Every C++ source in this directory is registered as a CMake target and compiled
with the same warnings and sanitizers as the library.

## API levels

- `flow/example_automation_session_minimal.cpp` is the starting point for most
  applications.
- `flow/example_bidi_flow_production.cpp` shows configured
  `AutomationSessionBuilder` usage.
- `flow/example_bidi_flow_minimal.cpp` is the low-level example for users who
  need direct `bidi::Client` access.
- `flow/example_bidi_flow_script_exception.cpp` and
  `flow/example_log_monitoring.cpp` each focus on one protocol behavior.
- `scraping/` contains page-oriented workflows built on `AutomationSession`,
  including event-driven element waiting without client-side polling.
- `threading/` contains advanced executor and CPU-pool patterns.

Use one level consistently inside a workflow. Accessing `session.client()` is an
intentional escape hatch for features that are only exposed by `bidi::Client`,
such as event subscriptions; it should not be used to recreate session setup or
context management manually.

## Build

```bash
cmake -S . -B build -DWEBDRIVER_USE_CONAN=ON -G Ninja
cmake --build build -j $(nproc)
```

Runtime examples expect ChromeDriver at `http://localhost:9515`.
