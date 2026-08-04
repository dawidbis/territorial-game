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

# Modułów C++20 tu nie ma i nie planujemy ich, a skanowanie w ich poszukiwaniu kosztuje
# przy każdej budowie i dokłada do `compile_commands.json` argument `@...modmap`, którego
# clang-tidy nie rozumie — czyli psuje jedyne narzędzie, dla którego ten plik powstaje.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Zależności przychodzą z vcpkg w trybie manifestowym (vcpkg.json obok tego pliku).
find_package(protobuf CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS asio beast json)

# Tylko libcrypto: TLS terminuje proxy (D9), więc proces nie ma żadnego powodu, żeby
# umieć rozmawiać po SSL-u. Z OpenSSL-a używamy weryfikacji podpisu biletu i SHA-256
# pliku terenu (D13).
find_package(OpenSSL REQUIRED)

# Wyłącznie dla `tmapgen`: dane obrazu w PNG to strumień deflate i nic poza zlib go nie
# rozpakuje. Wpisany wprost, mimo że dochodzi też tranzytywnie z protobufem — zależność,
# której się używa, ma być widoczna w manifeście.
find_package(ZLIB REQUIRED)

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

# ─────────────────────────── Sanitizery ───────────────────────────

# Lista sanitizerów oddzielona średnikami, np. „address;undefined". Puste (domyślne) znaczy
# zwykłą budowę — presety `linux-gcc-sanitizers` i `windows-ninja-asan` podstawiają tu wartość.
#
# Sanitizery znajdują dokładnie tę klasę błędów, której **nie widzi żaden test jednostkowy
# tego projektu**: dostęp do zwolnionej ramki korutyny, wiszącą referencję w `MatchServices`,
# indeks kafelka poza tablicą i przepełnienie ze znakiem. Cały ten kod stoi na cudzych
# czasach życia opisanych w komentarzach — a komentarz nie jest egzekwowalny.
set(GAMESERVER_SANITIZERS "" CACHE STRING "Sanitizery: address, undefined (średnik jako separator)")

set(GAMESERVER_SANITIZER_FLAGS "")

if(GAMESERVER_SANITIZERS)
    string(REPLACE ";" "," GAMESERVER_SANITIZER_LIST "${GAMESERVER_SANITIZERS}")

    if(MSVC)
        # MSVC ma wyłącznie ASan i nie przyjmuje listy — UBSan po tej stronie nie istnieje.
        if(NOT GAMESERVER_SANITIZER_LIST STREQUAL "address")
            message(FATAL_ERROR
                "MSVC obsługuje tylko sanitizer 'address', a dostał '${GAMESERVER_SANITIZER_LIST}'. "
                "UBSan chodzi na Linuksie — użyj presetu linux-gcc-sanitizers."
            )
        endif()

        # /INCREMENTAL:NO, bo linker przyrostowy nie współpracuje z ASan i mówi o tym dopiero
        # przy konsolidacji.
        #
        # /wd4714 wyłącza „funkcja oznaczona __forceinline nie została umieszczona
        # śródwierszowo" — pod ASan MSVC przestaje wstawiać setery z kodu generowanego przez
        # protobuf. To ostrzeżenie o cudzym kodzie, którego nie mamy jak poprawić, a przy /WX
        # zatrzymuje budowę. `/external:W0` go nie łapie, bo `__forceinline` jest instancjonowany
        # w naszej jednostce kompilacji.
        set(GAMESERVER_SANITIZER_FLAGS /fsanitize=address /wd4714)

        add_link_options(/INCREMENTAL:NO)
    else()
        # `-fno-sanitize-recover` zamienia znalezisko UBSan w niezerowy kod wyjścia. Bez tego
        # test przechodzi mimo wypisanego naruszenia, a CI świeci na zielono nad błędem.
        # `-fno-omit-frame-pointer` odzyskuje czytelny ślad stosu w raporcie.
        set(GAMESERVER_SANITIZER_FLAGS
            -fsanitize=${GAMESERVER_SANITIZER_LIST}
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer)

        add_link_options(-fsanitize=${GAMESERVER_SANITIZER_LIST})
    endif()

    message(STATUS "Sanitizery: ${GAMESERVER_SANITIZER_LIST}")
endif()

# Kod generowany z protobuf dostaje **tylko ASan**, nigdy UBSan.
#
# ASan musi go objąć, bo inaczej MSVC widzi adnotacje kontenerów w jednym pliku obiektowym,
# a w drugim nie, i konsolidacja pada na LNK2038. UBSan nie ma tam czego pilnować: generator
# protobufa rzutuje wartości z drutu na typy wyliczeniowe i robi arytmetykę na wskaźnikach
# w sposób, którego nie mamy jak zmienić, a czerwone CI z powodu cudzego kodu uczy ludzi
# wyłączać zadanie, a nie czytać raport.
set(GAMESERVER_GENERATED_SANITIZER_FLAGS "")

if(GAMESERVER_SANITIZERS)
    if(MSVC)
        set(GAMESERVER_GENERATED_SANITIZER_FLAGS ${GAMESERVER_SANITIZER_FLAGS})
    elseif("address" IN_LIST GAMESERVER_SANITIZERS)
        set(GAMESERVER_GENERATED_SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
    endif()
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

# Sam ASan, bez UBSan — powód przy definicji tej zmiennej.
target_compile_options(game_proto PRIVATE ${GAMESERVER_GENERATED_SANITIZER_FLAGS})

# SYSTEM, żeby ostrzeżenia z wygenerowanego kodu nie wywracały budowy przez /WX.
target_include_directories(game_proto SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${GAMESERVER_PROTO_GENERATED_DIR}>"
)

protobuf_generate(
    TARGET game_proto
    IMPORT_DIRS "${GAMESERVER_PROTO_DIR}"
    PROTOC_OUT_DIR "${GAMESERVER_PROTO_GENERATED_DIR}"
)

# ─────────────────────────── Format pliku mapy ───────────────────────────

# Osobno od reszty serwera, bo ten sam nagłówek czyta proces meczu i zapisuje `tmapgen`.
# Format z dwiema niezależnymi implementacjami rozjeżdża się przy pierwszej zmianie i wychodzi
# to dopiero na produkcji. Bez Boosta i bez protobufa — konwerter nie ma powodu ich ciągnąć.
add_library(tmap_format STATIC src/map/tmap.cpp)

target_include_directories(tmap_format PUBLIC src)

target_compile_options(tmap_format PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM} ${GAMESERVER_SANITIZER_FLAGS})

# ─────────────────────────── Serwer ───────────────────────────

# Logika w bibliotece, `main` wyłącznie jako cienka warstwa — inaczej testy nie miałyby
# się o co zaczepić.
add_library(gameserver_core STATIC
    src/app/log.cpp
    src/app/match_runner.cpp
    src/app/options.cpp
    src/app/startup.cpp
    src/map/map_file.cpp
    src/meta/manifest.cpp
    src/meta/ticket.cpp
    src/net/commands.cpp
    src/net/listener.cpp
    src/net/session.cpp
    src/net/session_registry.cpp
    src/sim/attack.cpp
    src/sim/economy.cpp
    src/sim/rng.cpp
    src/sim/roster.cpp
    src/sim/simulation.cpp
    src/sim/world.cpp
    src/state/keyframe.cpp
    src/state/match_intro.cpp
    src/state/publisher.cpp
    src/state/snapshot.cpp
    src/tick/match_clock.cpp
    src/tick/match_lifetime.cpp
)

target_include_directories(gameserver_core PUBLIC src)

target_link_libraries(gameserver_core
    PUBLIC game_proto tmap_format Boost::asio Boost::beast
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

target_compile_options(gameserver_core PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM} ${GAMESERVER_SANITIZER_FLAGS})

add_executable(gameserver src/main.cpp)

target_link_libraries(gameserver PRIVATE gameserver_core)

target_compile_options(gameserver PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM} ${GAMESERVER_SANITIZER_FLAGS})

# ─────────────────────────── tmapgen ───────────────────────────

# Konwerter pary plików źródłowych (PNG + JSON) na `.tmap`. W tym samym projekcie co serwer,
# żeby dzielić z nim nagłówek formatu — i tu też logika siedzi w bibliotece, bo walidacja map
# jest dokładnie tym, co warto mieć pokryte testami.
add_library(tmapgen_core STATIC
    tools/tmapgen/convert.cpp
    tools/tmapgen/png.cpp
    tools/tmapgen/synthetic.cpp
)

target_include_directories(tmapgen_core PUBLIC tools/tmapgen)

target_link_libraries(tmapgen_core PUBLIC tmap_format PRIVATE Boost::json ZLIB::ZLIB)

target_compile_options(tmapgen_core PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM} ${GAMESERVER_SANITIZER_FLAGS})

add_executable(tmapgen tools/tmapgen/main.cpp)

target_link_libraries(tmapgen PRIVATE tmapgen_core)

target_compile_options(tmapgen PRIVATE ${GAMESERVER_WARNINGS} ${GAMESERVER_DETERMINISM} ${GAMESERVER_SANITIZER_FLAGS})

# ─────────────────────────── Testy ───────────────────────────

option(GAMESERVER_BUILD_TESTS "Buduj testy jednostkowe" ON)

if(GAMESERVER_BUILD_TESTS)
    enable_testing()

    add_subdirectory(tests)
endif()
