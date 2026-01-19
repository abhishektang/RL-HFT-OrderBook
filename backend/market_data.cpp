#include "market_data.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstring>
#include <json/json.h>

namespace OrderBookNS {

// ============================================================================
// HTTPClient Implementation
// ============================================================================

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

HTTPClient::HTTPClient() : timeout_(10) {
    curl_handle_ = curl_easy_init();
}

HTTPClient::~HTTPClient() {
    if (curl_handle_) {
        curl_easy_cleanup((CURL*)curl_handle_);
    }
}

bool HTTPClient::get(const std::string& url, std::string& response, 
                     const std::map<std::string, std::string>& headers) {
    if (!curl_handle_) return false;
    
    CURL* curl = (CURL*)curl_handle_;
    response.clear();
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Set headers if provided
    struct curl_slist* header_list = nullptr;
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    if (header_list) {
        curl_slist_free_all(header_list);
    }
    
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    return http_code == 200;
}

void HTTPClient::set_timeout(long timeout_seconds) {
    timeout_ = timeout_seconds;
}

// ============================================================================
// Yahoo Finance Provider Implementation
// ============================================================================

YahooFinanceProvider::YahooFinanceProvider() 
    : base_url_("https://query1.finance.yahoo.com/v8/finance") {
}

bool YahooFinanceProvider::is_available() const {
    return true; // Yahoo Finance doesn't require API key
}

bool YahooFinanceProvider::get_quote(const std::string& symbol, Quote& quote) {
    std::string url = base_url_ + "/chart/" + symbol + "?interval=1m&range=1d";
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        auto chart = root["chart"]["result"][0];
        auto meta = chart["meta"];
        auto indicators = chart["indicators"]["quote"][0];
        
        // Get current price
        Price current_price = static_cast<Price>(meta["regularMarketPrice"].asDouble() * 100);
        
        quote.symbol = symbol;
        quote.bid_price = current_price - 1; // Approximate bid
        quote.ask_price = current_price + 1; // Approximate ask
        quote.bid_size = 100;
        quote.ask_size = 100;
        quote.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Yahoo Finance parse error: " << e.what() << std::endl;
        return false;
    }
}

bool YahooFinanceProvider::get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit) {
    // Yahoo Finance doesn't provide individual trade data easily
    // We'll use price history as a proxy
    std::string url = base_url_ + "/chart/" + symbol + "?interval=1m&range=1d";
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        auto chart = root["chart"]["result"][0];
        auto timestamps = chart["timestamp"];
        auto indicators = chart["indicators"]["quote"][0];
        auto closes = indicators["close"];
        auto volumes = indicators["volume"];
        
        trades.clear();
        int count = std::min(limit, static_cast<int>(timestamps.size()));
        
        for (int i = timestamps.size() - count; i < static_cast<int>(timestamps.size()); ++i) {
            Trade trade;
            trade.symbol = symbol;
            trade.price = static_cast<Price>(closes[i].asDouble() * 100);
            trade.quantity = volumes[i].asUInt64() / 100; // Approximate
            trade.timestamp = timestamps[i].asUInt64() * 1000000000ULL; // Convert to nanoseconds
            trades.push_back(trade);
        }
        
        return !trades.empty();
    } catch (const std::exception& e) {
        std::cerr << "Yahoo Finance parse error: " << e.what() << std::endl;
        return false;
    }
}

bool YahooFinanceProvider::get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                                     const std::string& interval, int limit) {
    std::string url = base_url_ + "/chart/" + symbol + "?interval=" + interval + "&range=1d";
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        auto chart = root["chart"]["result"][0];
        auto timestamps = chart["timestamp"];
        auto indicators = chart["indicators"]["quote"][0];
        
        data.clear();
        int count = std::min(limit, static_cast<int>(timestamps.size()));
        
        for (int i = timestamps.size() - count; i < static_cast<int>(timestamps.size()); ++i) {
            OHLCV bar;
            bar.symbol = symbol;
            bar.timestamp = timestamps[i].asUInt64() * 1000000000ULL;
            bar.open = static_cast<Price>(indicators["open"][i].asDouble() * 100);
            bar.high = static_cast<Price>(indicators["high"][i].asDouble() * 100);
            bar.low = static_cast<Price>(indicators["low"][i].asDouble() * 100);
            bar.close = static_cast<Price>(indicators["close"][i].asDouble() * 100);
            bar.volume = indicators["volume"][i].asUInt64();
            data.push_back(bar);
        }
        
        return !data.empty();
    } catch (const std::exception& e) {
        std::cerr << "Yahoo Finance parse error: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Alpha Vantage Provider Implementation
// ============================================================================

AlphaVantageProvider::AlphaVantageProvider(const std::string& api_key)
    : api_key_(api_key), base_url_("https://www.alphavantage.co/query") {
    last_request_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(60);
}

bool AlphaVantageProvider::is_available() const {
    return !api_key_.empty();
}

void AlphaVantageProvider::rate_limit() {
    // Alpha Vantage free tier: 5 requests per minute
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time_);
    
    if (elapsed.count() < 12000) { // 12 seconds between requests
        std::this_thread::sleep_for(std::chrono::milliseconds(12000 - elapsed.count()));
    }
    
    last_request_time_ = std::chrono::steady_clock::now();
}

bool AlphaVantageProvider::get_quote(const std::string& symbol, Quote& quote) {
    rate_limit();
    
    std::string url = base_url_ + "?function=GLOBAL_QUOTE&symbol=" + symbol + "&apikey=" + api_key_;
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        auto global_quote = root["Global Quote"];
        if (global_quote.isNull()) {
            return false;
        }
        
        Price price = static_cast<Price>(std::stod(global_quote["05. price"].asString()) * 100);
        
        quote.symbol = symbol;
        quote.bid_price = price - 1;
        quote.ask_price = price + 1;
        quote.bid_size = 100;
        quote.ask_size = 100;
        quote.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Alpha Vantage parse error: " << e.what() << std::endl;
        return false;
    }
}

bool AlphaVantageProvider::get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit) {
    // Alpha Vantage doesn't provide tick-by-tick trade data in free tier
    return false;
}

bool AlphaVantageProvider::get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                                     const std::string& interval, int limit) {
    rate_limit();
    
    std::string url = base_url_ + "?function=TIME_SERIES_INTRADAY&symbol=" + symbol + 
                     "&interval=" + interval + "&apikey=" + api_key_;
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        std::string time_series_key = "Time Series (" + interval + ")";
        auto time_series = root[time_series_key];
        
        if (time_series.isNull()) {
            return false;
        }
        
        data.clear();
        int count = 0;
        
        for (auto it = time_series.begin(); it != time_series.end() && count < limit; ++it, ++count) {
            OHLCV bar;
            bar.symbol = symbol;
            bar.open = static_cast<Price>(std::stod((*it)["1. open"].asString()) * 100);
            bar.high = static_cast<Price>(std::stod((*it)["2. high"].asString()) * 100);
            bar.low = static_cast<Price>(std::stod((*it)["3. low"].asString()) * 100);
            bar.close = static_cast<Price>(std::stod((*it)["4. close"].asString()) * 100);
            bar.volume = std::stoull((*it)["5. volume"].asString());
            data.push_back(bar);
        }
        
        return !data.empty();
    } catch (const std::exception& e) {
        std::cerr << "Alpha Vantage parse error: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Financial Modeling Prep Provider Implementation
// ============================================================================

FinancialModelingPrepProvider::FinancialModelingPrepProvider(const std::string& api_key)
    : api_key_(api_key), base_url_("https://financialmodelingprep.com/stable") {
}

bool FinancialModelingPrepProvider::is_available() const {
    return !api_key_.empty();
}

bool FinancialModelingPrepProvider::get_quote(const std::string& symbol, Quote& quote) {
    std::string url = base_url_ + "/quote?symbol=" + symbol + "&apikey=" + api_key_;
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        if (root.isArray() && root.size() > 0) {
            auto quote_data = root[0];
            
            Price price = static_cast<Price>(quote_data["price"].asDouble() * 100);
            
            quote.symbol = symbol;
            quote.bid_price = price - 1;
            quote.ask_price = price + 1;
            quote.bid_size = 100;
            quote.ask_size = 100;
            quote.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            return true;
        }
        
        return false;
    } catch (const std::exception& e) {
        std::cerr << "FMP parse error: " << e.what() << std::endl;
        return false;
    }
}

bool FinancialModelingPrepProvider::get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit) {
    // FMP doesn't provide tick-by-tick trade data
    return false;
}

bool FinancialModelingPrepProvider::get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                                              const std::string& interval, int limit) {
    // FMP uses different interval format: 1min, 5min, 15min, 30min, 1hour, 4hour
    std::string url = base_url_ + "/historical-chart/" + interval + "/" + symbol + "?apikey=" + api_key_;
    std::string response;
    
    if (!http_client_.get(url, response)) {
        return false;
    }
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            return false;
        }
        
        data.clear();
        int count = std::min(limit, static_cast<int>(root.size()));
        
        for (int i = 0; i < count; ++i) {
            auto bar_data = root[i];
            OHLCV bar;
            bar.symbol = symbol;
            bar.open = static_cast<Price>(bar_data["open"].asDouble() * 100);
            bar.high = static_cast<Price>(bar_data["high"].asDouble() * 100);
            bar.low = static_cast<Price>(bar_data["low"].asDouble() * 100);
            bar.close = static_cast<Price>(bar_data["close"].asDouble() * 100);
            bar.volume = bar_data["volume"].asUInt64();
            data.push_back(bar);
        }
        
        return !data.empty();
    } catch (const std::exception& e) {
        std::cerr << "FMP parse error: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Market Data Aggregator Implementation
// ============================================================================

MarketDataAggregator::MarketDataAggregator() {
}

void MarketDataAggregator::add_provider(std::shared_ptr<IMarketDataProvider> provider) {
    if (provider && provider->is_available()) {
        providers_.push_back(provider);
        std::cout << "Added market data provider: " << provider->get_name() << std::endl;
    }
}

bool MarketDataAggregator::get_quote(const std::string& symbol, Quote& quote) {
    for (auto& provider : providers_) {
        if (provider->get_quote(symbol, quote)) {
            return true;
        }
    }
    return false;
}

bool MarketDataAggregator::get_trades(const std::string& symbol, std::vector<Trade>& trades, int limit) {
    for (auto& provider : providers_) {
        if (provider->get_trades(symbol, trades, limit)) {
            return true;
        }
    }
    return false;
}

bool MarketDataAggregator::get_ohlcv(const std::string& symbol, std::vector<OHLCV>& data, 
                                     const std::string& interval, int limit) {
    for (auto& provider : providers_) {
        if (provider->get_ohlcv(symbol, data, interval, limit)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> MarketDataAggregator::get_available_providers() const {
    std::vector<std::string> names;
    for (const auto& provider : providers_) {
        if (provider->is_available()) {
            names.push_back(provider->get_name());
        }
    }
    return names;
}

// ============================================================================
// Market Data Feed Implementation
// ============================================================================

MarketDataFeed::MarketDataFeed(MarketDataAggregator& aggregator)
    : aggregator_(aggregator), running_(false), update_interval_ms_(1000) {
}

void MarketDataFeed::start(const std::string& symbol) {
    symbol_ = symbol;
    running_ = true;
}

void MarketDataFeed::stop() {
    running_ = false;
}

bool MarketDataFeed::get_latest_quote(Quote& quote) {
    if (!running_) return false;
    
    if (aggregator_.get_quote(symbol_, latest_quote_)) {
        quote = latest_quote_;
        
        if (quote_callback_) {
            quote_callback_(quote);
        }
        
        return true;
    }
    
    return false;
}

void MarketDataFeed::set_quote_callback(std::function<void(const Quote&)> callback) {
    quote_callback_ = callback;
}

void MarketDataFeed::set_trade_callback(std::function<void(const Trade&)> callback) {
    trade_callback_ = callback;
}

} // namespace OrderBookNS
