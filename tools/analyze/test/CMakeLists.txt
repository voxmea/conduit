
cmake_minimum_required(VERSION 3.5)

add_definitions(-D __STDC_LIMIT_MACROS -D __STDC_CONSTANT_MACROS -D __STDC_FORMAT_MACROS -D SOURCE_STRING_INTERNING -D CONDUIT_NO_LUA)
include_directories(../../../include)
add_executable(analyze-test analyze-test.cpp)
