# Retry Policy Project

## Overview

The Retry Policy Project provides a customizable structure for managing execution attempts with a retry policy. This project is designed to facilitate the implementation of retry logic in applications, allowing developers to specify the maximum number of attempts and the delay between retries.

## Project Structure

```
retry-policy-project
├── include
│   └── retry_policy.hpp       # Header file defining the RetryPolicy structure
├── src
│   └── retry_policy.cpp       # Source file implementing the logic for RetryPolicy
├── CMakeLists.txt             # CMake configuration file for building the project
└── README.md                  # Documentation for the project
```

## Files Description

- **include/retry_policy.hpp**: This file contains the definition of the `RetryPolicy` structure, which manages the retry policy. The structure includes the members `max_attempts` and `base_delay`, along with a static method `exponential` that creates an instance of `RetryPolicy` with a specified number of attempts.

- **src/retry_policy.cpp**: This file implements the logic related to the `RetryPolicy` structure. It may include functions that utilize the retry policy defined in `retry_policy.hpp`.

- **CMakeLists.txt**: This file is the configuration script for CMake, defining how the project should be built, including compilation options and necessary dependencies.

## Building the Project

To build the project, follow these steps:

1. Ensure you have CMake installed on your system.
2. Open a terminal and navigate to the project directory.
3. Create a build directory:
   ```
   mkdir build
   cd build
   ```
4. Run CMake to configure the project:
   ```
   cmake ..
   ```
5. Build the project:
   ```
   cmake --build . -j$(nproc)
   ```

## Usage

After building the project, you can include the `retry_policy.hpp` header in your application to utilize the `RetryPolicy` structure. You can create instances of `RetryPolicy` and use them to manage retry attempts in your code.

## License

This project is licensed under the MIT License. See the LICENSE file for more details.