#include "backend/orderbook.hpp"
#include "backend/market_data.hpp"
#include "config/config_loader.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <signal.h>

using namespace OrderBookNS;
using namespace orderbook;

volatile bool running = true;

void signal_handler(int) {
    running = false;
}

void print_quote(const Quote& quote) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Symbol: " << quote.symbol << std::endl;
    std::cout << "Bid: $" << std::fixed << std::setprecision(2) 
              << (quote.bid_price / 100.0) << " x " << quote.bid_size << std::endl;
    std::cout << "Ask: $" << std::fixed << std::setprecision(2) 
              << (quote.ask_price / 100.0) << " x " << quote.ask_size << std::endl;
    std::cout << "Spread: $" << std::fixed << std::setprecision(2) 
              << ((quote.ask_price - quote.bid_price) / 100.0) << std::endl;
}

void print_order_book(orderbook::OrderBook& book, const std::string& symbol) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Order Book for " << symbol << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    std::cout << std::setw(15) << "BID SIZE" << " | " 
              << std::setw(10) << "BID" << " | "
              << std::setw(10) << "ASK" << " | "
              << std::setw(15) << "ASK SIZE" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    // Get best bid and ask
    auto best_bid_opt = book.get_best_bid();
    auto best_ask_opt = book.get_best_ask();
    
    if (best_bid_opt && best_ask_opt) {
        Price best_bid = *best_bid_opt;
        Price best_ask = *best_ask_opt;
        
        for (int i = 0; i < 5; ++i) {
            Price bid_price = best_bid - i;
            Price ask_price = best_ask + i;
            
            std::cout << std::setw(15) << "~100" << " | "
                      << std::setw(10) << std::fixed << std::setprecision(2) << (bid_price / 100.0) << " | "
                      << std::setw(10) << std::fixed << std::setprecision(2) << (ask_price / 100.0) << " | "
                      << std::setw(15) << "~100" << std::endl;
        }
    }
    
    std::cout << std::string(60, '=') << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    
    std::cout << "=== Real-Time Order Book with Live Market Data ===" << std::endl;
    std::cout << "Loading configuration..." << std::endl;
    
    // Load configuration
    ConfigLoader config;
    if (!config.load()) {
        std::cout << "Warning: Could not load config.json, using defaults" << std::endl;
        std::cout << "Note: Yahoo Finance will work without API keys" << std::endl;
    }
    
    std::string symbol = config.get_default_symbol();
    if (argc > 1) {
        symbol = argv[1];
    }
    
    std::cout << "Tracking symbol: " << symbol << std::endl;
    
    // Initialize market data providers
    MarketDataAggregator aggregator;
    
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
    std::cout << "\nActive data providers: ";
    for (size_t i = 0; i < providers.size(); ++i) {
        std::cout << providers[i];
        if (i < providers.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;
    
    if (providers.empty()) {
        std::cerr << "\nError: No data providers available!" << std::endl;
        std::cerr << "Please configure API keys in config.json or ensure internet connectivity." << std::endl;
        return 1;
    }
    
    // Initialize order book
    orderbook::OrderBook book;
    
    // Start market data feed
    MarketDataFeed feed(aggregator);
    feed.set_update_interval(config.get_update_interval_ms());
    feed.start(symbol);
    
    std::cout << "\nFetching live market data every " << config.get_update_interval_ms() / 1000 
              << " seconds..." << std::endl;
    std::cout << "Press Ctrl+C to exit\n" << std::endl;
    
    int iteration = 0;
    while (running) {
        Quote quote;
        if (feed.get_latest_quote(quote)) {
            print_quote(quote);
            
            // Simulate order book updates based on market data
            // In a real system, you'd get actual order book data
            book.add_order(
                quote.bid_price,
                quote.bid_size,
                Side::BUY,
                OrderType::LIMIT
            );
            
            book.add_order(
                quote.ask_price,
                quote.ask_size,
                Side::SELL,
                OrderType::LIMIT
            );
            
            // Print order book state
            print_order_book(book, symbol);
            
            // Get and print OHLCV data occasionally
            if (iteration % 3 == 0) {
                std::vector<OHLCV> ohlcv_data;
                if (aggregator.get_ohlcv(symbol, ohlcv_data, "1min", 5)) {
                    std::cout << "\nRecent 1-minute bars:" << std::endl;
                    std::cout << std::string(60, '-') << std::endl;
                    std::cout << std::setw(12) << "OPEN" << " | "
                              << std::setw(12) << "HIGH" << " | "
                              << std::setw(12) << "LOW" << " | "
                              << std::setw(12) << "CLOSE" << std::endl;
                    std::cout << std::string(60, '-') << std::endl;
                    
                    for (const auto& bar : ohlcv_data) {
                        std::cout << std::setw(12) << std::fixed << std::setprecision(2) << (bar.open / 100.0) << " | "
                                  << std::setw(12) << std::fixed << std::setprecision(2) << (bar.high / 100.0) << " | "
                                  << std::setw(12) << std::fixed << std::setprecision(2) << (bar.low / 100.0) << " | "
                                  << std::setw(12) << std::fixed << std::setprecision(2) << (bar.close / 100.0) << std::endl;
                    }
                }
            }
            
            // Print statistics
            auto best_bid_opt = book.get_best_bid();
            auto best_ask_opt = book.get_best_ask();
            
            std::cout << "\nOrder Book Statistics:" << std::endl;
            if (best_bid_opt) {
                std::cout << "Best Bid: $" << std::fixed << std::setprecision(2) 
                          << (*best_bid_opt / 100.0) << std::endl;
            }
            if (best_ask_opt) {
                std::cout << "Best Ask: $" << std::fixed << std::setprecision(2) 
                          << (*best_ask_opt / 100.0) << std::endl;
            }
            if (best_bid_opt && best_ask_opt) {
                std::cout << "Spread: $" << std::fixed << std::setprecision(2) 
                          << ((*best_ask_opt - *best_bid_opt) / 100.0) << std::endl;
            }
        } else {
            std::cout << "Failed to fetch quote for " << symbol << std::endl;
        }
        
        iteration++;
        std::this_thread::sleep_for(std::chrono::milliseconds(config.get_update_interval_ms()));
    }
    
    feed.stop();
    std::cout << "\n\nShutting down..." << std::endl;
    
    return 0;
}
