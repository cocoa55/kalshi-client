cmake_minimum_required(VERSION 4.4)
project(kalshi-client)

set(CMAKE_CXX_STANDARD 23)

add_executable(kalshi-client main.cpp
        src/network/tcp_socket.cpp
        src/network/web_socket.cpp
        src/protocol/http_client.cpp
        include/tcp_socket.hpp
        include/tcp_socket.hpp
        include/web_socket.hpp
        include/tls_socket.hpp
        src/network/tls_socket.cpp
        include/kalshi_auth.hpp
        src/protocol/kalshi_auth.cpp
        include/web_socket_frame.hpp
        src/protocol/frame_parser.cpp
        include/frame_parser.hpp
        src/protocol/frame_builder.cpp
        include/frame_builder.hpp
        include/openssl_util.hpp
        include/net_constants.hpp
        include/parse_error.hpp
        include/kalshi_messages.hpp
        src/encoding/json_lexer.cpp
        include/json_lexer.hpp
        src/encoding/json_parser.cpp
        include/json_parser.hpp
        include/json_parser.hpp
        src/protocol/kalshi_messages.cpp
)
target_include_directories(kalshi-client PRIVATE ${CMAKE_SOURCE_DIR}/include)
find_package(OpenSSL REQUIRED)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
target_link_libraries(kalshi-client PRIVATE OpenSSL::SSL OpenSSL::Crypto)