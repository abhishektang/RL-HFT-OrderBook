#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <map>
#include "order.hpp"

namespace OrderBookNS {

// Use types from orderbook namespace
using orderbook::Price;
using orderbook::Quantity;

// Market data structures
struct Quote {
    std::string symbol;
    Price bid_price;
    Price ask_price;
    Quantity bid_size;
    Quantity ask_size;
    uint64_t timestamp;
    
    Quote() : bid_price(0), ask_price(0), bid_size(0), ask_size(0), timestamp(0) {}
};

struct Trade {
    std::string symbol;
    Price price;
    Quantity quantity;
    uint64_t timestamp;
    
    Trade() : price(0), quantity(0), timestamp(0) {}
};

struct OHLCV {
    std::string symbol;
    uint64_t timestamp;
    Price open;
    Price high;
    Price low;
    Price close;
    Quantity volume;
    
    OHLCV() : timestamp(0), open(0), high(0), low(0), close(0), volume(0) {}
};

// Data provider interface
class IMarketDataProvider {
public:
    virtual ~IMarketDataProvider() = default;
    
    // Get real-time quote
    virtual bool get_quote(const std::string& symbol, Quote& quote) = 0;
    
    // Get recent trades
    virtual bool get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit = 100) = 0;
    
    // Get historical OHLCV data
    virtual bool get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                          const std::string& interval = "1min", int limit = 100) = 0;
    
    // Get order book snapshot (if available)
    virtual bool get_order_book_snapshot(const std::string& symbol, 
                                        std::vector<std::pair<Price, Quantity>>& bids,
                                        std::vector<std::pair<Price, Quantity>>& asks) {
        return false; // Not all providers support this
    }
    
    // Check if provider is available
    virtual bool is_available() const = 0;
    
    // Get provider name
    virtual std::string get_name() const = 0;
};

// HTTP Client for making API requests
class HTTPClient {
public:
    HTTPClient();
    ~HTTPClient();
    
    // Perform GET request
    bool get(const std::string& url, std::string& response, 
             const std::map<std::string, std::string>& headers = {});
    
    // Set timeout in seconds
    void set_timeout(long timeout_seconds);
    
private:
    void* curl_handle_; // CURL*
    long timeout_;
};

// Yahoo Finance provider
class YahooFinanceProvider : public IMarketDataProvider {
public:
    YahooFinanceProvider();
    ~YahooFinanceProvider() override = default;
    
    bool get_quote(const std::string& symbol, Quote& quote) override;
    bool get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit = 100) override;
    bool get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                   const std::string& interval = "1min", int limit = 100) override;
    bool is_available() const override;
    std::string get_name() const override { return "Yahoo Finance"; }
    
private:
    HTTPClient http_client_;
    std::string base_url_;
};

// Alpha Vantage provider
class AlphaVantageProvider : public IMarketDataProvider {
public:
    explicit AlphaVantageProvider(const std::string& api_key);
    ~AlphaVantageProvider() override = default;
    
    bool get_quote(const std::string& symbol, Quote& quote) override;
    bool get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit = 100) override;
    bool get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                   const std::string& interval = "1min", int limit = 100) override;
    bool is_available() const override;
    std::string get_name() const override { return "Alpha Vantage"; }
    
private:
    HTTPClient http_client_;
    std::string api_key_;
    std::string base_url_;
    std::chrono::steady_clock::time_point last_request_time_;
    
    void rate_limit(); // Alpha Vantage has rate limits
};

// Financial Modeling Prep provider
class FinancialModelingPrepProvider : public IMarketDataProvider {
public:
    explicit FinancialModelingPrepProvider(const std::string& api_key);
    ~FinancialModelingPrepProvider() override = default;
    
    bool get_quote(const std::string& symbol, Quote& quote) override;
    bool get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit = 100) override;
    bool get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                   const std::string& interval = "1min", int limit = 100) override;
    bool is_available() const override;
    std::string get_name() const override { return "Financial Modeling Prep"; }
    
private:
    HTTPClient http_client_;
    std::string api_key_;
    std::string base_url_;
};

// Market data aggregator - tries multiple providers
class MarketDataAggregator {
public:
    MarketDataAggregator();
    ~MarketDataAggregator() = default;
    
    // Add a data provider
    void add_provider(std::shared_ptr<IMarketDataProvider> provider);
    
    // Get quote from first available provider
    bool get_quote(const std::string& symbol, Quote& quote);
    
    // Get trades from first available provider
    bool get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit = 100);
    
    // Get OHLCV from first available provider
    bool get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                   const std::string& interval = "1min", int limit = 100);
    
    // Get list of available providers
    std::vector<std::string> get_available_providers() const;
    
private:
    std::vector<std::shared_ptr<IMarketDataProvider>> providers_;
};

// Market data feed that converts real data into order book orders
class MarketDataFeed {
public:
    explicit MarketDataFeed(MarketDataAggregator& aggregator);
    ~MarketDataFeed() = default;
    
    // Start streaming market data for a symbol
    void start(const std::string& symbol);
    
    // Stop streaming
    void stop();
    
    // Check if feed is running
    bool is_running() const { return running_; }
    
    // Get latest quote
    bool get_latest_quote(Quote& quote);
    
    // Set callback for new quotes
    void set_quote_callback(std::function<void(const Quote&)> callback);
    
    // Set callback for new trades
    void set_trade_callback(std::function<void(const Trade&)> callback);
    
    // Update interval in milliseconds
    void set_update_interval(int milliseconds) { update_interval_ms_ = milliseconds; }
    
private:
    MarketDataAggregator& aggregator_;
    std::string symbol_;
    bool running_;
    int update_interval_ms_;
    Quote latest_quote_;
    
    std::function<void(const Quote&)> quote_callback_;
    std::function<void(const Trade&)> trade_callback_;
};

} // namespace OrderBookNS
