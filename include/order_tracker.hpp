#pragma once
#include <string>
#include <unordered_map>

#include "kalshi_messages.hpp"

enum class OrderStatus {
    Pending,
    Open,
    Filled,
    Cancelled
};

using OrderId = std::string;
struct Order {
    OrderId order_id;
    OrderRequest order_request;
    OrderStatus order_status{OrderStatus::Pending};
    std::string fill_count{"0.00"};
    std::string remaining_count;
};


class OrderTracker {
    std::unordered_map<OrderId, Order> _orders;
public:
    const Order* get_order(const OrderId& id) const {
        auto it = _orders.find(id);
        return it != _orders.end() ? &it->second : nullptr;
    }
    const std::unordered_map<OrderId, Order>& all_orders() const {return _orders;}

    void add_order(const Order& order) {_orders.emplace(order.order_id, order);}
    void update_status(const OrderId& id, const OrderStatus status)  {
        auto it = _orders.find(id);
        if (it != _orders.end()) {
            it->second.order_status = status;
        }
    }
};