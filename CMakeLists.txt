cmake_minimum_required(VERSION 4.4)
project(kalshi-client CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KALSHI_SANITIZE "Build everything with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(KALSHI_FUZZ "Build libFuzzer targets (requires Clang; implies sanitizers)" OFF)

if (KALSHI_FUZZ)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "KALSHI_FUZZ requires Clang (libFuzzer)")
    endif ()
    # Instrument all code for coverage-guided fuzzing; only fuzz targets link the libFuzzer main.
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
elseif (KALSHI_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif ()

find_package(OpenSSL REQUIRED)

# Everything except main() lives in one library so the bot, tests, benchmarks and fuzzers share it.
add_library(kalshi_core STATIC
        src/encoding/json_lexer.cpp
        src/encoding/json_parser.cpp
        src/network/tcp_socket.cpp
        src/network/tls_socket.cpp
        src/network/web_socket.cpp
        src/protocol/frame_builder.cpp
        src/protocol/frame_parser.cpp
        src/protocol/http_client.cpp
        src/protocol/http_response.cpp
        src/protocol/kalshi_decoder.cpp
        src/protocol/kalshi_auth.cpp
        src/protocol/kalshi_messages.cpp
        src/trading/market_state.cpp
        src/trading/strategy.cpp
        src/trading/trading_bot.cpp
)
target_include_directories(kalshi_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(kalshi_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
target_compile_options(kalshi_core PRIVATE -Wall -Wextra)

add_executable(kalshi-client main.cpp)
target_link_libraries(kalshi-client PRIVATE kalshi_core)

add_executable(benchmarks bench/benchmarks.cpp)
target_link_libraries(benchmarks PRIVATE kalshi_core)

enable_testing()
add_executable(unit_tests
        tests/test_main.cpp
        tests/test_fixed_point.cpp
        tests/test_frame_parser.cpp
        tests/test_http_response.cpp
        tests/test_json.cpp
        tests/test_kalshi_messages.cpp
        tests/test_market_state.cpp
        tests/test_strategy.cpp
)
target_link_libraries(unit_tests PRIVATE kalshi_core)
add_test(NAME unit_tests COMMAND unit_tests)

# Coverage-guided fuzzers (libFuzzer). Configure with -DKALSHI_FUZZ=ON using Clang, then e.g.:
#   mkdir -p fuzz/work/json && ./cmake-build-fuzz/fuzz_json -artifact_prefix=fuzz/work/ fuzz/work/json fuzz/corpus/json
if (KALSHI_FUZZ)
    foreach (target fuzz_frame_parser fuzz_json fuzz_http_response fuzz_decoder_differential)
        add_executable(${target} fuzz/${target}.cpp)
        target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        target_link_libraries(${target} PRIVATE kalshi_core)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    endforeach ()
endif ()
