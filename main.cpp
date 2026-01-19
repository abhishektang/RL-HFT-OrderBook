#include "backend/orderbook.hpp"
#include "agent/rl_agent.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace orderbook;

// Simple market making strategy
class MarketMaker {
private:
    RLAgent& agent_;
    Quantity quote_size_;
    Price spread_ticks_;
    int64_t max_position_;
    
public:
    MarketMaker(RLAgent& agent, Quantity size = 1000, Price spread = 2, int64_t max_pos = 10000)
        : agent_(agent), quote_size_(size), spread_ticks_(spread), max_position_(max_pos) {}
    
    RLAgent::Action operator()(const RLAgent::Observation& obs) {
        const auto& pos = obs.position;
        const auto& market = obs.market_state;
        
        // Cancel all orders if position too large
        if (std::abs(pos.quantity) > max_position_) {
            return RLAgent::Action::CANCEL_ALL;
        }
        
        // If heavily long, only provide sell quotes
        if (pos.quantity > max_position_ / 2) {
            return RLAgent::Action::SELL_LIMIT_AT_ASK;
        }
        
        // If heavily short, only provide buy quotes
        if (pos.quantity < -max_position_ / 2) {
            return RLAgent::Action::BUY_LIMIT_AT_BID;
        }
        
        // Otherwise, provide quotes on both sides
        if (obs.active_orders.size() < 2 && market.best_bid > 0 && market.best_ask > 0) {
            // Alternate between buy and sell
            static bool buy_next = true;
            buy_next = !buy_next;
            return buy_next ? RLAgent::Action::BUY_LIMIT_AT_BID : 
                             RLAgent::Action::SELL_LIMIT_AT_ASK;
        }
        
        return RLAgent::Action::HOLD;
    }
};

int main() {
    std::cout << "=== High-Performance Order Book for Nanosecond Trading ===" << std::endl;
    std::cout << "Optimized for ultra-low latency with RL integration\n" << std::endl;
    
    // Create order book
    OrderBook book;
    
    // Register callbacks for monitoring
    book.register_trade_callback([](const Trade& trade) {
        std::cout << "TRADE: "
                  << "Price=" << trade.price / 100.0
                  << ", Qty=" << trade.quantity
                  << ", Buy #" << trade.buy_order_id
                  << ", Sell #" << trade.sell_order_id << std::endl;
    });
    
    std::cout << "\n=== Demo 1: Basic Order Book Operations ===" << std::endl;
    
    // Add some initial liquidity
    Price base_price = 10000; // $100.00
    book.add_order(base_price - 10, 500, Side::BUY);   // Bid at 99.90
    book.add_order(base_price - 5, 1000, Side::BUY);   // Bid at 99.95
    book.add_order(base_price + 5, 800, Side::SELL);   // Ask at 100.05
    book.add_order(base_price + 10, 600, Side::SELL);  // Ask at 100.10
    
    book.print_book(5);
    
    // Get market state for RL
    auto state = book.get_market_state();
    std::cout << "Market State:" << std::endl;
    std::cout << "  Best Bid: " << state.best_bid / 100.0 << std::endl;
    std::cout << "  Best Ask: " << state.best_ask / 100.0 << std::endl;
    std::cout << "  Spread: " << state.spread / 100.0 << std::endl;
    std::cout << "  Mid Price: " << state.mid_price / 100.0 << std::endl;
    std::cout << "  Order Flow Imbalance: " << state.order_flow_imbalance << std::endl;
    std::cout << "  VWAP: " << state.vwap << std::endl;
    
    // Execute a market order (should trigger match)
    std::cout << "\nExecuting market buy order for 600 shares..." << std::endl;
    book.add_order(base_price + 10, 600, Side::BUY, OrderType::MARKET);
    
    book.print_book(5);
    
    std::cout << "\n=== Demo 2: Market Simulation ===" << std::endl;
    
    // Create market simulator
    MarketSimulator sim(book, base_price, 0.005, 50.0);
    
    std::cout << "Simulating 1000 microseconds of order flow..." << std::endl;
    sim.simulate_microseconds(1000);
    
    book.print_book(10);
    
    std::cout << "\nOrder Book Statistics:" << std::endl;
    std::cout << "  Total Orders: " << book.get_order_count() << std::endl;
    std::cout << "  Bid Levels: " << book.get_bid_level_count() << std::endl;
    std::cout << "  Ask Levels: " << book.get_ask_level_count() << std::endl;
    
    std::cout << "\n=== Demo 3: RL Agent Trading ===" << std::endl;
    
    // Create RL agent
    RLAgent agent(book, 1000000.0);  // $1M initial capital
    
    // Create market making strategy
    MarketMaker strategy(agent);
    
    std::cout << "Running market making strategy for 100 steps..." << std::endl;
    
    for (size_t step = 0; step < 100; ++step) {
        // Simulate some market activity
        sim.simulate_step(5);
        
        // Get observation
        auto obs = agent.get_observation();
        
        // Execute strategy action
        auto action = strategy(obs);
        auto reward = agent.execute_action(action, 500);
        
        if (step % 20 == 0) {
            std::cout << "\nStep " << step << ":" << std::endl;
            std::cout << "  Position: " << obs.position.quantity << std::endl;
            std::cout << "  Realized PnL: $" << std::fixed << std::setprecision(2) 
                      << obs.position.realized_pnl << std::endl;
            std::cout << "  Unrealized PnL: $" << obs.position.unrealized_pnl << std::endl;
            std::cout << "  Portfolio Value: $" << obs.portfolio_value << std::endl;
            std::cout << "  Reward: " << reward.total << std::endl;
            std::cout << "  Active Orders: " << obs.active_orders.size() << std::endl;
        }
    }
    
    std::cout << "\n=== Final Statistics ===" << std::endl;
    
    auto final_obs = agent.get_observation();
    std::cout << "Agent Performance:" << std::endl;
    std::cout << "  Total Trades: " << agent.get_total_trades() << std::endl;
    std::cout << "  Total Volume: " << agent.get_total_volume() << std::endl;
    std::cout << "  Final Position: " << final_obs.position.quantity << std::endl;
    std::cout << "  Realized PnL: $" << std::fixed << std::setprecision(2)
              << final_obs.position.realized_pnl << std::endl;
    std::cout << "  Unrealized PnL: $" << final_obs.position.unrealized_pnl << std::endl;
    std::cout << "  Total PnL: $" 
              << (final_obs.position.realized_pnl + final_obs.position.unrealized_pnl) << std::endl;
    std::cout << "  Portfolio Value: $" << final_obs.portfolio_value << std::endl;
    std::cout << "  Return: " 
              << ((final_obs.portfolio_value - 1000000.0) / 1000000.0 * 100) << "%" << std::endl;
    
    std::cout << "\n=== Performance Characteristics ===" << std::endl;
    std::cout << "Order Book Features:" << std::endl;
    std::cout << "  ✓ O(1) order insertion" << std::endl;
    std::cout << "  ✓ O(1) order cancellation" << std::endl;
    std::cout << "  ✓ O(1) price level access" << std::endl;
    std::cout << "  ✓ Lock-free memory pool allocation" << std::endl;
    std::cout << "  ✓ Cache-friendly data structures" << std::endl;
    std::cout << "  ✓ Zero-copy order matching" << std::endl;
    std::cout << "  ✓ Nanosecond timestamp precision" << std::endl;
    std::cout << "\nRL Integration Features:" << std::endl;
    std::cout << "  ✓ Real-time market state observation" << std::endl;
    std::cout << "  ✓ Order flow imbalance tracking" << std::endl;
    std::cout << "  ✓ VWAP and volatility calculation" << std::endl;
    std::cout << "  ✓ Position and PnL management" << std::endl;
    std::cout << "  ✓ Custom reward function" << std::endl;
    std::cout << "  ✓ Backtesting framework" << std::endl;
    std::cout << "  ✓ Market simulation for training" << std::endl;
    
    return 0;
}
