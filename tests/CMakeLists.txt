
cmake_minimum_required(VERSION 3.5)
project(conduit-tests)

add_subdirectory(googletest)

include(GoogleTest)
file(GLOB src *.cpp)
add_executable(conduit-tests ${src})
target_include_directories(conduit-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(conduit-tests PUBLIC gtest gmock gtest_main)
gtest_add_tests(TARGET conduit-tests)
