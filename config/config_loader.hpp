#pragma once

#include <string>
#include <map>
#include <fstream>
#include <json/json.h>

namespace OrderBookNS {

class ConfigLoader {
public:
    ConfigLoader() : loaded_(false) {}
    
    bool load(const std::string& config_file = "config/config.json") {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            return false;
        }
        
        Json::Value root;
        Json::Reader reader;
        
        if (!reader.parse(file, root)) {
            return false;
        }
        
        // Load Alpha Vantage config
        if (root["market_data"]["providers"]["alpha_vantage"]["enabled"].asBool()) {
            alpha_vantage_key_ = root["market_data"]["providers"]["alpha_vantage"]["api_key"].asString();
        }
        
        // Load FMP config
        if (root["market_data"]["providers"]["financial_modeling_prep"]["enabled"].asBool()) {
            fmp_key_ = root["market_data"]["providers"]["financial_modeling_prep"]["api_key"].asString();
        }
        
        // Load Yahoo Finance config
        yahoo_enabled_ = root["market_data"]["providers"]["yahoo_finance"]["enabled"].asBool();
        
        // Load other settings
        default_symbol_ = root["market_data"]["default_symbol"].asString();
        if (default_symbol_.empty()) {
            default_symbol_ = "AAPL";
        }
        
        update_interval_ms_ = root["market_data"]["update_interval_ms"].asInt();
        if (update_interval_ms_ <= 0) {
            update_interval_ms_ = 5000;
        }
        
        timeout_seconds_ = root["market_data"]["timeout_seconds"].asInt();
        if (timeout_seconds_ <= 0) {
            timeout_seconds_ = 10;
        }
        
        loaded_ = true;
        return true;
    }
    
    bool is_loaded() const { return loaded_; }
    
    const std::string& get_alpha_vantage_key() const { return alpha_vantage_key_; }
    const std::string& get_fmp_key() const { return fmp_key_; }
    bool is_yahoo_enabled() const { return yahoo_enabled_; }
    const std::string& get_default_symbol() const { return default_symbol_; }
    int get_update_interval_ms() const { return update_interval_ms_; }
    int get_timeout_seconds() const { return timeout_seconds_; }
    
private:
    bool loaded_;
    std::string alpha_vantage_key_;
    std::string fmp_key_;
    bool yahoo_enabled_;
    std::string default_symbol_;
    int update_interval_ms_;
    int timeout_seconds_;
};

} // namespace OrderBookNS
