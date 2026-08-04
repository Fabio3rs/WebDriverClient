cmake_minimum_required(VERSION 3.16)
project(webdriverclientcpp
    VERSION 1.0.0
    DESCRIPTION "WebDriver Client Library for C++"
    LANGUAGES CXX C)

# =============================================================================
# Library Configuration Options (for library consumers)
# =============================================================================
option(WEBDRIVER_USE_CONAN "Use Conan package manager for dependencies" OFF)
option(WEBDRIVER_USE_BOOST "Enable Boost support for BiDi protocol" ON)
option(WEBDRIVER_USE_POCO "Use Poco libraries (alternative to system packages)" ON)
option(WEBDRIVER_BUILD_SHARED "Build shared library instead of static" OFF)
option(WEBDRIVER_HEADER_ONLY "Build as header-only library" OFF)
option(WEBDRIVER_USE_NLOHMANN_JSON "Enable optional nlohmann_json integration (alternative JSON backend)" OFF)

# =============================================================================
# Development Options (only when building standalone, not as subproject)
# =============================================================================
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(WEBDRIVER_STANDALONE_BUILD ON)
else()
    set(WEBDRIVER_STANDALONE_BUILD OFF)
endif()

option(WEBDRIVER_BUILD_EXAMPLES "Build example executables" ${WEBDRIVER_STANDALONE_BUILD})
option(WEBDRIVER_BUILD_TESTS "Build and run tests" ${WEBDRIVER_STANDALONE_BUILD})
option(WEBDRIVER_ENABLE_SANITIZERS "Enable sanitizers in debug builds" ${WEBDRIVER_STANDALONE_BUILD})
option(WEBDRIVER_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" ${WEBDRIVER_STANDALONE_BUILD})
option(WEBDRIVER_STRIP_LOG_LOCATION "Strip source location from logs" OFF)

# Require native thread primitives and kernel wait primitives (timers, io
# context, thread pool). When ON a preprocessor macro is defined so
# consumer code and headers can static-assert or document assumptions.
option(WEBDRIVER_REQUIRE_THREAD_PRIMITIVES "Require native thread primitives (io_context, thread_pool, steady_timer)" ON)
if(WEBDRIVER_REQUIRE_THREAD_PRIMITIVES)
    add_compile_definitions(WEBDRIVER_REQUIRE_THREAD_PRIMITIVES=1)
endif()

# =============================================================================
# C++ Standard and Compiler Settings
# =============================================================================
# Prefer C++23 when BiDi (Boost) support is enabled to allow coroutines, std::print
# and other C++23 ergonomics; otherwise keep C++20 for maximum compatibility.
if(WEBDRIVER_USE_BOOST)
    # If Boost/BiDi requested, prefer C++23
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Use standard C++, not GNU extensions by default

if(WEBDRIVER_STANDALONE_BUILD)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# =============================================================================
# Dependency Management
# =============================================================================
set(WEBDRIVER_REQUIRED_LIBS)
set(WEBDRIVER_OPTIONAL_LIBS)

# Conan setup (optional)
if(WEBDRIVER_USE_CONAN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/conan.cmake")
        include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/conan.cmake)

        conan_cmake_autodetect(CONAN_SETTINGS)
        conan_cmake_run(CONANFILE conanfile.txt
                        BASIC_SETUP CMAKE_TARGETS
                        BUILD missing
                        SETTINGS ${CONAN_SETTINGS}
                        ENV "CXXFLAGS=-fdata-sections -ffunction-sections"
                        ENV "CFLAGS=-fdata-sections -ffunction-sections")

        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS conanfile.txt)

        if(EXISTS "${CMAKE_BINARY_DIR}/conanbuildinfo.cmake")
            include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
            conan_basic_setup()
            message(STATUS "CONAN_LIBS: ${CONAN_LIBS}")
            set(WEBDRIVER_CONAN_AVAILABLE ON)
            list(APPEND WEBDRIVER_REQUIRED_LIBS ${CONAN_LIBS})
        else()
            message(WARNING "Conan setup failed, falling back to system packages")
            set(WEBDRIVER_USE_CONAN OFF)
        endif()
    else()
        message(WARNING "Conan cmake file not found, falling back to system packages")
        set(WEBDRIVER_USE_CONAN OFF)
    endif()
endif()

# System packages fallback
if(NOT WEBDRIVER_USE_CONAN)
    # Essential dependencies
    find_package(CURL REQUIRED)
    list(APPEND WEBDRIVER_REQUIRED_LIBS CURL::libcurl)

    # Optional: Boost for BiDi protocol
    if(WEBDRIVER_USE_BOOST)
        # Handle modern CMake Boost policy
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
            cmake_policy(SET CMP0167 NEW)
        endif()

        # Request json component as well for Boost.JSON linking when available
        find_package(Boost COMPONENTS system thread json QUIET)
        if(Boost_FOUND)
            list(APPEND WEBDRIVER_OPTIONAL_LIBS Boost::system Boost::thread)
            if(TARGET Boost::json)
                list(APPEND WEBDRIVER_OPTIONAL_LIBS Boost::json)
            endif()
            # Try to find coroutine component (C++20 feature)
            find_package(Boost COMPONENTS coroutine QUIET)
            if(TARGET Boost::coroutine)
                list(APPEND WEBDRIVER_OPTIONAL_LIBS Boost::coroutine)
            endif()
            set(WEBDRIVER_BOOST_AVAILABLE ON)
            message(STATUS "Boost found - BiDi protocol support enabled")
        else()
            message(STATUS "Boost not found - BiDi protocol support disabled")
            set(WEBDRIVER_USE_BOOST OFF)
        endif()
    endif()

    # Optional: Poco libraries
    if(WEBDRIVER_USE_POCO)
        find_package(Poco COMPONENTS Foundation JSON Net NetSSL Crypto QUIET)
        if(Poco_FOUND)
            list(APPEND WEBDRIVER_OPTIONAL_LIBS
                Poco::Foundation Poco::JSON Poco::Net Poco::NetSSL Poco::Crypto)
            set(WEBDRIVER_POCO_AVAILABLE ON)
            message(STATUS "Poco found - enhanced networking support enabled")
        else()
            message(STATUS "Poco not found - using alternative JSON implementation")
            set(WEBDRIVER_USE_POCO OFF)
        endif()
    endif()

    # Optional: nlohmann_json (header-only) as alternative/auxiliary JSON backend
    if(WEBDRIVER_USE_NLOHMANN_JSON)
        find_package(nlohmann_json QUIET)
        if(nlohmann_json_FOUND)
            list(APPEND WEBDRIVER_OPTIONAL_LIBS nlohmann_json::nlohmann_json)
            set(WEBDRIVER_NLOHMANN_AVAILABLE ON)
            message(STATUS "nlohmann_json found - alternative JSON backend enabled")
        else()
            message(STATUS "nlohmann_json not found - disabling optional JSON backend")
            set(WEBDRIVER_USE_NLOHMANN_JSON OFF)
        endif()
    endif()
endif()

# =============================================================================
# Compiler-Specific Flags (Professional Configuration)
# =============================================================================
function(configure_compiler_flags target_name)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target_name} PRIVATE
            # Core warnings (NASA P10 + C++ Core Guidelines)
            -Wall -Wextra -Wpedantic
            -Wformat-security -Wconversion -Wsign-conversion
            -Wshadow -Wnon-virtual-dtor -Wold-style-cast
            -Wcast-align -Wunused -Woverloaded-virtual
            -Wpedantic -Wmisleading-indentation -Wduplicated-cond
            -Wduplicated-branches -Wlogical-op -Wnull-dereference
            -Wuseless-cast -Wdouble-promotion -Wformat=2

            # Security hardening
            -fstack-protector-strong -D_FORTIFY_SOURCE=2

            # Performance optimizations
            -fdata-sections -ffunction-sections
        )

        if(WEBDRIVER_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target_name} PRIVATE
            # Comprehensive warnings
            -Wall -Wextra -Wpedantic -Wmost
            -Wconversion -Wsign-conversion -Wshadow
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
            -Wunused -Woverloaded-virtual -Wpedantic
            -Wmisleading-indentation
            -Wnull-dereference -Wdouble-promotion
            -Wno-unused-private-field

            # Clang-specific safety
            -Wthread-safety -Wthread-safety-beta

            # Disable noisy warnings for library use
            -Wno-c++98-compat -Wno-c++98-compat-pedantic
            -Wno-padded -Wno-global-constructors
            -Wno-exit-time-destructors -Wno-weak-vtables
            -Wno-documentation -Wno-documentation-unknown-command

            # Security hardening
            -fstack-protector-strong -D_FORTIFY_SOURCE=2

            # Performance optimizations
            -fdata-sections -ffunction-sections
        )

        if(WEBDRIVER_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE
            /W4          # High warning level
            /permissive- # Strict conformance
            /Zc:__cplusplus # Correct __cplusplus macro
            /utf-8       # UTF-8 source and execution charset
        )

        if(WEBDRIVER_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()

        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()
endfunction()

# =============================================================================
# Sanitizers Configuration (Debug builds only)
# =============================================================================
function(configure_sanitizers target_name)
    if(WEBDRIVER_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target_name} PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=leak
                -g
            )

            target_link_options(${target_name} PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=leak
            )
        endif()
    endif()
endfunction()

# =============================================================================
# Feature Detection and Preprocessor Definitions
# =============================================================================
function(configure_feature_macros target_name)
    target_compile_definitions(${target_name} PRIVATE
        CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(WEBDRIVER_STRIP_LOG_LOCATION)
        target_compile_definitions(${target_name} PUBLIC WEBDRIVER_STRIP_LOG_LOCATION=1)
    endif()

    if(WEBDRIVER_BOOST_AVAILABLE)
        target_compile_definitions(${target_name} PUBLIC WEBDRIVER_HAS_BOOST=1)
    endif()

    if(WEBDRIVER_POCO_AVAILABLE)
        target_compile_definitions(${target_name} PUBLIC WEBDRIVER_HAS_POCO=1)
    endif()

    if(WEBDRIVER_CONAN_AVAILABLE)
        target_compile_definitions(${target_name} PUBLIC WEBDRIVER_HAS_CONAN=1)
    endif()

    if(WEBDRIVER_NLOHMANN_AVAILABLE)
        target_compile_definitions(${target_name} PUBLIC WEBDRIVER_HAS_NLOHMANN_JSON=1)
    endif()
endfunction()

# =============================================================================
# Library Target Creation
# =============================================================================

# Collect source files (excluding examples)
file(GLOB_RECURSE WEBDRIVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER WEBDRIVER_SOURCES EXCLUDE REGEX "main\\.cpp$")

# Ensure new BiDi threading and optimization files are included
list(APPEND WEBDRIVER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bidi_threading.cpp"          # Core threading
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bidi_session_threaded.cpp"   # BiDi session
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bidi_memory_pool.cpp"        # JSON arena allocation
)

# Create library target
if(WEBDRIVER_BUILD_SHARED)
    add_library(webdriverclientcpp SHARED ${WEBDRIVER_SOURCES})
    set_target_properties(webdriverclientcpp PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
else()
    add_library(webdriverclientcpp STATIC ${WEBDRIVER_SOURCES})
endif()

# Configure library
target_include_directories(webdriverclientcpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
# (inserindo SYSTEM include dirs para reduzir warnings de terceiros)
foreach(_dep IN LISTS WEBDRIVER_REQUIRED_LIBS WEBDRIVER_OPTIONAL_LIBS)
    if(TARGET ${_dep})
        get_target_property(_inc_dirs ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
        if(_inc_dirs)
            # Adiciona cada include como SYSTEM
            foreach(_dir IN LISTS _inc_dirs)
                if(EXISTS "${_dir}")
                    target_include_directories(webdriverclientcpp SYSTEM PUBLIC $<BUILD_INTERFACE:${_dir}>)
                endif()
            endforeach()
        endif()
    endif()
endforeach()

# Mantém includes do próprio projeto
target_include_directories(webdriverclientcpp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(webdriverclientcpp
    PUBLIC ${WEBDRIVER_REQUIRED_LIBS}
    PRIVATE ${WEBDRIVER_OPTIONAL_LIBS}
)

# Apply compiler flags and sanitizers
configure_compiler_flags(webdriverclientcpp)
configure_sanitizers(webdriverclientcpp)
configure_feature_macros(webdriverclientcpp)

# Create alias for subproject usage
add_library(WebDriverClient::webdriverclientcpp ALIAS webdriverclientcpp)

# =============================================================================
# Example Executable (optional)
# =============================================================================
if(WEBDRIVER_BUILD_EXAMPLES)
    function(add_webdriver_example target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE webdriverclientcpp)
        configure_compiler_flags(${target})
        configure_sanitizers(${target})
    endfunction()

    add_executable(webdriverclientcpp_example "src/main.cpp")
    target_link_libraries(webdriverclientcpp_example
        PRIVATE webdriverclientcpp
    )
    configure_compiler_flags(webdriverclientcpp_example)
    configure_sanitizers(webdriverclientcpp_example)

    # BiDi integration demo (requires Boost)
    if(WEBDRIVER_USE_BOOST)
        add_webdriver_example(example_automation_session_minimal
                              "examples/flow/example_automation_session_minimal.cpp")
        add_webdriver_example(example_bidi_flow_minimal
                              "examples/flow/example_bidi_flow_minimal.cpp")
        add_webdriver_example(example_bidi_flow_production
                              "examples/flow/example_bidi_flow_production.cpp")
        add_webdriver_example(example_bidi_flow_script_exception
                              "examples/flow/example_bidi_flow_script_exception.cpp")
        add_webdriver_example(example_log_monitoring
                              "examples/flow/example_log_monitoring.cpp")

        add_webdriver_example(example_multithreaded_basic
                              "examples/threading/example_multithreaded_basic.cpp")
        add_webdriver_example(example_multithreaded_cpu_pool
                              "examples/threading/example_multithreaded_cpu_pool.cpp")

        add_webdriver_example(example_scraping_patterns
                              "examples/scraping/real_world_scraping_patterns.cpp")
        add_webdriver_example(example_spa_and_lazy_loading
                              "examples/scraping/spa_and_lazy_loading.cpp")
    endif()
endif()

# =============================================================================
# Testing Configuration (optional)
# =============================================================================
if(WEBDRIVER_BUILD_TESTS)
    include(CTest)
    enable_testing()
    find_package(GTest QUIET)

    if(GTest_FOUND)
        add_subdirectory(tests)
    else()
        message(STATUS "GTest not found - tests disabled")
    endif()
endif()

# =============================================================================
# Installation and Export (for library consumers)
# =============================================================================
if(WEBDRIVER_STANDALONE_BUILD)
    include(GNUInstallDirs)

    # Install library
    install(TARGETS webdriverclientcpp
        EXPORT WebDriverClientTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # Install headers
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
    )

    # Install CMake config files
    install(EXPORT WebDriverClientTargets
        FILE WebDriverClientTargets.cmake
        NAMESPACE WebDriverClient::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WebDriverClient
    )

    # Create config file
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebDriverClientConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/WebDriverClientConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WebDriverClient
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/WebDriverClientConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/WebDriverClientConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/WebDriverClientConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WebDriverClient
    )
endif()
