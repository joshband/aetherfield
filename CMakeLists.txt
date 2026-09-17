cmake_minimum_required(VERSION 3.25)

project(aetherfield LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aetherfield_dsp STATIC
    src/dsp/GainProcessor.cpp
    src/dsp/DelayLine.cpp
    src/dsp/FeedbackDelayNetwork.cpp
)
target_include_directories(aetherfield_dsp PUBLIC src)

add_executable(aetherfield_dsp_tests tests/GainProcessorTests.cpp)
target_link_libraries(aetherfield_dsp_tests PRIVATE aetherfield_dsp)

add_executable(aetherfield_dsp_delay_tests tests/DelayLineTests.cpp)
target_link_libraries(aetherfield_dsp_delay_tests PRIVATE aetherfield_dsp)

add_executable(aetherfield_dsp_fdn_tests tests/FdnTests.cpp)
target_link_libraries(aetherfield_dsp_fdn_tests PRIVATE aetherfield_dsp)

add_executable(aetherfield_render tools/render/main.cpp)
target_link_libraries(aetherfield_render PRIVATE aetherfield_dsp)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target aetherfield_dsp aetherfield_dsp_tests aetherfield_dsp_delay_tests aetherfield_dsp_fdn_tests aetherfield_render)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endforeach()
endif()

enable_testing()
add_test(NAME aetherfield_dsp_tests COMMAND aetherfield_dsp_tests)
add_test(NAME aetherfield_dsp_delay_tests COMMAND aetherfield_dsp_delay_tests)
add_test(NAME aetherfield_dsp_fdn_tests COMMAND aetherfield_dsp_fdn_tests)
