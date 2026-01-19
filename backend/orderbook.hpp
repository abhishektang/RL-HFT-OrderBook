#pragma once

#include "order.hpp"
#include "price_level.hpp"
#include "memory_pool.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <cmath>

namespace orderbook {

// RL State observation for the order book
struct MarketState {
    // Top of book
    Price best_bid;
    Price best_ask;
    Quantity bid_quantity;
    Quantity ask_quantity;
    
    // Spread
    Price spread;
    double mid_price;
    
    // Market depth (top N levels)
    std::vector<std::pair<Price, Quantity>> bid_levels;
    std::vector<std::pair<Price, Quantity>> ask_levels;
    
    // Order flow imbalance
    double order_flow_imbalance;
    
    // Recent trade information
    Price last_trade_price;
    Quantity last_trade_quantity;
    
    // Volume weighted average price
    double vwap;
    
    // Volatility indicator
    double price_volatility;
    
    // Timestamp
    Timestamp timestamp;
};

// Callbacks for RL agent and monitoring
using TradeCallback = std::function<void(const Trade&)>;
using OrderUpdateCallback = std::function<void(const Order&)>;
using MarketStateCallback = std::function<void(const MarketState&)>;

class OrderBook {
private:
    // Price level storage (sorted by price)
    std::map<Price, PriceLevel*, std::greater<Price>> bid_levels_;  // Descending for bids
    std::map<Price, PriceLevel*, std::less<Price>> ask_levels_;     // Ascending for asks
    
    // Order lookup
    std::unordered_map<OrderId, Order*> orders_;
    
    // Memory pools for efficient allocation
    MemoryPool<Order> order_pool_;
    MemoryPool<PriceLevel> price_level_pool_;
    
    // Callbacks
    std::vector<TradeCallback> trade_callbacks_;
    std::vector<OrderUpdateCallback> order_callbacks_;
    std::vector<MarketStateCallback> state_callbacks_;
    
    // Statistics for RL state
    std::vector<Price> recent_trade_prices_;
    std::vector<Quantity> recent_trade_quantities_;
    double cumulative_volume_;
    double cumulative_pq_;  // Price * Quantity for VWAP
    
    static constexpr size_t MAX_RECENT_TRADES = 100;
    static constexpr size_t DEPTH_LEVELS = 10;
    
    // Helper methods
    PriceLevel* get_or_create_level(Price price, Side side);
    void remove_level_if_empty(Price price, Side side);
    void match_order(Order* incoming_order);
    void execute_trade(Order* passive_order, Order* aggressive_order, Quantity quantity);
    void notify_trade(const Trade& trade);
    void notify_order_update(const Order& order);
    void update_market_statistics(const Trade& trade);
    
public:
    OrderBook();
    ~OrderBook();
    
    // Order management
    OrderId add_order(Price price, Quantity quantity, Side side, OrderType type = OrderType::LIMIT);
    bool cancel_order(OrderId order_id);
    bool modify_order(OrderId order_id, Price new_price, Quantity new_quantity);
    std::optional<Order> get_order(OrderId order_id) const;
    
    // Market data queries
    std::optional<Price> get_best_bid() const;
    std::optional<Price> get_best_ask() const;
    std::optional<Price> get_mid_price() const;
    std::optional<Price> get_spread() const;
    Quantity get_volume_at_price(Price price, Side side) const;
    
    // RL interface - get current market state
    MarketState get_market_state() const;
    
    // Register callbacks for RL agent
    void register_trade_callback(TradeCallback callback);
    void register_order_callback(OrderUpdateCallback callback);
    void register_state_callback(MarketStateCallback callback);
    
    // Statistics
    size_t get_order_count() const { return orders_.size(); }
    size_t get_bid_level_count() const { return bid_levels_.size(); }
    size_t get_ask_level_count() const { return ask_levels_.size(); }
    
    // For debugging/visualization
    void print_book(size_t depth = 10) const;
};

} // namespace orderbook
