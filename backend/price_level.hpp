#pragma once

#include "order.hpp"
#include <memory>

namespace orderbook {

// Price level maintains a doubly-linked list of orders at the same price
// Optimized for O(1) operations
// Cache-line aligned for HFT performance (64 bytes = typical L1 cache line)
class alignas(64) PriceLevel {
public:
    Price price;
    Quantity total_quantity;
    uint32_t order_count;
    
    // Head and tail of the order list (FIFO for price-time priority)
    Order* head;
    Order* tail;
    
    // For linking price levels in a tree/map
    PriceLevel* next;
    PriceLevel* prev;
    
    explicit PriceLevel(Price p)
        : price(p), total_quantity(0), order_count(0),
          head(nullptr), tail(nullptr), next(nullptr), prev(nullptr) {}
    
    // Add order to the end of the queue (maintaining time priority)
    [[gnu::always_inline]]
    inline void add_order(Order* order) noexcept {
        if (tail) {
            tail->next = order;
            order->prev = tail;
            order->next = nullptr;
            tail = order;
        } else {
            head = tail = order;
            order->prev = order->next = nullptr;
        }
        total_quantity += order->remaining_quantity();
        ++order_count;
    }
    
    // Remove order from the queue
    [[gnu::always_inline]]
    inline void remove_order(Order* order) noexcept {
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head = order->next;
        }
        
        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail = order->prev;
        }
        
        total_quantity -= order->remaining_quantity();
        --order_count;
        
        order->prev = order->next = nullptr;
    }
    
    // Update quantity when order is partially filled
    [[gnu::always_inline]]
    inline void update_quantity(Order* order, Quantity old_remaining) noexcept {
        total_quantity = total_quantity - old_remaining + order->remaining_quantity();
    }
    
    [[gnu::always_inline]]
    inline bool is_empty() const noexcept {
        return order_count == 0;
    }
    
    // Get best order (head of queue for price-time priority)
    [[gnu::always_inline]]
    inline Order* get_best_order() const noexcept {
        return head;
    }
};

} // namespace orderbook
