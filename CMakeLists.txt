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
)
target_include_directories(kalshi-client PRIVATE ${CMAKE_SOURCE_DIR}/include)
set(CMAKE_CXX_STANDARD_REQUIRED ON)