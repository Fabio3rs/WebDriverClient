add_executable(webdriverclientcpp_tests test.cpp)
target_link_libraries(webdriverclientcpp_tests webdriverclientcpp_lib GTest::GTest GTest::Main ${CURL_LIBRARIES} ${Poco_LIBRARIES})
gtest_discover_tests(webdriverclientcpp_tests)
