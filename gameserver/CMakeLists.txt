cmake_minimum_required(VERSION 3.28)

# IDE potrafią skonfigurować projekt własnym profilem, z pominięciem presetów — CLion
# robi tak domyślnie. Wtedy nikt nie podstawia łańcucha narzędzi vcpkg i konfiguracja
# kończy się na „nie znaleziono protobuf". Podstawiamy go sami, zanim ruszy project().
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Łańcuch narzędzi vcpkg")
endif()

project(territorial_gameserver VERSION 0.1.0 LANGUAGES CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "Brak łańcucha narzędzi vcpkg. Ustaw zmienną środowiskową VCPKG_ROOT na katalog "
        "instalacji vcpkg albo skonfiguruj projekt jednym z presetów z CMakePresets.json."
    )
endif()

# Preset nie wymusza kompilatora, bo generator Ninja bierze go ze środowiska, a IDE
# podaje własną ścieżkę — wymuszony „cl" oznaczałby, że budowa działa wyłącznie tam,
# gdzie ktoś wcześniej wczytał vcvars. Zamiast tego sprawdzamy wynik i mówimy wprost,
# co poszło nie tak, bo cicha budowa innym kompilatorem byłaby gorsza niż błąd.
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR
        "Na Windowsie budujemy MSVC, a CMake wybrał inny kompilator (${CMAKE_CXX_COMPILER_ID}). "
        "Zwykle znaczy to, że konfiguracja poszła bez środowiska Visual Studio: w CLion ustaw "
        "w profilu CMake łańcuch narzędzi Visual Studio, w terminalu użyj presetu windows-msvc."
    )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dla podpowiedzi w edytorach i dla clang-tidy. Generator Visual Studio tego nie
# wystawia i nic na tym nie traci — czyta projekt wprost.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Zależności przychodzą z vcpkg w trybie manifestowym (vcpkg.json obok tego pliku).
find_package(protobuf CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS asio beast json)

# Tylko libcrypto: TLS terminuje proxy (D9), więc proces nie ma żadnego powodu, żeby
# umieć rozmawiać po SSL-u. Z OpenSSL-a używamy wyłącznie weryfikacji podpisu biletu.
find_package(OpenSSL REQUIRED)

# Ostrzeżenia są błędami — tak samo jak analizatory po stronie .NET. Nagłówki obcych
# bibliotek są z tego wyłączone, bo nie mamy jak ich poprawić.
if(MSVC)
    # /Zc:__cplusplus — MSVC domyślnie raportuje __cplusplus jako 199711, przez co
    # biblioteki wykrywające możliwości języka widzą kompilator sprzed dwóch dekad.
    # /bigobj — szablony Beasta razem z korutynami przekraczają limit sekcji w pliku
    # obiektowym COFF. To limit formatu z lat dziewięćdziesiątych, nie objaw problemu w kodzie.
    set(GAMESERVER_WARNINGS
        /W4 /WX /permissive- /Zc:__cplusplus /bigobj /external:anglebrackets /external:W0)

    # Determinizm (§8): bez kontrakcji i przestawiania działań zmiennoprzecinkowych.
    # W samej symulacji `float` nie ma prawa wystąpić, ale flaga kosztuje nic i zamyka
    # drogę, którą inaczej ktoś kiedyś pójdzie nieświadomie.
    set(GAMESERVER_DETERMINISM /fp:strict)
else()
    set(GAMESERVER_WARNINGS -Wall -Wextra -Wpedantic -Werror)
    set(GAMESERVER_DETERMINISM -ffp-contract=off -fno-fast-math)
endif()

# ─────────────────────────── Kontrakt sieciowy ───────────────────────────

set(GAMESERVER_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto")
set(GAMESERVER_PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Schemat leży poza tym projektem, bo należy też do klienta (D2). Kod generowany
# powstaje w katalogu budowania i nigdy nie trafia do repozytorium — wersjonowany
# rozjechałby się ze schematem w sposób niewidoczny w diffie.
add_library(game_proto OBJECT "${GAMESERVER_PROTO_DIR}/game.proto")

set_target_properties(game_proto PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(game_proto PUBLIC protobuf::libprotobuf)

# SYSTEM, żeby ostrzeżenia z wygenerowanego kodu nie wywracały budowy przez /WX.
target_include_directories(game_proto SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${GAMESERVER_PROTO_GENERATED_DIR}>"
)

protobuf_generate(
    TARGET game_proto
    IMPORT_DIRS "${GAMESERVER_PROTO_DIR}"
    PROTOC_OUT_DIR "${GAMESERVER_PROTO_GENERATED_DIR}"
)

# ─────────────────────────── Serwer ───────────────────────────

# Logika w bibliotece, `main` wyłącznie jako cienka warstwa — inaczej testy nie miałyby
# się o co zaczepić.
add_library(gameserver_core STATIC
    src/app/options.cpp
    src/app/log.cpp
    src/meta/ticket.cpp
    src/net/listener.cpp
    src/net/session.cpp
    src/tick/match_clock.cpp
)

target_include_directories(gameserver_core PUBLIC src)

target_link_libraries(gameserver_core
    PUBLIC game_proto Boost::asio Boost::beast
    PRIVATE Boost::json OpenSSL::Crypto
)

if(WIN32)
    # Bez tego Asio zakłada Windows 7 i mówi o tym przy każdej kompilacji. Wersja
    # docelowa to Windows 10 — deweloperska, bo produkcja stoi na Linuksie.
    target_compile_definitions(gameserver_core PUBLIC _WIN32_WINNT=0x0A00)
endif()

if(MSVC)
    # Asio włącza swoje API korutynowe pod warunkiem `!defined(__clang__)` (config.hpp).
    # Kompilacja przechodzi, ale **clangd** — czyli podpowiedzi i analiza w IDE — definiuje
    # `__clang__` nawet w trybie zgodności z MSVC, więc nie widzi `awaitable` ani `co_spawn`
    # i zalewa plik błędami, których nie ma. Włączamy te możliwości wprost; przy wymaganym
    # C++23 nie ma kompilatora, który by ich nie miał.
    target_compile_definitions(gameserver_core PUBLIC
        BOOST_ASIO_HAS_CO_AWAIT
        BOOST_ASIO_HAS_STD_COROUTINE
    )
endif()

target_compile_options(gameserver_core PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM})

add_executable(gameserver src/main.cpp)

target_link_libraries(gameserver PRIVATE gameserver_core)

target_compile_options(gameserver PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM})

# ─────────────────────────── Testy ───────────────────────────

option(GAMESERVER_BUILD_TESTS "Buduj testy jednostkowe" ON)

if(GAMESERVER_BUILD_TESTS)
    enable_testing()

    add_subdirectory(tests)
endif()
