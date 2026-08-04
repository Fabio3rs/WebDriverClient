# Network interception

The project exposes typed WebDriver BiDi network interception through
`NetworkInterceptHandler` and through `AutomationSession` configuration. This
document describes the maintained surface; it is not a claim of complete W3C
Network-module coverage.

## Supported surface

`NetworkInterceptConfig` selects interception phases, URL patterns, a policy,
and optional callbacks. Built-in policies cover continuing or failing matched
traffic; custom callbacks can choose a request, response, authentication, or
failure resolution supported by the handler's types.

`AutomationSessionBuilder` is the preferred application entry point when
interception is part of session configuration. Direct
`NetworkInterceptHandler::create(...)` is available to lower-level `Client`
users who need explicit handler ownership.

Protocol values and command serialization live under:

- `include/bidi/types/network.hpp`;
- `include/bidi/commands/network.hpp`;
- `include/bidi/network_intercept_handler.hpp`;
- `src/bidi_network_intercept_handler.cpp`.

Boost.JSON is used for protocol serialization. Handlers must not retain arena-
backed values or views into an incoming frame.

## Lifecycle

Creating a handler registers the intercept and required event subscriptions.
The handler must remain alive while traffic is being intercepted. Cleanup
removes protocol interception and subscriptions before the client and executor
are destroyed. `AutomationSession` owns this sequence when network interception
is configured through the facade.

Custom callbacks should return promptly. CPU-heavy policy decisions belong on
a separate executor and need an explicit timeout strategy so browser requests
are not left paused indefinitely.

## Validation boundaries

Unit tests cover configuration, serialization, validation, and routing without
requiring a browser. Integration tests under `tests/network_intercept_*` require
a compatible browser/driver and, where applicable, the local HTML fixture
server.

Because browser and driver behavior changes, feature status must come from the
current test run. Record skipped prerequisites and observed failures in the PR;
do not preserve percentages or fixed test totals here.

When extending interception, add the protocol type and command first, preserve
structured errors, cover cleanup and late-event behavior, then expose the
smallest high-level builder option that improves the common path.
