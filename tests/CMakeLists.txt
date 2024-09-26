add_executable(clichromewebdriver_tests test.cpp)
target_link_libraries(clichromewebdriver_tests clichromewebdriver_lib GTest::GTest GTest::Main ${CURL_LIBRARIES} ${Poco_LIBRARIES})
gtest_discover_tests(clichromewebdriver_tests)
