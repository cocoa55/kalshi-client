#include "strategy.hpp"

#include <cmath>
#include <format>

std::optional<Signal> MeanReversionStrategy::on_book_update(const MarketState &book, const Quantity exposure,
                                                            const Clock::time_point now) {
    const auto bid = book.best_yes_bid();
    const auto ask = book.best_yes_ask();
    if (!bid || !ask || *ask <= *bid) // one-sided or crossed book: no reliable price
        return std::nullopt;

    const double mid = static_cast<double>(*bid + *ask) / 2.0;

    // Time-decayed EMA: weight depends on elapsed time, not message count,
    // so a burst of deltas doesn't drag fair value around.
    if (!_ema) {
        _ema = mid;
        _first_update = now;
    } else {
        const std::chrono::duration<double> dt = now - _last_update;
        const std::chrono::duration<double> tau = _config.ema_time_constant;
        const double alpha = 1.0 - std::exp(-dt / tau);
        *_ema += alpha * (mid - *_ema);
    }
    _last_update = now;

    if (now - _first_update < _config.warmup)
        return std::nullopt;
    if (*ask - *bid > _config.max_spread)
        return std::nullopt;
    if (_last_order && now - *_last_order < _config.cooldown)
        return std::nullopt;

    std::optional<Signal> signal;
    if (*ask <= *_ema - static_cast<double>(_config.entry_edge) && exposure + _config.order_size <= _config.max_position) {
        signal = Signal{.side = OrderSide::Bid, .price = *ask, .count = _config.order_size};
    } else if (*bid >= *_ema + static_cast<double>(_config.entry_edge) &&
               exposure - _config.order_size >= -_config.max_position) {
        signal = Signal{.side = OrderSide::Ask, .price = *bid, .count = _config.order_size};
    }

    if (signal)
        _last_order = now;
    return signal;
}

OrderRequest make_ioc_order(const std::string &ticker, const Signal &signal, const std::string &client_order_id) {
    return OrderRequest{.ticker = ticker,
                        .side = signal.side,
                        .count = std::format("{}.00", signal.count),
                        .price = price_to_dollars(signal.price),
                        .time_in_force = TimeInForce::ImmediateOrCancel,
                        .self_trade_prevention = SelfTradePrevention::TakerAtCross,
                        .client_order_id = client_order_id,
                        .post_only = false};
}
