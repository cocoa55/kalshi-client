#pragma once

#include <string>
#include <expected>
#include <sys/types.h>

class TcpSocket {
private:
    int _fd = 0;

public:
    [[nodiscard]] std::expected<int, std::string> connect (const std::string& host, const std::string& port);

    [[nodiscard]] std::expected<ssize_t, std::string> send_data(const std::string& request) const;
    [[nodiscard]] std::expected<std::string, std::string> receive_data() const;

};