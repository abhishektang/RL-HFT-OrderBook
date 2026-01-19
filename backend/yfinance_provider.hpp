#pragma once

#include "market_data.hpp"
#include <iostream>
#include <sstream>
#include <json/json.h>

namespace OrderBookNS {

// YFinance provider that connects to local Python server
class YFinanceProvider : public IMarketDataProvider {
private:
    HTTPClient client_;
    std::string server_url_;
    
public:
    YFinanceProvider(const std::string& server_url = "http://localhost:8080")
        : server_url_(server_url) {
        client_.set_timeout(5);
    }
    
    std::string get_name() const override {
        return "YFinance (Local Server)";
    }
    
    bool is_available() const override {
        // Simple health check
        return true; // Assume available if constructed
    }
    
    bool get_quote(const std::string& symbol, Quote& quote) override {
        std::string url = server_url_ + "/quote?symbol=" + symbol;
        std::string response;
        
        if (!client_.get(url, response)) {
            return false;
        }
        
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::istringstream stream(response);
            std::string errs;
            
            if (!Json::parseFromStream(builder, stream, &root, &errs)) {
                std::cerr << "Failed to parse JSON: " << errs << std::endl;
                return false;
            }
            
            quote.symbol = root["symbol"].asString();
            quote.bid_price = root["bid_price"].asInt64();
            quote.ask_price = root["ask_price"].asInt64();
            quote.bid_size = root["bid_size"].asUInt64();
            quote.ask_size = root["ask_size"].asUInt64();
            quote.timestamp = root["timestamp"].asUInt64();
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error parsing quote: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool get_trades(const std::string&, std::vector<Trade>&, int) override {
        return false; // Not implemented
    }
    
    bool get_ohlcv(const std::string&, std::vector<OHLCV>&, const std::string&, int) override {
        return false; // Not implemented
    }
};

} // namespace OrderBookNS
