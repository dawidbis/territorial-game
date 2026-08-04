find_package(GTest CONFIG REQUIRED)

add_executable(gameserver_tests
    attack_tests.cpp
    convert_tests.cpp
    economy_tests.cpp
    enclosure_tests.cpp
    keyframe_tests.cpp
    manifest_tests.cpp
    match_clock_tests.cpp
    match_lifetime_tests.cpp
    options_tests.cpp
    png_tests.cpp
    proto_tests.cpp
    rng_tests.cpp
    roster_tests.cpp
    session_tests.cpp
    simulation_tests.cpp
    synthetic_tests.cpp
    ticket_tests.cpp
    tmap_tests.cpp
    world_tests.cpp
)

# zlib wprost, bo testy czytnika PNG **budują pliki PNG**: uszkodzonej sumy kontrolnej ani
# wiersza zapisanego filtrem Sub nie da się poprosić od edytora graficznego.
target_link_libraries(gameserver_tests
    PRIVATE gameserver_core tmapgen_core ZLIB::ZLIB GTest::gtest GTest::gtest_main)

target_compile_options(gameserver_tests
    PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM} ${GAMESERVER_SANITIZER_FLAGS})

include(GoogleTest)

gtest_discover_tests(gameserver_tests)
