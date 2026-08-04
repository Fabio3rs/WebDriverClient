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
- CMake
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
## Configuração e Build

### Opções de Configuração

```cmake
# Dependências (configuráveis)
option(WEBDRIVER_USE_CONAN "Use Conan package manager" OFF)
option(WEBDRIVER_USE_BOOST "Enable Boost for BiDi protocol" ON)
option(WEBDRIVER_USE_POCO "Use Poco libraries" ON)

# Tipo de biblioteca
option(WEBDRIVER_BUILD_SHARED "Build shared library" OFF)
option(WEBDRIVER_HEADER_ONLY "Header-only library" OFF)
```

### Build Standalone

```bash
# Com Conan (recomendado para desenvolvimento)
mkdir build && cd build
cmake .. -DWEBDRIVER_USE_CONAN=ON
cmake --build .

# Sem Conan (system packages - produção)
sudo apt install libcurl4-openssl-dev libboost-all-dev libpoco-dev  # Ubuntu
mkdir build && cd build
cmake .. -DWEBDRIVER_USE_CONAN=OFF
cmake --build .
```

### Como Biblioteca em Outros Projetos

```cmake
# Método 1: Como subprojeto
add_subdirectory(webdriverclientcpp)
target_link_libraries(myapp WebDriverClient::webdriverclientcpp)

# Método 2: Via find_package (após install)
find_package(WebDriverClient REQUIRED)
target_link_libraries(myapp WebDriverClient::webdriverclientcpp)
```3. **Configure the project with CMake**:
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

## Download chromedriver
https://developer.chrome.com/docs/chromedriver/downloads/version-selection

## Usage instructions
    ***Note:*** The WebDriverClient is currently only compatible with Chrome WebDriver. Ensure that the ChromeDriver is installed and running before using the WebDriverClient.
    ***TODO:*** Add instructions for installing and running ChromeDriver. Improve the error handling and usability of the WebDriverClient.

    ```cpp
        WebDriver browser("http://localhost:9515"); // Creates a new WebDriver instance with the specified URL for the WebDriver server

        browser.connect(); // Connects the browser to the WebDriver server

        browser.get("https://www.google.com"); // Navigates to the specified URL
    ```
