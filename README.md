# WebDriverClient

A C++ implementation of a WebDriver client that directly interacts with Chrome WebDriver for browser automation. This project provides a interface for automating web tasks without relying on Selenium.

## Disclaimer

This project is currently **experimental** and has not undergone thorough testing. As a result, it may contain bugs or behave unexpectedly in certain scenarios. Users are strongly advised to **proceed with caution** and conduct comprehensive testing before using this project in any critical systems or production environments.

The code is provided **"as is"**, without any warranties or guarantees of any kind. By using this project, users assume all risks associated with its use, including but not limited to potential issues with stability, performance, or security.


## Build and Test Instructions

This project uses CMake for the build system, along with Clang and various dependencies for testing and code quality checks. The testing framework includes `googletest` and integration with a (Chrome-only tested) WebDriver for browser automation.

## Prerequisites

Ensure that the following dependencies are installed on your system before building and testing the project:

- Clang or GCC (Build examples with Clang 15)
- CMake 3.16.x
- Ninja build system
- GoogleTest framework
- Chromium ChromeDriver
- Python 3.x

For Debian/Ubuntu-based systems, the dependencies can be installed using the following commands:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake clang-15 clang-tidy-15 clang-format ninja-build
sudo apt-get install libcurl4 libcurl4-openssl-dev libpoco-dev libgtest-dev googletest
sudo apt-get install python3-pip chromium-chromedriver
```

## Build Instructions

1. **Clone the repository** (ensure to include submodules):
   ```bash
   git clone --recurse-submodules https://github.com/Fabio3rs/WebDriverClient
   cd WebDriverClient
   ```

2. **Create a build directory**:
   ```bash
   mkdir -p build
   ```

3. **Configure the project with CMake**:
   ```bash
   cd build
   export CC=$(which clang-15)
   export CXX=$(which clang++-15)
   cmake .. -G Ninja
   ```

4. **Build the project**:
   ```bash
   cmake --build . --config Debug --target all -j $(nproc)
   ```

## Test Instructions

1. **Start a Python webserver for testing** (necessary if tests involve browser interactions):
   ```bash
   pushd tests/html
   python3 -m http.server 8080 &
   popd
   ```

2. **Start ChromeDriver**:
   ```bash
   chromedriver --port=9515 &
   ```

3. **Run the tests**:
   ```bash
   cd build
   ctest -j 20 -C Debug -T test --output-on-failure
   ```

4. **Stop the servers after testing**:
   ```bash
   killall python3
   killall chromedriver
   ```
