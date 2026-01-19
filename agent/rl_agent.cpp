#include "rl_agent.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orderbook {

RLAgent::RLAgent(OrderBook& book, double initial_cash)
    : orderbook_(book), cash_(initial_cash), initial_cash_(initial_cash),
      inventory_penalty_coef_(0.01), spread_capture_reward_(1.0),
      total_trades_(0), total_volume_(0.0),
      total_execution_time_ns_(0.0), action_count_(0) {
    
    // Pre-allocate to avoid reallocation overhead
    active_orders_.reserve(100);
    
    // Register callback to update position on trades
    orderbook_.register_trade_callback([this](const Trade& trade) {
        this->update_position(trade);
    });
}

void RLAgent::update_position(const Trade& trade) {
    // Check if this trade involves our orders
    bool is_buy = false;
    bool is_ours = false;
    
    for (OrderId id : active_orders_) {
        if (id == trade.buy_order_id) {
            is_buy = true;
            is_ours = true;
            break;
        } else if (id == trade.sell_order_id) {
            is_buy = false;
            is_ours = true;
            break;
        }
    }
    
    if (!is_ours) return;
    
    ++total_trades_;
    total_volume_ += trade.quantity;
    
    if (is_buy) {
        // Buying - increase position
        if (position_.quantity < 0) {
            // Closing short position
            int64_t close_qty = std::min((int64_t)trade.quantity, -position_.quantity);
            double pnl = close_qty * (position_.avg_price - trade.price / 100.0);
            position_.realized_pnl += pnl;
            cash_ += pnl;
            position_.quantity += close_qty;
            
            // Opening new long if trade is larger
            if (trade.quantity > close_qty) {
                int64_t new_qty = trade.quantity - close_qty;
                position_.avg_price = trade.price / 100.0;
                position_.quantity += new_qty;
                cash_ -= new_qty * position_.avg_price;
            }
        } else {
            // Adding to long position or opening new
            double total_cost = position_.quantity * position_.avg_price + 
                               trade.quantity * (trade.price / 100.0);
            position_.quantity += trade.quantity;
            position_.avg_price = total_cost / position_.quantity;
            cash_ -= trade.quantity * (trade.price / 100.0);
        }
    } else {
        // Selling - decrease position
        if (position_.quantity > 0) {
            // Closing long position
            int64_t close_qty = std::min((int64_t)trade.quantity, position_.quantity);
            double pnl = close_qty * (trade.price / 100.0 - position_.avg_price);
            position_.realized_pnl += pnl;
            cash_ += pnl + close_qty * position_.avg_price;
            position_.quantity -= close_qty;
            
            // Opening new short if trade is larger
            if (trade.quantity > close_qty) {
                int64_t new_qty = trade.quantity - close_qty;
                position_.avg_price = trade.price / 100.0;
                position_.quantity -= new_qty;
                cash_ += new_qty * position_.avg_price;
            }
        } else {
            // Adding to short position or opening new
            double total_value = -position_.quantity * position_.avg_price + 
                                trade.quantity * (trade.price / 100.0);
            position_.quantity -= trade.quantity;
            position_.avg_price = total_value / (-position_.quantity);
            cash_ += trade.quantity * (trade.price / 100.0);
        }
    }
}

RLAgent::Observation RLAgent::get_observation() const {
    Observation obs;
    obs.market_state = orderbook_.get_market_state();
    obs.position = position_;
    obs.active_orders = active_orders_;
    obs.cash = cash_;
    obs.portfolio_value = get_portfolio_value();
    
    // Update unrealized PnL
    if (position_.quantity != 0 && obs.market_state.mid_price > 0) {
        if (position_.quantity > 0) {
            obs.position.unrealized_pnl = position_.quantity * 
                (obs.market_state.mid_price - position_.avg_price);
        } else {
            obs.position.unrealized_pnl = -position_.quantity * 
                (position_.avg_price - obs.market_state.mid_price);
        }
    }
    
    return obs;
}

RLAgent::Reward RLAgent::execute_action(Action action, Quantity quantity) {
    auto start = std::chrono::high_resolution_clock::now();
    
    const double previous_pnl = position_.realized_pnl + position_.unrealized_pnl;
    
    // Fast path: only fetch what we need based on action
    if (action != Action::HOLD && action != Action::CANCEL_ALL) {
        const auto best_bid = orderbook_.get_best_bid();
        const auto best_ask = orderbook_.get_best_ask();
        
        switch (action) {
            case Action::BUY_MARKET:
                if (best_ask) [[likely]] {
                    active_orders_.emplace_back(
                        orderbook_.add_order(*best_ask, quantity, Side::BUY, OrderType::MARKET)
                    );
                }
                break;
                
            case Action::SELL_MARKET:
                if (best_bid) [[likely]] {
                    active_orders_.emplace_back(
                        orderbook_.add_order(*best_bid, quantity, Side::SELL, OrderType::MARKET)
                    );
                }
                break;
                
            case Action::BUY_LIMIT_AT_BID:
                if (best_bid) [[likely]] {
                    active_orders_.emplace_back(
                        orderbook_.add_order(*best_bid, quantity, Side::BUY, OrderType::LIMIT)
                    );
                }
                break;
                
            case Action::SELL_LIMIT_AT_ASK:
                if (best_ask) [[likely]] {
                    active_orders_.emplace_back(
                        orderbook_.add_order(*best_ask, quantity, Side::SELL, OrderType::LIMIT)
                    );
                }
                break;
                
            case Action::BUY_LIMIT_AGGRESSIVE:
                if (best_bid && best_ask) [[likely]] {
                    const Price aggressive_price = (*best_bid + *best_ask) >> 1;
                    active_orders_.emplace_back(
                        orderbook_.add_order(aggressive_price, quantity, Side::BUY, OrderType::LIMIT)
                    );
                }
                break;
                
            case Action::SELL_LIMIT_AGGRESSIVE:
                if (best_bid && best_ask) [[likely]] {
                    const Price aggressive_price = (*best_bid + *best_ask) >> 1;
                    active_orders_.emplace_back(
                        orderbook_.add_order(aggressive_price, quantity, Side::SELL, OrderType::LIMIT)
                    );
                }
                break;
                
            default:
                break;
        }
    } else if (action == Action::CANCEL_ALL) {
        for (const OrderId id : active_orders_) {
            orderbook_.cancel_order(id);
        }
        active_orders_.clear();
    }
    
    // Only cleanup every 10 actions to reduce overhead
    if (++action_count_ % 10 == 0) [[unlikely]] {
        size_t write_idx = 0;
        for (size_t read_idx = 0; read_idx < active_orders_.size(); ++read_idx) {
            const auto order = orderbook_.get_order(active_orders_[read_idx]);
            if (order.has_value() && 
                order->status != OrderStatus::FILLED &&
                order->status != OrderStatus::CANCELLED) {
                active_orders_[write_idx++] = active_orders_[read_idx];
            }
        }
        active_orders_.resize(write_idx);
    }
    
    // Track execution time
    auto end = std::chrono::high_resolution_clock::now();
    total_execution_time_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    return calculate_reward(previous_pnl);
}

RLAgent::Reward RLAgent::calculate_reward(double previous_pnl) {
    Reward reward;
    
    // Calculate current PnL
    double current_pnl = position_.realized_pnl + position_.unrealized_pnl;
    reward.pnl_change = current_pnl - previous_pnl;
    
    // Inventory penalty (discourage large positions)
    reward.inventory_penalty = -inventory_penalty_coef_ * std::abs(position_.quantity);
    
    // Reward for providing liquidity (spread capture)
    // This would be calculated based on fills at favorable prices
    reward.spread_capture = 0.0; // Simplified for now
    
    reward.total = reward.pnl_change + reward.inventory_penalty + reward.spread_capture;
    
    return reward;
}

void RLAgent::reset() {
    position_ = Position();
    active_orders_.clear();
    cash_ = initial_cash_;
    total_trades_ = 0;
    total_volume_ = 0.0;
    total_execution_time_ns_ = 0.0;
    action_count_ = 0;
}

double RLAgent::get_portfolio_value() const {
    double value = cash_ + position_.realized_pnl;
    
    // Add value of current position at mid price
    auto mid = orderbook_.get_mid_price();
    if (mid && position_.quantity != 0) {
        value += position_.quantity * (*mid / 100.0);
    }
    
    return value;
}

// MarketSimulator implementation
MarketSimulator::MarketSimulator(OrderBook& book, Price base_price, 
                                 double volatility, double arrival_rate)
    : orderbook_(book), rng_(std::random_device{}()), 
      base_price_(base_price), volatility_(volatility), 
      arrival_rate_(arrival_rate), spread_width_(0.01),
      price_dist_(0.0, volatility), 
      size_dist_(1.0 / 1000.0),  // Average 1000 shares
      side_dist_(0.5) {}

void MarketSimulator::simulate_step(size_t num_orders) {
    for (size_t i = 0; i < num_orders; ++i) {
        // Determine side
        Side side = side_dist_(rng_) ? Side::BUY : Side::SELL;
        
        // Generate price around base with volatility
        double price_offset = price_dist_(rng_);
        Price price = base_price_ + static_cast<Price>(price_offset * base_price_);
        
        // Add spread
        if (side == Side::BUY) {
            price -= static_cast<Price>(spread_width_ * base_price_ / 2);
        } else {
            price += static_cast<Price>(spread_width_ * base_price_ / 2);
        }
        
        // Generate size
        Quantity size = std::max(100ULL, 
            static_cast<Quantity>(size_dist_(rng_) * 10000));
        
        // Add order to book
        orderbook_.add_order(price, size, side, OrderType::LIMIT);
    }
}

void MarketSimulator::simulate_microseconds(uint64_t microseconds) {
    // Calculate expected number of orders based on arrival rate
    double expected_orders = arrival_rate_ * microseconds;
    
    // Use Poisson-like distribution
    std::poisson_distribution<size_t> order_dist(expected_orders);
    size_t num_orders = order_dist(rng_);
    
    simulate_step(num_orders);
}

// PerformanceMetrics implementation
void PerformanceMetrics::print() const {
    std::cout << "\n=== Performance Metrics ===" << std::endl;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Total Return:     " << (total_return * 100) << "%" << std::endl;
    std::cout << "Sharpe Ratio:     " << sharpe_ratio << std::endl;
    std::cout << "Sortino Ratio:    " << sortino_ratio << std::endl;
    std::cout << "Max Drawdown:     " << (max_drawdown * 100) << "%" << std::endl;
    std::cout << "Win Rate:         " << (win_rate * 100) << "%" << std::endl;
    std::cout << "Profit Factor:    " << profit_factor << std::endl;
    std::cout << "Total Trades:     " << total_trades << std::endl;
    std::cout << "Avg Trade Duration: " << avg_trade_duration << " steps" << std::endl;
    std::cout << "===========================\n" << std::endl;
}

// Backtester implementation
Backtester::Backtester(double initial_cash)
    : agent_(orderbook_, initial_cash) {
    equity_curve_.push_back(initial_cash);
}

template<typename Strategy>
void Backtester::run(Strategy& strategy, size_t num_steps) {
    for (size_t step = 0; step < num_steps; ++step) {
        // Get observation
        auto obs = agent_.get_observation();
        
        // Get action from strategy
        auto action = strategy(obs);
        
        // Execute action
        agent_.execute_action(action);
        
        // Record equity
        double equity = agent_.get_portfolio_value();
        equity_curve_.push_back(equity);
        
        if (!equity_curve_.empty() && equity_curve_.size() > 1) {
            double ret = (equity - equity_curve_[equity_curve_.size() - 2]) / 
                        equity_curve_[equity_curve_.size() - 2];
            returns_.push_back(ret);
        }
    }
}

PerformanceMetrics Backtester::calculate_metrics() const {
    PerformanceMetrics metrics;
    
    if (equity_curve_.size() < 2) {
        return metrics;
    }
    
    // Total return
    metrics.total_return = (equity_curve_.back() - equity_curve_.front()) / 
                          equity_curve_.front();
    
    // Sharpe ratio
    if (!returns_.empty()) {
        double mean_return = std::accumulate(returns_.begin(), returns_.end(), 0.0) / 
                            returns_.size();
        
        double variance = 0.0;
        for (double ret : returns_) {
            variance += (ret - mean_return) * (ret - mean_return);
        }
        variance /= returns_.size();
        double std_dev = std::sqrt(variance);
        
        if (std_dev > 0) {
            metrics.sharpe_ratio = mean_return / std_dev * std::sqrt(252.0); // Annualized
        }
        
        // Sortino ratio (downside deviation)
        double downside_variance = 0.0;
        size_t downside_count = 0;
        for (double ret : returns_) {
            if (ret < 0) {
                downside_variance += ret * ret;
                ++downside_count;
            }
        }
        if (downside_count > 0) {
            double downside_dev = std::sqrt(downside_variance / downside_count);
            if (downside_dev > 0) {
                metrics.sortino_ratio = mean_return / downside_dev * std::sqrt(252.0);
            }
        }
    }
    
    // Max drawdown
    double peak = equity_curve_[0];
    double max_dd = 0.0;
    for (double equity : equity_curve_) {
        if (equity > peak) {
            peak = equity;
        }
        double dd = (peak - equity) / peak;
        if (dd > max_dd) {
            max_dd = dd;
        }
    }
    metrics.max_drawdown = max_dd;
    
    // Trade statistics
    metrics.total_trades = agent_.get_total_trades();
    metrics.win_rate = 0.5; // Simplified
    metrics.profit_factor = 1.0; // Simplified
    metrics.avg_trade_duration = 1.0; // Simplified
    
    return metrics;
}

} // namespace orderbook
