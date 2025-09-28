# Tests executable
add_executable(webdriverclientcpp_tests test.cpp)

# Link with the main library (which handles all dependencies internally)
target_link_libraries(webdriverclientcpp_tests
    PRIVATE
        webdriverclientcpp
        GTest::GTest
        GTest::Main
)

# Apply the same compiler flags as the library
configure_compiler_flags(webdriverclientcpp_tests)
configure_sanitizers(webdriverclientcpp_tests)

# Register tests
gtest_discover_tests(webdriverclientcpp_tests)
