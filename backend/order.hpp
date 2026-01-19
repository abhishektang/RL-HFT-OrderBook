#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace orderbook {

using OrderId = uint64_t;
using Price = int64_t;  // Price in ticks (e.g., cents for USD)
using Quantity = uint64_t;
using Timestamp = std::chrono::nanoseconds;

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

enum class OrderType : uint8_t {
    LIMIT = 0,
    MARKET = 1,
    IOC = 2,      // Immediate or Cancel
    FOK = 3       // Fill or Kill
};

enum class OrderStatus : uint8_t {
    NEW = 0,
    PARTIALLY_FILLED = 1,
    FILLED = 2,
    CANCELLED = 3,
    REJECTED = 4
};

struct Order {
    OrderId id;
    Price price;
    Quantity quantity;
    Quantity filled_quantity;
    Side side;
    OrderType type;
    OrderStatus status;
    Timestamp timestamp;
    
    // For linked list in price level
    Order* next;
    Order* prev;
    
    Order() : id(0), price(0), quantity(0), filled_quantity(0),
              side(Side::BUY), type(OrderType::LIMIT), status(OrderStatus::NEW),
              timestamp(Timestamp::zero()), next(nullptr), prev(nullptr) {}
    
    Order(OrderId id_, Price price_, Quantity qty_, Side side_, OrderType type_)
        : id(id_), price(price_), quantity(qty_), filled_quantity(0),
          side(side_), type(type_), status(OrderStatus::NEW),
          timestamp(std::chrono::high_resolution_clock::now().time_since_epoch()),
          next(nullptr), prev(nullptr) {}
    
    inline Quantity remaining_quantity() const {
        return quantity - filled_quantity;
    }
    
    inline bool is_fully_filled() const {
        return filled_quantity >= quantity;
    }
};

struct Trade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;
    
    Trade(OrderId buy_id, OrderId sell_id, Price p, Quantity q)
        : buy_order_id(buy_id), sell_order_id(sell_id), price(p), quantity(q),
          timestamp(std::chrono::high_resolution_clock::now().time_since_epoch()) {}
};

} // namespace orderbook
