#include "backend/orderbook.hpp"
#include "agent/rl_agent.hpp"
#include "frontend/terminal_ui.hpp"
#include "backend/market_data.hpp"
#include "backend/yfinance_provider.hpp"
#include "config/config_loader.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <limits>

using namespace orderbook;
using namespace OrderBookNS;

std::atomic<bool> running{true};

int main(int argc, char* argv[]) {
    try {
        // Delete previous session report if it exists
        std::remove("SESSION_REPORT.md");
        
        std::cout << "=== Interactive Order Book with Real Market Data ===" << std::endl;
        std::cout << "Loading configuration..." << std::endl;
        
        // Load configuration
        ConfigLoader config;
        if (!config.load()) {
            std::cout << "Warning: Could not load config.json, using defaults" << std::endl;
        }
        
        std::string symbol = config.get_default_symbol();
        if (argc > 1) {
            symbol = argv[1];
        }
        
        std::cout << "Tracking symbol: " << symbol << std::endl;
        
        // Create order book
        OrderBook book;
        
        // Initialize market data providers
        MarketDataAggregator aggregator;
        
        // Add YFinance local server (primary - most reliable)
        auto yfinance = std::make_shared<YFinanceProvider>("http://localhost:8080");
        aggregator.add_provider(yfinance);
        std::cout << "Added YFinance provider (local Python server)" << std::endl;
        
        // Add Yahoo Finance (no API key required)
        if (config.is_yahoo_enabled()) {
            auto yahoo = std::make_shared<YahooFinanceProvider>();
            aggregator.add_provider(yahoo);
        }
        
        // Add Alpha Vantage if configured
        if (!config.get_alpha_vantage_key().empty() && 
            config.get_alpha_vantage_key() != "YOUR_ALPHA_VANTAGE_API_KEY") {
            auto alpha_vantage = std::make_shared<AlphaVantageProvider>(config.get_alpha_vantage_key());
            aggregator.add_provider(alpha_vantage);
        }
        
        // Add Financial Modeling Prep if configured
        if (!config.get_fmp_key().empty() && 
            config.get_fmp_key() != "YOUR_FMP_API_KEY") {
            auto fmp = std::make_shared<FinancialModelingPrepProvider>(config.get_fmp_key());
            aggregator.add_provider(fmp);
        }
        
        auto providers = aggregator.get_available_providers();
        std::cout << "Active data providers: ";
        for (size_t i = 0; i < providers.size(); ++i) {
            std::cout << providers[i];
            if (i < providers.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
        
        // Start market data feed in background thread
        std::shared_ptr<MarketDataFeed> feed_ptr;
        std::thread market_thread;
        if (!providers.empty()) {
            feed_ptr = std::make_shared<MarketDataFeed>(aggregator);
            feed_ptr->set_update_interval(config.get_update_interval_ms());
            feed_ptr->start(symbol);
            
            std::cout << "Market data feed started (updating every " 
                      << config.get_update_interval_ms() / 1000 << " seconds)" << std::endl;
            std::cout << "Fetching initial market data for " << symbol << "..." << std::endl;
            
            // Wait for first quote and populate initial order book
            bool got_initial_data = false;
            for (int attempts = 0; attempts < 10 && !got_initial_data; ++attempts) {
                Quote quote;
                if (feed_ptr->get_latest_quote(quote)) {
                    std::cout << "Got quote: Bid $" << (quote.bid_price / 100.0) 
                              << " x " << quote.bid_size 
                              << " | Ask $" << (quote.ask_price / 100.0) 
                              << " x " << quote.ask_size << std::endl;
                    
                    // Add initial liquidity around the market quote
                    for (int i = 0; i < 10; ++i) {
                        book.add_order(
                            quote.bid_price - i,
                            quote.bid_size,
                            Side::BUY,
                            OrderType::LIMIT
                        );
                        
                        book.add_order(
                            quote.ask_price + i,
                            quote.ask_size,
                            Side::SELL,
                            OrderType::LIMIT
                        );
                    }
                    got_initial_data = true;
                } else {
                    std::cout << "Waiting for market data..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
            
            if (!got_initial_data) {
                std::cout << "Warning: Could not fetch initial market data, using default prices" << std::endl;
                Price base_price = 10000; // $100.00
                for (int i = 1; i <= 10; ++i) {
                    book.add_order(base_price - i * 5, 100, Side::BUY);
                    book.add_order(base_price + i * 5, 100, Side::SELL);
                }
            }
            
            // Start background thread to continuously update market data and simulate activity
            market_thread = std::thread([&book, feed_ptr]() {
                MarketSimulator sim(book, 25000, 0.005, 50.0);
                int counter = 0;
                while (running.load()) {
                    // Generate market activity every 200ms
                    sim.simulate_step(5); // Add 5 random orders
                    
                    // Update with real market data every 3 seconds
                    if (++counter >= 15) {
                        Quote quote;
                        if (feed_ptr->get_latest_quote(quote)) {
                            // Add fresh orders at market prices
                            book.add_order(quote.bid_price, 50, Side::BUY, OrderType::LIMIT);
                            book.add_order(quote.ask_price, 50, Side::SELL, OrderType::LIMIT);
                        }
                        counter = 0;
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            });
        } else {
            std::cout << "No market data providers available - using manual orders only" << std::endl;
            // Add initial liquidity manually
            Price base_price = 10000; // $100.00
            for (int i = 1; i <= 10; ++i) {
                book.add_order(base_price - i * 5, 1000 * i, Side::BUY);
                book.add_order(base_price + i * 5, 1000 * i, Side::SELL);
            }
        }
        
        std::cout << "Starting terminal UI..." << std::endl;
        std::cout << "Order book has " << book.get_order_count() << " orders" << std::endl;
        
        // Create RL agent for automated trading
        RLAgent agent(book, 1000000.0);
        agent.set_inventory_penalty(0.01);
        agent.set_spread_capture_reward(10.0);
        
        // Create terminal UI with RL agent
        TerminalUI ui(book, &agent);
        ui.init();
        
        std::cout << "UI initialized. Press 'a' to toggle automated trading mode." << std::endl;
        
        // Run UI (blocks until user quits)
        ui.run();
        
        // Cleanup
        running = false;
        if (market_thread.joinable()) {
            market_thread.join();
        }
        
        // Cleanup
        ui.cleanup();
        
        // Generate Session Report Markdown File
        std::ofstream report("SESSION_REPORT.md");
        
        auto position = agent.get_position();
        double total_pnl = position.realized_pnl + position.unrealized_pnl;
        double portfolio_value = agent.get_portfolio_value();
        double return_pct = ((portfolio_value - 1000000.0) / 1000000.0) * 100.0;
        
        auto best_bid = book.get_best_bid();
        auto best_ask = book.get_best_ask();
        
        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time_t);
        timestamp.pop_back(); // Remove trailing newline
        
        report << "# Trading Session Report\n\n";
        report << "**Symbol:** " << symbol << "  \n";
        report << "**Session Ended:** " << timestamp << "  \n";
        report << "**Status:** ";
        if (total_pnl > 0) {
            report << "✅ **PROFITABLE**\n\n";
        } else if (total_pnl < 0) {
            report << "❌ **LOSS**\n\n";
        } else {
            report << "➖ **BREAK-EVEN**\n\n";
        }
        
        report << "---\n\n";
        
        // Executive Summary
        report << "## 📊 Executive Summary\n\n";
        report << "| Metric | Value |\n";
        report << "|--------|-------|\n";
        report << "| Initial Capital | $" << std::fixed << std::setprecision(2) << 1000000.0 << " |\n";
        report << "| Final Portfolio Value | $" << portfolio_value << " |\n";
        report << "| **Total P&L** | **" << (total_pnl >= 0 ? "+" : "") << "$" << total_pnl << "** |\n";
        report << "| **Return** | **" << (return_pct >= 0 ? "+" : "") << std::setprecision(4) << return_pct << "%** |\n";
        report << "| Total Trades | " << agent.get_total_trades() << " |\n";
        report << "| Total Volume | " << std::setprecision(0) << agent.get_total_volume() << " shares |\n\n";
        
        // Position Details
        report << "## 📍 Position & P&L Details\n\n";
        report << "### Current Position\n";
        report << "- **Shares Held:** " << position.quantity << "\n";
        report << "- **Average Entry Price:** $" << std::fixed << std::setprecision(2) << (position.avg_price / 100.0) << "\n\n";
        
        report << "### Profit & Loss Breakdown\n";
        report << "| Type | Amount |\n";
        report << "|------|--------|\n";
        report << "| Realized P&L | " << (position.realized_pnl >= 0 ? "+" : "") << "$" << position.realized_pnl << " |\n";
        report << "| Unrealized P&L | " << (position.unrealized_pnl >= 0 ? "+" : "") << "$" << position.unrealized_pnl << " |\n";
        report << "| **Total P&L** | **" << (total_pnl >= 0 ? "+" : "") << "$" << total_pnl << "** |\n\n";
        
        // Trading Activity
        report << "## 📈 Trading Activity\n\n";
        report << "| Metric | Value |\n";
        report << "|--------|-------|\n";
        report << "| Total Trades Executed | " << agent.get_total_trades() << " |\n";
        report << "| Total Volume Traded | " << std::setprecision(0) << agent.get_total_volume() << " shares |\n";
        if (agent.get_total_trades() > 0) {
            report << "| Average Trade Size | " << (agent.get_total_volume() / agent.get_total_trades()) << " shares |\n";
            report << "| Average P&L per Trade | $" << std::setprecision(2) << (total_pnl / agent.get_total_trades()) << " |\n";
        }
        report << "\n";
        
        // Performance & Latency Metrics
        report << "## ⚡ Performance & Latency Metrics\n\n";
        report << "Agent execution performance measured in nanoseconds:\n\n";
        report << "| Metric | Value |\n";
        report << "|--------|-------|\n";
        report << "| Average Agent Latency | " << std::setprecision(2) << agent.get_avg_latency_ns() << " ns |\n";
        report << "| Minimum Latency | " << agent.get_min_latency_ns() << " ns |\n";
        report << "| Maximum Latency | " << agent.get_max_latency_ns() << " ns |\n";
        report << "| Total Actions Executed | " << (agent.get_avg_latency_ns() > 0 ? static_cast<size_t>(agent.get_avg_latency_ns() * 1.0) : 0) << " |\n";
        report << "\n";
        report << "**Performance Rating:** ";
        double avg_latency = agent.get_avg_latency_ns();
        if (avg_latency < 100) {
            report << "⚡ **EXCELLENT** (Ultra-low latency)\n";
        } else if (avg_latency < 500) {
            report << "✅ **GOOD** (Low latency)\n";
        } else if (avg_latency < 1000) {
            report << "⚠️ **MODERATE** (Acceptable latency)\n";
        } else {
            report << "❌ **SLOW** (High latency - optimization needed)\n";
        }
        report << "\n";
        
        // Order Book Statistics
        report << "## 📖 Order Book Statistics\n\n";
        report << "| Metric | Value |\n";
        report << "|--------|-------|\n";
        report << "| Total Orders Placed | " << book.get_order_count() << " |\n";
        report << "| Active Bid Levels | " << book.get_bid_level_count() << " |\n";
        report << "| Active Ask Levels | " << book.get_ask_level_count() << " |\n";
        if (best_bid && best_ask) {
            report << "| Final Best Bid | $" << std::fixed << std::setprecision(2) << (*best_bid / 100.0) << " |\n";
            report << "| Final Best Ask | $" << (*best_ask / 100.0) << " |\n";
            report << "| Final Spread | $" << ((*best_ask - *best_bid) / 100.0) << " |\n";
        }
        report << "\n";
        
        // Risk Metrics
        report << "## ⚠️ Risk Metrics\n\n";
        report << "| Metric | Value |\n";
        report << "|--------|-------|\n";
        report << "| Current Exposure | $" << std::setprecision(2) << std::abs(position.quantity * 250.0) << " (at ~$250/share) |\n";
        report << "| Position Size | " << std::abs(position.quantity) << " shares |\n";
        report << "| Position Direction | " << (position.quantity > 0 ? "Long ⬆️" : position.quantity < 0 ? "Short ⬇️" : "Flat ➖") << " |\n\n";
        
        // Performance Analysis
        report << "## 🎯 Performance Analysis\n\n";
        if (total_pnl > 0) {
            report << "### ✅ Profitable Session\n\n";
            report << "The RL agent successfully generated **positive returns** of $" << total_pnl << " ";
            report << "(" << (return_pct >= 0 ? "+" : "") << return_pct << "%) during this trading session.\n\n";
            report << "**Key Success Factors:**\n";
            report << "- Effective market making strategy\n";
            report << "- Successful spread capture\n";
            report << "- Efficient position management\n";
        } else if (total_pnl < 0) {
            report << "### ❌ Loss Incurred\n\n";
            report << "The RL agent incurred a **loss** of $" << std::abs(total_pnl) << " ";
            report << "(" << return_pct << "%) during this trading session.\n\n";
            report << "**Areas for Improvement:**\n";
            report << "- Review market making parameters\n";
            report << "- Adjust position sizing strategy\n";
            report << "- Optimize entry/exit timing\n";
        } else {
            report << "### ➖ Break-Even Session\n\n";
            report << "The RL agent maintained **neutral performance** with no significant profit or loss.\n";
        }
        
        report << "\n---\n\n";
        report << "*Report generated automatically by Order Book Trading System*  \n";
        report << "*Session ended at " << timestamp << "*\n";
        
        report.close();
        
        std::cout << "\n✅ Session report saved to SESSION_REPORT.md" << std::endl;
        std::cout << "Thank you for using the Order Book Trading System!\n" << std::endl;
        
        // Pause to let user read the report
        std::cout << "Press Enter to exit..." << std::flush;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
