cmake_minimum_required(VERSION 3.24)
project(swipe CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build ExecuTorch (XNNPACK backend — the .pte is XNNPACK-delegated) + extensions.
set(EXECUTORCH_BUILD_XNNPACK          ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_EXTENSION_TENSOR  ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_EXTENSION_MODULE  ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING                     OFF CACHE BOOL "" FORCE)

set(ET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/executorch)
add_subdirectory(${ET_DIR} ${CMAKE_BINARY_DIR}/executorch EXCLUDE_FROM_ALL)

set(ET_LIBS executorch portable_ops_lib
           extension_data_loader extension_tensor extension_module
           xnnpack_backend)

# 1. native inference spike (forwards Python's exact tensors, compares output)
add_executable(spike spike.cpp)
target_link_libraries(spike PRIVATE ${ET_LIBS})

# 2. reusable native decoder (will link into the Qt prototype too)
add_library(swipe_engine STATIC swipe_engine.cpp)
target_link_libraries(swipe_engine PUBLIC ${ET_LIBS})

# 3. corpus accuracy + latency test
add_executable(corpus_test corpus_test.cpp)
target_link_libraries(corpus_test PRIVATE swipe_engine)

# 4. personalization persistence test (bump -> save -> file)
add_executable(test_userfreq test_userfreq.cpp)
target_link_libraries(test_userfreq PRIVATE swipe_engine)

# Aspect-model unit test — header-only, no ExecuTorch, no model, no dictionary.
add_executable(aspect_model_test aspect_model_test.cpp)
