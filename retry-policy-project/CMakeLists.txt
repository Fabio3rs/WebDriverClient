cmake_minimum_required(VERSION 3.10)

# Define the project name
project(RetryPolicyProject)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/retry_policy.cpp
)

# Create the executable
add_executable(retry_policy ${SOURCES})