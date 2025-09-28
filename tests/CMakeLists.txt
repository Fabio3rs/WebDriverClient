# Tests executable
file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(webdriverclientcpp_tests ${TEST_SOURCES})

# Link with the main library (which handles all dependencies internally)
target_link_libraries(webdriverclientcpp_tests
    PRIVATE
        webdriverclientcpp
        GTest::GTest
        GTest::Main
)

target_compile_definitions(webdriverclientcpp_tests PRIVATE GTEST_LANG_CXX11=1)

# Apply the same compiler flags as the library
configure_compiler_flags(webdriverclientcpp_tests)
configure_sanitizers(webdriverclientcpp_tests)

# Register tests
gtest_discover_tests(webdriverclientcpp_tests)
