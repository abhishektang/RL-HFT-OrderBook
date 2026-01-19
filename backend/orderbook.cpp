#include "orderbook.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace orderbook {

static std::atomic<OrderId> next_order_id{1};

OrderBook::OrderBook()
    : order_pool_(10), price_level_pool_(10),
      cumulative_volume_(0.0), cumulative_pq_(0.0) {
    recent_trade_prices_.reserve(MAX_RECENT_TRADES);
    recent_trade_quantities_.reserve(MAX_RECENT_TRADES);
}

OrderBook::~OrderBook() {
    // Clean up all orders
    for (auto& [id, order] : orders_) {
        order_pool_.deallocate(order);
    }
    
    // Clean up bid levels
    for (auto& [price, level] : bid_levels_) {
        price_level_pool_.deallocate(level);
    }
    
    // Clean up ask levels
    for (auto& [price, level] : ask_levels_) {
        price_level_pool_.deallocate(level);
    }
}

PriceLevel* OrderBook::get_or_create_level(Price price, Side side) {
    if (side == Side::BUY) {
        auto it = bid_levels_.find(price);
        if (it != bid_levels_.end()) {
            return it->second;
        }
        PriceLevel* level = price_level_pool_.allocate(price);
        bid_levels_[price] = level;
        return level;
    } else {
        auto it = ask_levels_.find(price);
        if (it != ask_levels_.end()) {
            return it->second;
        }
        PriceLevel* level = price_level_pool_.allocate(price);
        ask_levels_[price] = level;
        return level;
    }
}

void OrderBook::remove_level_if_empty(Price price, Side side) {
    if (side == Side::BUY) {
        auto it = bid_levels_.find(price);
        if (it != bid_levels_.end() && it->second->is_empty()) {
            price_level_pool_.deallocate(it->second);
            bid_levels_.erase(it);
        }
    } else {
        auto it = ask_levels_.find(price);
        if (it != ask_levels_.end() && it->second->is_empty()) {
            price_level_pool_.deallocate(it->second);
            ask_levels_.erase(it);
        }
    }
}

void OrderBook::execute_trade(Order* passive_order, Order* aggressive_order, Quantity quantity) {
    Quantity passive_old_remaining = passive_order->remaining_quantity();
    
    passive_order->filled_quantity += quantity;
    aggressive_order->filled_quantity += quantity;
    
    if (passive_order->is_fully_filled()) {
        passive_order->status = OrderStatus::FILLED;
    } else {
        passive_order->status = OrderStatus::PARTIALLY_FILLED;
    }
    
    if (aggressive_order->is_fully_filled()) {
        aggressive_order->status = OrderStatus::FILLED;
    } else {
        aggressive_order->status = OrderStatus::PARTIALLY_FILLED;
    }
    
    // Update price level quantities
    PriceLevel* level = get_or_create_level(passive_order->price, passive_order->side);
    level->update_quantity(passive_order, passive_old_remaining);
    
    // Create trade record
    Trade trade(
        passive_order->side == Side::BUY ? passive_order->id : aggressive_order->id,
        passive_order->side == Side::SELL ? passive_order->id : aggressive_order->id,
        passive_order->price,
        quantity
    );
    
    // Update statistics
    update_market_statistics(trade);
    
    // Notify callbacks
    notify_trade(trade);
    notify_order_update(*passive_order);
    notify_order_update(*aggressive_order);
    
    // Remove filled passive order from book
    if (passive_order->is_fully_filled()) {
        level->remove_order(passive_order);
        remove_level_if_empty(passive_order->price, passive_order->side);
    }
}

void OrderBook::match_order(Order* incoming_order) {
    if (incoming_order->side == Side::BUY) {
        // Match against asks
        while (!incoming_order->is_fully_filled() && !ask_levels_.empty()) {
            auto it = ask_levels_.begin();
            PriceLevel* best_ask_level = it->second;
            
            // Check if price crosses
            if (incoming_order->price < best_ask_level->price) {
                break;
            }
            
            Order* passive_order = best_ask_level->get_best_order();
            if (!passive_order) break;
            
            Quantity match_quantity = std::min(
                incoming_order->remaining_quantity(),
                passive_order->remaining_quantity()
            );
            
            execute_trade(passive_order, incoming_order, match_quantity);
            
            // Check order type constraints
            if (incoming_order->type == OrderType::IOC && !incoming_order->is_fully_filled()) {
                incoming_order->status = OrderStatus::CANCELLED;
                break;
            }
            if (incoming_order->type == OrderType::FOK && !incoming_order->is_fully_filled()) {
                incoming_order->status = OrderStatus::REJECTED;
                break;
            }
        }
    } else {
        // Match against bids
        while (!incoming_order->is_fully_filled() && !bid_levels_.empty()) {
            auto it = bid_levels_.begin();
            PriceLevel* best_bid_level = it->second;
            
            // Check if price crosses
            if (incoming_order->price > best_bid_level->price) {
                break;
            }
            
            Order* passive_order = best_bid_level->get_best_order();
            if (!passive_order) break;
            
            Quantity match_quantity = std::min(
                incoming_order->remaining_quantity(),
                passive_order->remaining_quantity()
            );
            
            execute_trade(passive_order, incoming_order, match_quantity);
            
            // Check order type constraints
            if (incoming_order->type == OrderType::IOC && !incoming_order->is_fully_filled()) {
                incoming_order->status = OrderStatus::CANCELLED;
                break;
            }
            if (incoming_order->type == OrderType::FOK && !incoming_order->is_fully_filled()) {
                incoming_order->status = OrderStatus::REJECTED;
                break;
            }
        }
    }
}

OrderId OrderBook::add_order(Price price, Quantity quantity, Side side, OrderType type) {
    OrderId id = next_order_id.fetch_add(1);
    Order* order = order_pool_.allocate(id, price, quantity, side, type);
    orders_[id] = order;
    
    // Market orders always match
    if (type == OrderType::MARKET) {
        // Use best available price
        if (side == Side::BUY && !ask_levels_.empty()) {
            order->price = ask_levels_.begin()->first;
        } else if (side == Side::SELL && !bid_levels_.empty()) {
            order->price = bid_levels_.begin()->first;
        }
    }
    
    // Try to match the order
    match_order(order);
    
    // If order still has remaining quantity and is a limit order, add to book
    if (!order->is_fully_filled() && 
        order->status != OrderStatus::CANCELLED && 
        order->status != OrderStatus::REJECTED &&
        type == OrderType::LIMIT) {
        
        PriceLevel* level = get_or_create_level(price, side);
        level->add_order(order);
        notify_order_update(*order);
    } else if (order->status == OrderStatus::CANCELLED || order->status == OrderStatus::REJECTED) {
        // Remove rejected/cancelled orders
        orders_.erase(id);
        order_pool_.deallocate(order);
    }
    
    // Notify state update for RL
    for (auto& callback : state_callbacks_) {
        callback(get_market_state());
    }
    
    return id;
}

bool OrderBook::cancel_order(OrderId order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    
    Order* order = it->second;
    
    // Remove from price level if not fully filled
    if (!order->is_fully_filled()) {
        PriceLevel* level = get_or_create_level(order->price, order->side);
        level->remove_order(order);
        remove_level_if_empty(order->price, order->side);
    }
    
    order->status = OrderStatus::CANCELLED;
    notify_order_update(*order);
    
    orders_.erase(it);
    order_pool_.deallocate(order);
    
    return true;
}

bool OrderBook::modify_order(OrderId order_id, Price new_price, Quantity new_quantity) {
    // Cancel and replace strategy for simplicity
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    
    Order* old_order = it->second;
    Side side = old_order->side;
    OrderType type = old_order->type;
    
    cancel_order(order_id);
    add_order(new_price, new_quantity, side, type);
    
    return true;
}

std::optional<Order> OrderBook::get_order(OrderId order_id) const {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return std::nullopt;
    }
    return *it->second;
}

std::optional<Price> OrderBook::get_best_bid() const {
    if (bid_levels_.empty()) {
        return std::nullopt;
    }
    return bid_levels_.begin()->first;
}

std::optional<Price> OrderBook::get_best_ask() const {
    if (ask_levels_.empty()) {
        return std::nullopt;
    }
    return ask_levels_.begin()->first;
}

std::optional<Price> OrderBook::get_mid_price() const {
    auto bid = get_best_bid();
    auto ask = get_best_ask();
    if (bid && ask) {
        return (*bid + *ask) / 2;
    }
    return std::nullopt;
}

std::optional<Price> OrderBook::get_spread() const {
    auto bid = get_best_bid();
    auto ask = get_best_ask();
    if (bid && ask) {
        return *ask - *bid;
    }
    return std::nullopt;
}

Quantity OrderBook::get_volume_at_price(Price price, Side side) const {
    if (side == Side::BUY) {
        auto it = bid_levels_.find(price);
        if (it != bid_levels_.end()) {
            return it->second->total_quantity;
        }
    } else {
        auto it = ask_levels_.find(price);
        if (it != ask_levels_.end()) {
            return it->second->total_quantity;
        }
    }
    return 0;
}

MarketState OrderBook::get_market_state() const {
    MarketState state;
    state.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    // Best bid/ask
    auto best_bid = get_best_bid();
    auto best_ask = get_best_ask();
    
    state.best_bid = best_bid.value_or(0);
    state.best_ask = best_ask.value_or(0);
    
    if (best_bid && best_ask) {
        state.spread = *best_ask - *best_bid;
        state.mid_price = (*best_bid + *best_ask) / 2.0;
    } else {
        state.spread = 0;
        state.mid_price = 0.0;
    }
    
    // Bid depth
    state.bid_quantity = 0;
    size_t count = 0;
    for (const auto& [price, level] : bid_levels_) {
        if (count >= DEPTH_LEVELS) break;
        state.bid_levels.emplace_back(price, level->total_quantity);
        if (count == 0) {
            state.bid_quantity = level->total_quantity;
        }
        ++count;
    }
    
    // Ask depth
    state.ask_quantity = 0;
    count = 0;
    for (const auto& [price, level] : ask_levels_) {
        if (count >= DEPTH_LEVELS) break;
        state.ask_levels.emplace_back(price, level->total_quantity);
        if (count == 0) {
            state.ask_quantity = level->total_quantity;
        }
        ++count;
    }
    
    // Order flow imbalance
    if (state.bid_quantity + state.ask_quantity > 0) {
        state.order_flow_imbalance = 
            (double)(state.bid_quantity - state.ask_quantity) / 
            (double)(state.bid_quantity + state.ask_quantity);
    } else {
        state.order_flow_imbalance = 0.0;
    }
    
    // Recent trade info
    if (!recent_trade_prices_.empty()) {
        state.last_trade_price = recent_trade_prices_.back();
        state.last_trade_quantity = recent_trade_quantities_.back();
    } else {
        state.last_trade_price = 0;
        state.last_trade_quantity = 0;
    }
    
    // VWAP
    if (cumulative_volume_ > 0) {
        state.vwap = cumulative_pq_ / cumulative_volume_;
    } else {
        state.vwap = 0.0;
    }
    
    // Price volatility (standard deviation of recent prices)
    if (recent_trade_prices_.size() > 1) {
        double mean = std::accumulate(recent_trade_prices_.begin(), 
                                     recent_trade_prices_.end(), 0.0) / 
                     recent_trade_prices_.size();
        
        double sq_sum = 0.0;
        for (Price p : recent_trade_prices_) {
            sq_sum += (p - mean) * (p - mean);
        }
        state.price_volatility = std::sqrt(sq_sum / recent_trade_prices_.size());
    } else {
        state.price_volatility = 0.0;
    }
    
    return state;
}

void OrderBook::update_market_statistics(const Trade& trade) {
    recent_trade_prices_.push_back(trade.price);
    recent_trade_quantities_.push_back(trade.quantity);
    
    if (recent_trade_prices_.size() > MAX_RECENT_TRADES) {
        recent_trade_prices_.erase(recent_trade_prices_.begin());
        recent_trade_quantities_.erase(recent_trade_quantities_.begin());
    }
    
    cumulative_volume_ += trade.quantity;
    cumulative_pq_ += trade.price * trade.quantity;
}

void OrderBook::notify_trade(const Trade& trade) {
    for (auto& callback : trade_callbacks_) {
        callback(trade);
    }
}

void OrderBook::notify_order_update(const Order& order) {
    for (auto& callback : order_callbacks_) {
        callback(order);
    }
}

void OrderBook::register_trade_callback(TradeCallback callback) {
    trade_callbacks_.push_back(std::move(callback));
}

void OrderBook::register_order_callback(OrderUpdateCallback callback) {
    order_callbacks_.push_back(std::move(callback));
}

void OrderBook::register_state_callback(MarketStateCallback callback) {
    state_callbacks_.push_back(std::move(callback));
}

void OrderBook::print_book(size_t depth) const {
    std::cout << "\n=== Order Book ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "\nAsks:" << std::endl;
    std::vector<std::pair<Price, Quantity>> asks;
    for (const auto& [price, level] : ask_levels_) {
        asks.emplace_back(price, level->total_quantity);
        if (asks.size() >= depth) break;
    }
    std::reverse(asks.begin(), asks.end());
    for (const auto& [price, qty] : asks) {
        std::cout << "  " << std::setw(10) << price / 100.0 
                  << " | " << std::setw(10) << qty << std::endl;
    }
    
    std::cout << "  " << std::string(23, '-') << std::endl;
    
    std::cout << "Bids:" << std::endl;
    size_t count = 0;
    for (const auto& [price, level] : bid_levels_) {
        std::cout << "  " << std::setw(10) << price / 100.0 
                  << " | " << std::setw(10) << level->total_quantity << std::endl;
        if (++count >= depth) break;
    }
    
    auto spread = get_spread();
    auto mid = get_mid_price();
    if (spread && mid) {
        std::cout << "\nSpread: " << *spread / 100.0 
                  << " | Mid: " << *mid / 100.0 << std::endl;
    }
    std::cout << "==================\n" << std::endl;
}

} // namespace orderbook
