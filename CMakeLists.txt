cmake_minimum_required(VERSION 3.22)
project(synthcore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# synthcore is headers only: the clock, the transport, QWERTY and the focus rule,
# copied from synthshell (see README.md and CUTS.md). A unit links it with
#   add_subdirectory(<path>/synthcore synthcore EXCLUDE_FROM_ALL)
#   target_link_libraries(<unit> PRIVATE synthcore)
add_library(synthcore INTERFACE)
target_include_directories(synthcore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# synthcore's own tests, only when this repo is built on its own.
if(PROJECT_IS_TOP_LEVEL)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    # The comparison test builds synthshell from its own folder, READ ONLY, into
    # this build tree. Nothing is written into synthshell. Its effect units are
    # left off; the test needs only the host.
    set(SYNTHCORE_SHELL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../synthshell" CACHE PATH "synthshell, the source of the copy")
    set(SYNTHSHELL_FX_UNITS_ENABLED OFF CACHE BOOL "" FORCE)
    add_subdirectory("${SYNTHCORE_SHELL_DIR}" synthshell EXCLUDE_FROM_ALL)

    enable_testing()

    # Does CoreHost keep time exactly as ShellHost does? The same playheads and
    # MIDI clock go through both, and every number either exposes is compared,
    # bit for bit, every block. --selftest plants two faults and must see both.
    juce_add_console_app(synthcore_same_as_shell PRODUCT_NAME "synthcore_same_as_shell")
    target_sources(synthcore_same_as_shell PRIVATE tests/same_as_shell.cpp)
    target_link_libraries(synthcore_same_as_shell PRIVATE synthcore synthshell
        juce::juce_audio_utils juce::juce_recommended_config_flags)
    target_compile_definitions(synthcore_same_as_shell PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    set_target_properties(synthcore_same_as_shell PROPERTIES WIN32_EXECUTABLE FALSE)

    add_test(NAME synthcore.same_as_shell COMMAND synthcore_same_as_shell)
    add_test(NAME synthcore.same_as_shell_fails_when_it_should COMMAND synthcore_same_as_shell --selftest)
    # Is the MIDI clock we send on time? (Tabula B-008) --selftest plants the old even spread.
    juce_add_console_app(synthcore_clock_out PRODUCT_NAME "synthcore_clock_out")
    target_sources(synthcore_clock_out PRIVATE tests/clock_out.cpp)
    target_link_libraries(synthcore_clock_out PRIVATE synthcore juce::juce_audio_utils juce::juce_recommended_config_flags)
    target_compile_definitions(synthcore_clock_out PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    set_target_properties(synthcore_clock_out PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME synthcore.clock_out_is_on_time COMMAND synthcore_clock_out)
    add_test(NAME synthcore.clock_out_fails_when_it_should COMMAND synthcore_clock_out --selftest)
    add_test(NAME synthcore.every_line_is_the_shells
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_copy.py)
    add_test(NAME synthcore.check_copy_fails_when_it_should
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_copy.py --selftest)
endif()
