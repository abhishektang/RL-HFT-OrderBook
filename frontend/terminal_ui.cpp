#include "terminal_ui.hpp"
#include <chrono>
#include <ctime>
#include <algorithm>

namespace orderbook {

TerminalUI::TerminalUI(OrderBook& book, RLAgent* agent, size_t max_trades, size_t max_depth)
    : orderbook_(book), rl_agent_(agent), automated_mode_(false),
      max_trades_display_(max_trades), max_depth_(max_depth),
      header_win_(nullptr), book_win_(nullptr), trades_win_(nullptr),
      stats_win_(nullptr), input_win_(nullptr),
      term_height_(0), term_width_(0) {
    
    // Register trade callback
    orderbook_.register_trade_callback([this](const Trade& trade) {
        this->on_trade(trade);
    });
}

TerminalUI::~TerminalUI() {
    cleanup();
}

void TerminalUI::init() {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(1);
    
    // Get terminal dimensions
    getmaxyx(stdscr, term_height_, term_width_);
    
    // Initialize colors
    if (has_colors()) {
        start_color();
        init_colors();
    }
    
    // Create windows
    create_windows();
    
    // Initial draw
    update();
}

void TerminalUI::init_colors() {
    init_pair(DEFAULT_PAIR, COLOR_WHITE, COLOR_BLACK);
    init_pair(BID_PAIR, COLOR_GREEN, COLOR_BLACK);
    init_pair(ASK_PAIR, COLOR_RED, COLOR_BLACK);
    init_pair(TRADE_BUY_PAIR, COLOR_GREEN, COLOR_BLACK);
    init_pair(TRADE_SELL_PAIR, COLOR_RED, COLOR_BLACK);
    init_pair(HEADER_PAIR, COLOR_CYAN, COLOR_BLACK);
    init_pair(HIGHLIGHT_PAIR, COLOR_YELLOW, COLOR_BLACK);
}

void TerminalUI::create_windows() {
    int header_height = 3;
    int input_height = 3;
    int stats_height = 8;
    
    int book_trades_height = term_height_ - header_height - stats_height - input_height - 2;
    int book_width = term_width_ * 2 / 3;
    int trades_width = term_width_ - book_width;
    
    // Header window (top)
    header_win_ = newwin(header_height, term_width_, 0, 0);
    
    // Order book window (left, middle)
    book_win_ = newwin(book_trades_height, book_width, header_height, 0);
    
    // Trades window (right, middle)
    trades_win_ = newwin(book_trades_height, trades_width, header_height, book_width);
    
    // Stats window (bottom-ish)
    stats_win_ = newwin(stats_height, term_width_, header_height + book_trades_height, 0);
    
    // Input window (bottom)
    input_win_ = newwin(input_height, term_width_, term_height_ - input_height, 0);
    
    // Enable scrolling for trades window
    scrollok(trades_win_, TRUE);
}

void TerminalUI::cleanup() {
    if (header_win_) delwin(header_win_);
    if (book_win_) delwin(book_win_);
    if (trades_win_) delwin(trades_win_);
    if (stats_win_) delwin(stats_win_);
    if (input_win_) delwin(input_win_);
    
    endwin();
}

void TerminalUI::draw_header() {
    werase(header_win_);
    box(header_win_, 0, 0);
    
    wattron(header_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    mvwprintw(header_win_, 1, 2, "ORDER BOOK TERMINAL UI");
    wattroff(header_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    
    // Show mode status
    if (automated_mode_ && rl_agent_) {
        wattron(header_win_, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
        mvwprintw(header_win_, 1, 30, "[AUTO MODE - RL AGENT]");
        wattroff(header_win_, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
    } else {
        wattron(header_win_, COLOR_PAIR(DEFAULT_PAIR));
        mvwprintw(header_win_, 1, 30, "[MANUAL MODE]");
        wattroff(header_win_, COLOR_PAIR(DEFAULT_PAIR));
    }
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back(); // Remove newline
    
    mvwprintw(header_win_, 1, term_width_ - time_str.length() - 2, "%s", time_str.c_str());
    
    wrefresh(header_win_);
}

void TerminalUI::draw_order_book() {
    werase(book_win_);
    box(book_win_, 0, 0);
    
    wattron(book_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    mvwprintw(book_win_, 0, 2, " ORDER BOOK ");
    wattroff(book_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    
    int height, width;
    getmaxyx(book_win_, height, width);
    
    int mid_line = height / 2;
    
    // Draw asks (top half, reversed order)
    auto best_ask = orderbook_.get_best_ask();
    if (best_ask) {
        std::vector<std::pair<Price, Quantity>> asks;
        
        // Collect ask levels (this is a simplified approach - ideally expose this in OrderBook)
        // For now, we'll estimate based on what we know
        Price current_price = *best_ask;
        for (size_t i = 0; i < max_depth_ && i < mid_line - 2; ++i) {
            Quantity vol = orderbook_.get_volume_at_price(current_price, Side::SELL);
            if (vol > 0) {
                asks.emplace_back(current_price, vol);
            }
            current_price += 1; // Move up in price
        }
        
        // Draw asks from bottom to top (reverse order)
        std::reverse(asks.begin(), asks.end());
        int line = mid_line - 1;
        for (const auto& [price, qty] : asks) {
            if (line <= 1) break;
            
            wattron(book_win_, COLOR_PAIR(ASK_PAIR));
            mvwprintw(book_win_, line, 2, "ASK");
            wattroff(book_win_, COLOR_PAIR(ASK_PAIR));
            
            mvwprintw(book_win_, line, 8, "%10.2f", price / 100.0);
            mvwprintw(book_win_, line, 22, "%12llu", qty);
            
            // Draw volume bar
            int bar_width = std::min(20, (int)(qty / 1000));
            wattron(book_win_, COLOR_PAIR(ASK_PAIR));
            for (int i = 0; i < bar_width && 38 + i < width - 2; ++i) {
                mvwaddch(book_win_, line, 38 + i, ACS_CKBOARD);
            }
            wattroff(book_win_, COLOR_PAIR(ASK_PAIR));
            
            --line;
        }
    }
    
    // Draw spread line
    wattron(book_win_, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
    mvwhline(book_win_, mid_line, 1, ACS_HLINE, width - 2);
    
    auto spread = orderbook_.get_spread();
    auto mid = orderbook_.get_mid_price();
    if (spread && mid) {
        mvwprintw(book_win_, mid_line, width / 2 - 15, " SPREAD: %.2f | MID: %.2f ", 
                  *spread / 100.0, *mid / 100.0);
    }
    wattroff(book_win_, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
    
    // Draw bids (bottom half)
    auto best_bid = orderbook_.get_best_bid();
    if (best_bid) {
        Price current_price = *best_bid;
        int line = mid_line + 1;
        
        for (size_t i = 0; i < max_depth_ && line < height - 1; ++i) {
            Quantity vol = orderbook_.get_volume_at_price(current_price, Side::BUY);
            if (vol > 0) {
                wattron(book_win_, COLOR_PAIR(BID_PAIR));
                mvwprintw(book_win_, line, 2, "BID");
                wattroff(book_win_, COLOR_PAIR(BID_PAIR));
                
                mvwprintw(book_win_, line, 8, "%10.2f", current_price / 100.0);
                mvwprintw(book_win_, line, 22, "%12llu", vol);
                
                // Draw volume bar
                int bar_width = std::min(20, (int)(vol / 1000));
                wattron(book_win_, COLOR_PAIR(BID_PAIR));
                for (int i = 0; i < bar_width && 38 + i < width - 2; ++i) {
                    mvwaddch(book_win_, line, 38 + i, ACS_CKBOARD);
                }
                wattroff(book_win_, COLOR_PAIR(BID_PAIR));
                
                ++line;
            }
            current_price -= 1; // Move down in price
        }
    }
    
    wrefresh(book_win_);
}

void TerminalUI::draw_trades() {
    werase(trades_win_);
    box(trades_win_, 0, 0);
    
    wattron(trades_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    mvwprintw(trades_win_, 0, 2, " RECENT TRADES ");
    wattroff(trades_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    
    int height, width;
    getmaxyx(trades_win_, height, width);
    
    // Header
    mvwprintw(trades_win_, 2, 2, "SIDE");
    mvwprintw(trades_win_, 2, 8, "PRICE");
    mvwprintw(trades_win_, 2, 17, "QTY");
    
    // Draw recent trades
    int line = 3;
    for (auto it = recent_trades_.rbegin(); it != recent_trades_.rend() && line < height - 1; ++it) {
        int color = (it->side == Side::BUY) ? BID_PAIR : ASK_PAIR;
        const char* side_str = (it->side == Side::BUY) ? "BUY" : "SELL";
        
        wattron(trades_win_, COLOR_PAIR(color));
        mvwprintw(trades_win_, line, 2, "%-4s", side_str);
        wattroff(trades_win_, COLOR_PAIR(color));
        
        mvwprintw(trades_win_, line, 8, "%7.2f", it->price / 100.0);
        mvwprintw(trades_win_, line, 17, "%6llu", it->quantity);
        
        ++line;
    }
    
    wrefresh(trades_win_);
}

void TerminalUI::draw_stats() {
    werase(stats_win_);
    box(stats_win_, 0, 0);
    
    wattron(stats_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    mvwprintw(stats_win_, 0, 2, " MARKET STATISTICS ");
    wattroff(stats_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    
    auto state = orderbook_.get_market_state();
    
    // Left column
    mvwprintw(stats_win_, 1, 2, "Best Bid:");
    wattron(stats_win_, COLOR_PAIR(BID_PAIR) | A_BOLD);
    mvwprintw(stats_win_, 1, 20, "$%.2f", state.best_bid / 100.0);
    wattroff(stats_win_, COLOR_PAIR(BID_PAIR) | A_BOLD);
    
    mvwprintw(stats_win_, 2, 2, "Best Ask:");
    wattron(stats_win_, COLOR_PAIR(ASK_PAIR) | A_BOLD);
    mvwprintw(stats_win_, 2, 20, "$%.2f", state.best_ask / 100.0);
    wattroff(stats_win_, COLOR_PAIR(ASK_PAIR) | A_BOLD);
    
    mvwprintw(stats_win_, 3, 2, "Spread:");
    mvwprintw(stats_win_, 3, 20, "$%.2f", state.spread / 100.0);
    
    mvwprintw(stats_win_, 4, 2, "Mid Price:");
    mvwprintw(stats_win_, 4, 20, "$%.2f", state.mid_price / 100.0);
    
    // Right column
    int mid_col = term_width_ / 2;
    
    mvwprintw(stats_win_, 1, mid_col, "Total Orders:");
    mvwprintw(stats_win_, 1, mid_col + 20, "%zu", orderbook_.get_order_count());
    
    mvwprintw(stats_win_, 2, mid_col, "Bid Levels:");
    mvwprintw(stats_win_, 2, mid_col + 20, "%zu", orderbook_.get_bid_level_count());
    
    mvwprintw(stats_win_, 3, mid_col, "Ask Levels:");
    mvwprintw(stats_win_, 3, mid_col + 20, "%zu", orderbook_.get_ask_level_count());
    
    mvwprintw(stats_win_, 4, mid_col, "VWAP:");
    mvwprintw(stats_win_, 4, mid_col + 20, "$%.2f", state.vwap);
    
    mvwprintw(stats_win_, 5, 2, "Order Imbalance:");
    wattron(stats_win_, COLOR_PAIR(state.order_flow_imbalance > 0 ? BID_PAIR : ASK_PAIR));
    mvwprintw(stats_win_, 5, 20, "%.3f", state.order_flow_imbalance);
    wattroff(stats_win_, COLOR_PAIR(state.order_flow_imbalance > 0 ? BID_PAIR : ASK_PAIR));
    
    mvwprintw(stats_win_, 6, 2, "Volatility:");
    mvwprintw(stats_win_, 6, 20, "%.4f", state.price_volatility);
    
    // RL Agent stats (if available)
    if (rl_agent_) {
        mvwprintw(stats_win_, 5, mid_col, "RL Position:");
        auto position = rl_agent_->get_position();
        int pos_color = (position.quantity > 0) ? BID_PAIR : (position.quantity < 0) ? ASK_PAIR : DEFAULT_PAIR;
        wattron(stats_win_, COLOR_PAIR(pos_color) | A_BOLD);
        mvwprintw(stats_win_, 5, mid_col + 20, "%lld", position.quantity);
        wattroff(stats_win_, COLOR_PAIR(pos_color) | A_BOLD);
        
        mvwprintw(stats_win_, 6, mid_col, "RL PnL:");
        double total_pnl = position.realized_pnl + position.unrealized_pnl;
        int pnl_color = (total_pnl > 0) ? BID_PAIR : ASK_PAIR;
        wattron(stats_win_, COLOR_PAIR(pnl_color) | A_BOLD);
        mvwprintw(stats_win_, 6, mid_col + 20, "$%.2f", total_pnl);
        wattroff(stats_win_, COLOR_PAIR(pnl_color) | A_BOLD);
        
        // Show mode and active orders
        if (automated_mode_) {
            wattron(stats_win_, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
            mvwprintw(stats_win_, 7, 2, "AUTO TRADING ACTIVE");
            wattroff(stats_win_, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
        }
        
        auto obs = rl_agent_->get_observation();
        mvwprintw(stats_win_, 7, mid_col, "Active Orders:");
        mvwprintw(stats_win_, 7, mid_col + 20, "%zu", obs.active_orders.size());
    }
    
    wrefresh(stats_win_);
}

void TerminalUI::draw_input() {
    werase(input_win_);
    box(input_win_, 0, 0);
    
    wattron(input_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    mvwprintw(input_win_, 0, 2, " COMMAND INPUT ");
    wattroff(input_win_, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    
    mvwprintw(input_win_, 1, 2, "> %s", current_command_.c_str());
    
    // Show help hint with auto mode info
    wattron(input_win_, A_DIM);
    if (rl_agent_) {
        mvwprintw(input_win_, 1, term_width_ - 40, "[h]elp [a]uto [q]uit [TAB]macro");
    } else {
        mvwprintw(input_win_, 1, term_width_ - 30, "[h]elp [q]uit [TAB]macro");
    }
    wattroff(input_win_, A_DIM);
    
    wrefresh(input_win_);
}

void TerminalUI::refresh_all() {
    draw_header();
    draw_order_book();
    draw_trades();
    draw_stats();
    draw_input();
}

void TerminalUI::update() {
    refresh_all();
}

void TerminalUI::on_trade(const Trade& trade) {
    TradeInfo info;
    info.price = trade.price;
    info.quantity = trade.quantity;
    info.side = (trade.buy_order_id < trade.sell_order_id) ? Side::BUY : Side::SELL;
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str = time_str.substr(11, 8); // Extract HH:MM:SS
    info.timestamp = time_str;
    
    recent_trades_.push_back(info);
    if (recent_trades_.size() > max_trades_display_) {
        recent_trades_.pop_front();
    }
}

TerminalUI::OrderCommand TerminalUI::parse_command(const std::string& cmd) {
    OrderCommand result;
    result.valid = false;
    
    std::istringstream iss(cmd);
    std::string side_str, type_str;
    double price_dbl = 0;
    
    iss >> side_str >> type_str >> result.quantity >> price_dbl;
    
    if (iss.fail() && type_str != "market") {
        result.error = "Invalid command format. Use: [buy|sell] [limit|market] <quantity> [price]";
        return result;
    }
    
    // Parse side
    std::transform(side_str.begin(), side_str.end(), side_str.begin(), ::tolower);
    if (side_str == "buy" || side_str == "b") {
        result.side = Side::BUY;
    } else if (side_str == "sell" || side_str == "s") {
        result.side = Side::SELL;
    } else {
        result.error = "Invalid side. Use 'buy' or 'sell'";
        return result;
    }
    
    // Parse type
    std::transform(type_str.begin(), type_str.end(), type_str.begin(), ::tolower);
    if (type_str == "limit" || type_str == "l") {
        result.type = OrderType::LIMIT;
        if (price_dbl <= 0) {
            result.error = "Limit orders require a price";
            return result;
        }
        result.price = static_cast<Price>(price_dbl * 100); // Convert to ticks
    } else if (type_str == "market" || type_str == "m") {
        result.type = OrderType::MARKET;
        // Use best available price
        auto best = result.side == Side::BUY ? 
                   orderbook_.get_best_ask() : 
                   orderbook_.get_best_bid();
        result.price = best.value_or(10000);
    } else {
        result.error = "Invalid order type. Use 'limit' or 'market'";
        return result;
    }
    
    if (result.quantity <= 0) {
        result.error = "Quantity must be positive";
        return result;
    }
    
    result.valid = true;
    return result;
}

void TerminalUI::execute_command(const std::string& cmd) {
    if (cmd.empty()) return;
    
    if (cmd == "q" || cmd == "quit" || cmd == "exit") {
        return; // Will be handled in run()
    }
    
    if (cmd == "h" || cmd == "help") {
        show_help();
        return;
    }
    
    // Parse and execute order command
    auto order_cmd = parse_command(cmd);
    if (!order_cmd.valid) {
        // Show error (could add error window)
        beep();
        return;
    }
    
    // Execute order
    orderbook_.add_order(order_cmd.price, order_cmd.quantity, 
                        order_cmd.side, order_cmd.type);
    
    // Add to history
    command_history_.push_back(cmd);
}

void TerminalUI::show_help() {
    WINDOW* help_win = newwin(18, 70, (term_height_ - 18) / 2, (term_width_ - 70) / 2);
    box(help_win, 0, 0);
    
    wattron(help_win, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    mvwprintw(help_win, 0, 2, " HELP ");
    wattroff(help_win, COLOR_PAIR(HEADER_PAIR) | A_BOLD);
    
    mvwprintw(help_win, 2, 2, "Order Commands:");
    mvwprintw(help_win, 3, 4, "buy limit <qty> <price>   - Place limit buy order");
    mvwprintw(help_win, 4, 4, "sell limit <qty> <price>  - Place limit sell order");
    mvwprintw(help_win, 5, 4, "buy market <qty>          - Place market buy order");
    mvwprintw(help_win, 6, 4, "sell market <qty>         - Place market sell order");
    
    mvwprintw(help_win, 8, 2, "Shortcuts:");
    mvwprintw(help_win, 9, 4, "b/s = buy/sell, l/m = limit/market");
    
    mvwprintw(help_win, 11, 2, "Trading Modes:");
    if (rl_agent_) {
        wattron(help_win, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
        mvwprintw(help_win, 12, 4, "a/A - Toggle AUTOMATED MODE (RL Agent Trading)");
        wattroff(help_win, COLOR_PAIR(HIGHLIGHT_PAIR) | A_BOLD);
    }
    mvwprintw(help_win, 13, 4, "TAB - Generate random market activity");
    
    mvwprintw(help_win, 15, 2, "Other Commands:");
    mvwprintw(help_win, 16, 4, "h/help - Show this help   q/quit - Exit");
    
    mvwprintw(help_win, 17, 20, "Press any key to close");
    
    wrefresh(help_win);
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    delwin(help_win);
    
    update();
}

void TerminalUI::run() {
    bool running = true;
    auto last_rl_action = std::chrono::steady_clock::now();
    const auto rl_action_interval = std::chrono::milliseconds(500); // Execute RL action every 500ms
    
    while (running) {
        int ch = getch();
        
        // Execute RL actions periodically in automated mode
        if (automated_mode_ && rl_agent_) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_rl_action >= rl_action_interval) {
                execute_rl_action();
                last_rl_action = now;
            }
        }
        
        if (ch == ERR) {
            // No input, update display
            update();
            napms(100); // Sleep for 100ms
            continue;
        }
        
        if (ch == '\n' || ch == KEY_ENTER) {
            // Execute command
            if (current_command_ == "q" || current_command_ == "quit" || 
                current_command_ == "exit") {
                running = false;
            } else {
                execute_command(current_command_);
            }
            current_command_.clear();
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            // Handle backspace
            if (!current_command_.empty()) {
                current_command_.pop_back();
            }
        } else if (ch == '\t') {
            // TAB macro - generate random orders
            MarketSimulator sim(orderbook_, 10000, 0.005, 50.0);
            sim.simulate_step(100);
            current_command_.clear();
        } else if ((ch == 'a' || ch == 'A') && current_command_.empty()) {
            // Toggle automated mode (only when no command is being typed)
            toggle_automated_mode();
        } else if (ch >= 32 && ch <= 126) {
            // Printable character
            current_command_ += static_cast<char>(ch);
        }
        
        update();
    }
}

void TerminalUI::toggle_automated_mode() {
    if (rl_agent_) {
        automated_mode_ = !automated_mode_;
    }
}

void TerminalUI::execute_rl_action() {
    // Select best action based on current market state
    RLAgent::Action action = select_best_action();
    
    // Only execute if not holding
    if (action != RLAgent::Action::HOLD) {
        // Execute the action with appropriate quantity
        Quantity quantity = 100;
        auto reward = rl_agent_->execute_action(action, quantity);
        
        // Log action for debugging (store in recent_trades_ as a marker)
        // This helps verify the agent is working
    }
}

RLAgent::Action TerminalUI::select_best_action() {
    auto obs = rl_agent_->get_observation();
    
    auto best_bid = orderbook_.get_best_bid();
    auto best_ask = orderbook_.get_best_ask();
    
    if (!best_bid || !best_ask) {
        return RLAgent::Action::HOLD;
    }
    
    // Calculate mid-price and track for volatility using ring buffer
    Price mid_price = (*best_bid + *best_ask) >> 1;  // Bit shift for divide by 2
    
    // Ring buffer update
    price_buffer_[buffer_idx_] = mid_price;
    buffer_idx_ = (buffer_idx_ + 1) % PRICE_BUFFER_SIZE;
    if (buffer_count_ < PRICE_BUFFER_SIZE) {
        buffer_count_++;
    }
    
    // Update online volatility stats with return
    if (buffer_count_ >= 2) {
        size_t prev_idx = (buffer_idx_ + PRICE_BUFFER_SIZE - 2) % PRICE_BUFFER_SIZE;
        double ret = static_cast<double>(mid_price - price_buffer_[prev_idx]) / 
                     static_cast<double>(price_buffer_[prev_idx]);
        volatility_stats_.update(ret);
    }
    
    orderbook_version_++;  // Increment version for cache invalidation
    
    Price spread = *best_ask - *best_bid;
    int64_t position = obs.position.quantity;
    
    // === STRATEGY 1: Order Book Imbalance Detection ===
    // Detect adverse selection risk - pull quotes when imbalanced
    double imbalance = calculate_order_book_imbalance();
    if (std::abs(imbalance) > 0.4) {
        // High imbalance - potential informed trading
        // Cancel and wait for market to stabilize
        if (!obs.active_orders.empty()) {
            return RLAgent::Action::CANCEL_ALL;
        }
        return RLAgent::Action::HOLD;
    }
    
    // === STRATEGY 2: Volatility-Based Spread Adjustment ===
    double volatility = calculate_volatility();
    Price min_spread = static_cast<Price>(std::max(1.0, volatility * 200.0)); // Min 1 tick, scales with vol
    
    // Don't trade if spread is too tight for current volatility
    if (spread < min_spread) {
        return RLAgent::Action::HOLD;
    }
    
    // === STRATEGY 3: Inventory-Based Quote Skewing (Avellaneda-Stoikov) ===
    // Dynamic position limits based on volatility
    const int64_t max_position = static_cast<int64_t>(500.0 / (1.0 + volatility * 2.0));
    const int64_t urgent_threshold = max_position * 0.6;
    
    // Extreme position - urgent liquidation
    if (position > max_position) {
        // Very long - use aggressive sell to flatten
        return RLAgent::Action::SELL_LIMIT_AGGRESSIVE;
    }
    if (position < -max_position) {
        // Very short - use aggressive buy to flatten
        return RLAgent::Action::BUY_LIMIT_AGGRESSIVE;
    }
    
    // === STRATEGY 4: Inventory Risk Management ===
    // Calculate inventory factor (0 = neutral, 1 = at limit)
    double inventory_factor = static_cast<double>(std::abs(position)) / max_position;
    
    // Moderate position - skew quotes to reduce inventory
    if (position > urgent_threshold) {
        // Long position - incentivize selling
        // Quote at ask (easy to hit), avoid bidding
        if (inventory_factor > 0.7) {
            return RLAgent::Action::SELL_LIMIT_AGGRESSIVE;
        }
        return RLAgent::Action::SELL_LIMIT_AT_ASK;
    }
    
    if (position < -urgent_threshold) {
        // Short position - incentivize buying
        // Quote at bid (easy to hit), avoid offering
        if (inventory_factor > 0.7) {
            return RLAgent::Action::BUY_LIMIT_AGGRESSIVE;
        }
        return RLAgent::Action::BUY_LIMIT_AT_BID;
    }
    
    // === STRATEGY 5: Two-Sided Market Making ===
    // Neutral position - provide liquidity on both sides
    
    // Cancel stale orders periodically to avoid adverse selection
    static int action_counter = 0;
    if (++action_counter % 20 == 0 && !obs.active_orders.empty()) {
        return RLAgent::Action::CANCEL_ALL;
    }
    
    // Spread quality check
    if (spread >= min_spread * 2) {
        // Wide spread - good opportunity for market making
        
        // Use imbalance to choose side when near-neutral
        if (std::abs(position) < 50) {
            // Very neutral - use order book imbalance as signal
            if (imbalance > 0.15) {
                // More bids - likely upward pressure, sell into it
                return RLAgent::Action::SELL_LIMIT_AT_ASK;
            } else if (imbalance < -0.15) {
                // More asks - likely downward pressure, buy into it
                return RLAgent::Action::BUY_LIMIT_AT_BID;
            }
        }
        
        // Slight position - skew toward reducing it
        if (position > 0) {
            // Slightly long - prefer selling
            return RLAgent::Action::SELL_LIMIT_AT_ASK;
        } else if (position < 0) {
            // Slightly short - prefer buying
            return RLAgent::Action::BUY_LIMIT_AT_BID;
        }
        
        // Perfectly neutral - alternate
        return (action_counter % 2 == 0) ? 
               RLAgent::Action::BUY_LIMIT_AT_BID : 
               RLAgent::Action::SELL_LIMIT_AT_ASK;
    }
    
    // === STRATEGY 6: Tight Spread - Use Aggressive Orders ===
    if (spread >= min_spread && spread < min_spread * 2) {
        // Moderate spread - use aggressive quotes to get inside
        if (position >= 0) {
            return RLAgent::Action::SELL_LIMIT_AGGRESSIVE;
        } else {
            return RLAgent::Action::BUY_LIMIT_AGGRESSIVE;
        }
    }
    
    // Default: hold and wait for better opportunity
    return RLAgent::Action::HOLD;
}

double TerminalUI::calculate_volatility() noexcept {
    // Return cached value if still valid
    if (cached_version_ == orderbook_version_ && cached_volatility_ > 0.0) [[likely]] {
        return cached_volatility_;
    }
    
    // O(1) volatility using Welford's online algorithm
    if (volatility_stats_.count() < 2) [[unlikely]] {
        cached_volatility_ = 0.0;
        cached_version_ = orderbook_version_;
        return 0.0;
    }
    
    cached_volatility_ = volatility_stats_.stddev();
    cached_version_ = orderbook_version_;
    return cached_volatility_;
}

double TerminalUI::calculate_order_book_imbalance() noexcept {
    // Return cached value if order book hasn't changed
    if (cached_version_ == orderbook_version_) [[likely]] {
        return cached_imbalance_;
    }
    
    // Get market state which includes top levels
    auto market_state = orderbook_.get_market_state();
    
    Quantity bid_volume = 0;
    Quantity ask_volume = 0;
    
    // Sum up top 5 levels (or all available if less than 5)
    const size_t depth = std::min(size_t(5), std::min(market_state.bid_levels.size(), market_state.ask_levels.size()));
    
    for (size_t i = 0; i < depth && i < market_state.bid_levels.size(); ++i) [[likely]] {
        bid_volume += market_state.bid_levels[i].second;
    }
    
    for (size_t i = 0; i < depth && i < market_state.ask_levels.size(); ++i) [[likely]] {
        ask_volume += market_state.ask_levels[i].second;
    }
    
    Quantity total_volume = bid_volume + ask_volume;
    if (total_volume == 0) [[unlikely]] {
        cached_imbalance_ = 0.0;
        return 0.0;
    }
    
    // Imbalance: +1 = all bids, -1 = all asks, 0 = balanced
    cached_imbalance_ = static_cast<double>(bid_volume - ask_volume) / total_volume;
    return cached_imbalance_;
}

} // namespace orderbook
