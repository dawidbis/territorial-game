find_package(GTest CONFIG REQUIRED)

add_executable(gameserver_tests
    options_tests.cpp
    match_clock_tests.cpp
    proto_tests.cpp
)

target_link_libraries(gameserver_tests PRIVATE gameserver_core GTest::gtest GTest::gtest_main)

target_compile_options(gameserver_tests PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM})

include(GoogleTest)

gtest_discover_tests(gameserver_tests)
