#pragma once

#include "../backend/orderbook.hpp"
#include "../agent/rl_agent.hpp"
#include <ncurses.h>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace orderbook {

// ===== IMPERIAL HFT OPTIMIZATIONS =====

// 1. Constexpr compile-time calculations
namespace Constants {
    constexpr double RISK_AVERSION = 0.1;
    constexpr double INVENTORY_LIMIT = 10000.0;
    constexpr double MIN_SPREAD = 0.01;
    constexpr size_t PRICE_BUFFER_SIZE = 50;
    constexpr size_t PREFETCH_DISTANCE = 8;
    
    // Compile-time factorial for statistical calculations
    constexpr int factorial(int n) {
        return (n <= 1) ? 1 : (n * factorial(n - 1));
    }
    
    // Compile-time spread calculation multiplier
    constexpr double spread_multiplier(int risk_level) {
        return 1.0 + (risk_level * 0.05);
    }
}

// 2. Error handling flags for branch reduction
enum ErrorFlags : uint32_t {
    NO_ERROR = 0,
    INVALID_PRICE = 1 << 0,
    INVALID_QUANTITY = 1 << 1,
    MARKET_CLOSED = 1 << 2,
    POSITION_LIMIT = 1 << 3,
    CONNECTIVITY_ERROR = 1 << 4,
    ORDER_REJECT = 1 << 5
};

// Hot data structure with cache-line alignment
struct alignas(64) HotData {
    alignas(64) Price best_bid;
    alignas(64) Price best_ask;
    alignas(64) double mid_price;
    alignas(64) double volatility;
    alignas(64) double imbalance;
    alignas(64) uint64_t version;
};

// Welford's online algorithm for O(1) variance/stddev calculation
class OnlineStats {
private:
    size_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;  // Sum of squared differences from mean
    
public:
    [[gnu::always_inline]]
    inline void update(double value) noexcept {
        count_++;
        double delta = value - mean_;
        mean_ += delta / count_;
        double delta2 = value - mean_;
        m2_ += delta * delta2;
    }
    
    [[gnu::always_inline]]
    inline double variance() const noexcept {
        return count_ > 1 ? m2_ / count_ : 0.0;
    }
    
    [[gnu::always_inline]]
    inline double stddev() const noexcept {
        return std::sqrt(variance());
    }
    
    [[gnu::always_inline]]
    inline size_t count() const noexcept { return count_; }
    
    void reset() noexcept {
        count_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;
    }
};

struct TradeInfo {
    Price price;
    Quantity quantity;
    Side side;
    std::string timestamp;
};

class TerminalUI {
private:
    OrderBook& orderbook_;
    RLAgent* rl_agent_;
    bool automated_mode_;
    std::deque<TradeInfo> recent_trades_;
    std::vector<std::string> command_history_;
    std::string current_command_;
    size_t max_trades_display_;
    size_t max_depth_;
    
    // Window pointers
    WINDOW* header_win_;
    WINDOW* book_win_;
    WINDOW* trades_win_;
    WINDOW* stats_win_;
    WINDOW* input_win_;
    
    // Dimensions
    int term_height_;
    int term_width_;
    
    // Colors
    enum ColorPairs {
        DEFAULT_PAIR = 1,
        BID_PAIR = 2,
        ASK_PAIR = 3,
        TRADE_BUY_PAIR = 4,
        TRADE_SELL_PAIR = 5,
        HEADER_PAIR = 6,
        HIGHLIGHT_PAIR = 7
    };
    
    void init_colors();
    void create_windows();
    void draw_header();
    void draw_order_book();
    void draw_trades();
    void draw_stats();
    void draw_input();
    void refresh_all();
    
    // Command processing
    struct OrderCommand {
        Side side;
        OrderType type;
        Price price;
        Quantity quantity;
        bool valid;
        std::string error;
    };
    
    OrderCommand parse_command(const std::string& cmd);
    void execute_command(const std::string& cmd);
    void show_help();
    
    // Automated trading
    void execute_rl_action();
    RLAgent::Action select_best_action();
    
    // Advanced market making strategies - HFT optimized
    std::array<Price, Constants::PRICE_BUFFER_SIZE> price_buffer_;  // Fixed-size ring buffer
    size_t buffer_idx_ = 0;
    size_t buffer_count_ = 0;
    OnlineStats volatility_stats_;  // O(1) online volatility calculation
    
    // Imperial HFT: Cache-aligned hot data for L1 cache optimization
    HotData hot_data_;
    
    // Caching for performance
    double cached_volatility_ = 0.0;
    double cached_imbalance_ = 0.0;
    uint64_t orderbook_version_ = 0;  // Track order book changes
    uint64_t cached_version_ = 0;     // Version of cached data
    
    // Error handling state
    ErrorFlags error_state_ = NO_ERROR;
    
    // Imperial HFT: Slow-path removal - noinline error handlers
    __attribute__((noinline)) void handle_error(ErrorFlags flags);
    __attribute__((noinline)) void log_error(const std::string& message);
    
    // Imperial HFT: Cache warming - pre-load hot data
    void warm_cache() noexcept;
    
    // Fast-path helpers with prefetching
    [[gnu::always_inline]]
    inline double calculate_volatility() noexcept;
    
    [[gnu::always_inline]]
    inline double calculate_order_book_imbalance() noexcept;
    
    [[gnu::always_inline]]
    inline void prefetch_order_levels(Price start_price, Side side) noexcept;
    
public:
    explicit TerminalUI(OrderBook& book, RLAgent* agent = nullptr, size_t max_trades = 20, size_t max_depth = 15);
    ~TerminalUI();
    
    void init();
    void cleanup();
    void run();
    void update();
    
    // Mode control
    void toggle_automated_mode();
    bool is_automated() const { return automated_mode_; }
    
    // Callback for trade notifications
    void on_trade(const Trade& trade);
};

} // namespace orderbook
