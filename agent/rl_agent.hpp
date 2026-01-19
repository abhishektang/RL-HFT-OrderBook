#pragma once

#include "../backend/orderbook.hpp"
#include <vector>
#include <memory>
#include <random>

namespace orderbook {

// RL Agent Interface for training trading algorithms
// This provides a simplified interface for RL algorithms to interact with the order book
class RLAgent {
public:
    // Action space for RL agent
    enum class Action {
        HOLD = 0,           // Do nothing
        BUY_MARKET = 1,     // Submit market buy order
        SELL_MARKET = 2,    // Submit market sell order
        BUY_LIMIT_AT_BID = 3,     // Submit limit buy at best bid
        SELL_LIMIT_AT_ASK = 4,    // Submit limit sell at best ask
        BUY_LIMIT_AGGRESSIVE = 5,  // Submit limit buy inside spread
        SELL_LIMIT_AGGRESSIVE = 6, // Submit limit sell inside spread
        CANCEL_ALL = 7      // Cancel all pending orders
    };
    
    struct Position {
        int64_t quantity;      // Positive = long, negative = short
        double avg_price;      // Average entry price
        double unrealized_pnl; // Unrealized profit/loss
        double realized_pnl;   // Realized profit/loss
        
        Position() : quantity(0), avg_price(0.0), unrealized_pnl(0.0), realized_pnl(0.0) {}
    };
    
    struct Observation {
        MarketState market_state;
        Position position;
        std::vector<OrderId> active_orders;
        double portfolio_value;
        double cash;
    };
    
    struct Reward {
        double pnl_change;        // Change in PnL
        double inventory_penalty; // Penalty for large inventory
        double spread_capture;    // Reward for providing liquidity
        double total;             // Total reward
        
        Reward() : pnl_change(0.0), inventory_penalty(0.0), 
                   spread_capture(0.0), total(0.0) {}
    };
    
private:
    OrderBook& orderbook_;
    Position position_;
    std::vector<OrderId> active_orders_;
    double cash_;
    double initial_cash_;
    
    // Reward function parameters
    double inventory_penalty_coef_;
    double spread_capture_reward_;
    
    // Statistics
    size_t total_trades_;
    double total_volume_;
    
    // Performance metrics
    double total_execution_time_ns_;
    size_t action_count_;
    
    void update_position(const Trade& trade);
    Reward calculate_reward(double previous_pnl);
    
public:
    explicit RLAgent(OrderBook& book, double initial_cash = 1000000.0);
    
    // RL interface
    Observation get_observation() const;
    Reward execute_action(Action action, Quantity quantity = 100);
    void reset();
    
    // Configuration
    void set_inventory_penalty(double coef) { inventory_penalty_coef_ = coef; }
    void set_spread_capture_reward(double reward) { spread_capture_reward_ = reward; }
    
    // Getters
    const Position& get_position() const { return position_; }
    double get_portfolio_value() const;
    size_t get_total_trades() const { return total_trades_; }
    double get_total_volume() const { return total_volume_; }
    double get_avg_latency_ns() const { 
        return action_count_ > 0 ? total_execution_time_ns_ / action_count_ : 0.0; 
    }
    double get_min_latency_ns() const { return 50.0; } // Approximate from order book
    double get_max_latency_ns() const { return 200.0; } // Approximate from order book
};

// Market simulator for training RL agents
// Generates synthetic order flow
class MarketSimulator {
private:
    OrderBook& orderbook_;
    std::mt19937 rng_;
    
    // Market parameters
    Price base_price_;
    double volatility_;
    double arrival_rate_;      // Orders per microsecond
    double spread_width_;
    
    // Order distribution
    std::normal_distribution<> price_dist_;
    std::exponential_distribution<> size_dist_;
    std::bernoulli_distribution side_dist_;
    
public:
    MarketSimulator(OrderBook& book, Price base_price, 
                   double volatility = 0.01, double arrival_rate = 100.0);
    
    // Generate random order flow
    void simulate_step(size_t num_orders = 10);
    void simulate_microseconds(uint64_t microseconds);
    
    // Configuration
    void set_volatility(double vol) { volatility_ = vol; }
    void set_arrival_rate(double rate) { arrival_rate_ = rate; }
    void set_spread_width(double width) { spread_width_ = width; }
};

// Performance metrics for backtesting
struct PerformanceMetrics {
    double sharpe_ratio;
    double sortino_ratio;
    double max_drawdown;
    double total_return;
    double win_rate;
    double profit_factor;
    size_t total_trades;
    double avg_trade_duration;
    
    void print() const;
};

// Backtesting engine
class Backtester {
private:
    OrderBook orderbook_;
    RLAgent agent_;
    std::vector<double> equity_curve_;
    std::vector<double> returns_;
    
public:
    Backtester(double initial_cash = 1000000.0);
    
    // Run backtest with custom strategy
    template<typename Strategy>
    void run(Strategy& strategy, size_t num_steps);
    
    // Calculate performance metrics
    PerformanceMetrics calculate_metrics() const;
    
    const std::vector<double>& get_equity_curve() const { return equity_curve_; }
};

} // namespace orderbook
