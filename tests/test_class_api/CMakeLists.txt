cmake_minimum_required(VERSION 3.10)
project(test_class_api LANGUAGES CXX)

add_executable(test_class_api main.cpp)
target_link_libraries(test_class_api zen_static)
if(NOT WIN32)
    target_link_options(test_class_api PRIVATE -rdynamic)
endif()
