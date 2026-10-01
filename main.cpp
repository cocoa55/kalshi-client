#include <csignal>
#include <cstdio>
#include <format>
#include <print>
#include <thread>

#include "http_client.hpp"
#include "kalshi_auth.hpp"
#include "kalshi_messages.hpp"
#include "net_constants.hpp"
#include "trading_bot.hpp"

namespace {

constexpr std::string_view kDefaultTicker = "KXPRESNOMD-28-LC";

void handle_signal(int) { TradingBot::request_stop(); }

void install_signal_handlers() {
    // Installed without SA_RESTART so a blocking SSL_read returns EINTR and the loop can exit promptly.
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    // OpenSSL writes with write(), not send(MSG_NOSIGNAL): writing to a connection the peer has reset would
    // raise SIGPIPE and kill the process. Ignore it and handle the write error instead.
    std::signal(SIGPIPE, SIG_IGN);
}

// One-off admin command. Collateral is per exchange shard (e.g. baseball/tennis/basketball live on shard 3),
// so funds must be moved from the default shard 0 before trading those markets.
int fund_shard(const kalshi_auth::Signer &signer, const int shard, const int64_t dollars) {
    HttpClient http{std::string{kKalshiHost}, &signer};
    auto print_balance = [&] {
        auto balance = http.request_json("GET", kBalancePath);
        if (balance)
            std::println("Balance: {}", to_json_string(*balance));
        else
            std::println(stderr, "Balance request failed: {}", balance.error());
    };

    print_balance();
    // amount is in centicents: $1 = 10,000
    const std::string body = std::format(
            R"({{"source":"event_contract","destination":"event_contract","amount":{},"source_exchange_shard":0,"destination_exchange_shard":{}}})",
            dollars * 10'000, shard);
    auto response = http.request_json("POST", kShardTransferPath, body);
    if (!response) {
        std::println(stderr, "Transfer request failed: {}", response.error());
        return 1;
    }
    std::println("Transfer response: {}", to_json_string(*response));
    // Transfers are processed asynchronously, so give it a moment before re-reading.
    std::this_thread::sleep_for(std::chrono::seconds{2});
    print_balance();
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    install_signal_handlers();

    auto credentials = kalshi_auth::load_credentials_from_env();
    if (!credentials) {
        std::println(stderr, "Failed to load credentials: {}", credentials.error());
        return 1;
    }
    auto signer = kalshi_auth::Signer::from_credentials(*credentials);
    if (!signer) {
        std::println(stderr, "Failed to load private key: {}", signer.error());
        return 1;
    }

    if (argc == 4 && std::string_view{argv[1]} == "--fund-shard")
        return fund_shard(*signer, std::stoi(argv[2]), std::stoll(argv[3]));

    TradingBot bot{argc > 1 ? argv[1] : std::string{kDefaultTicker}, *signer, StrategyConfig{}};
    bot.run();

    std::println("Shutting down. Final position in {}: {}", bot.ticker(), bot.position());
    bot.print_latency_summary();
    return 0;
}
