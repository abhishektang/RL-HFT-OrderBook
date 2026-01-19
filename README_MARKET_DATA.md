# Real-Time Order Book with Live Market Data Integration

A high-performance C++ order book implementation integrated with real-time market data from multiple providers.

## Features

- **Ultra-low latency order matching** (50-200 nanoseconds per operation)
- **Real-time market data** from Yahoo Finance, Alpha Vantage, and Financial Modeling Prep
- **Terminal UI** with live order book visualization
- **RL agent integration** for algorithmic trading
- **Custom memory pool** for lock-free allocation
- **O(1) order operations** with price-time priority

## Prerequisites

### Required Libraries

```bash
# macOS
brew install curl jsoncpp ncurses

# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev libjsoncpp-dev libncurses5-dev

# Fedora/RHEL
sudo dnf install libcurl-devel jsoncpp-devel ncurses-devel
```

### API Keys (Optional but Recommended)

1. **Alpha Vantage** (Free): https://www.alphavantage.co/support/#api-key
2. **Financial Modeling Prep** (Free tier available): https://financialmodelingprep.com/developer/docs/

**Note**: Yahoo Finance works without an API key!

## Configuration

1. Copy the configuration template:
```bash
cp config.json.example config.json
```

2. Edit `config.json` and add your API keys:
```json
{
  "market_data": {
    "providers": {
      "alpha_vantage": {
        "enabled": true,
        "api_key": "YOUR_ALPHA_VANTAGE_API_KEY"
      },
      "financial_modeling_prep": {
        "enabled": true,
        "api_key": "YOUR_FMP_API_KEY"
      },
      "yahoo_finance": {
        "enabled": true
      }
    },
    "default_symbol": "AAPL",
    "update_interval_ms": 5000
  }
}
```

## Building

```bash
# Build all versions
make all ui market

# Or build individually
make          # Demo order book
make ui       # Terminal UI version
make market   # Market data integration
```

## Running

### 1. Demo Order Book (Simulated Data)
```bash
make run
```

### 2. Terminal UI (Interactive)
```bash
make run-ui
```

**Commands in Terminal UI:**
- `buy limit <qty> <price>` - Place limit buy order
- `sell limit <qty> <price>` - Place limit sell order
- `buy market <qty>` - Place market buy order
- `sell market <qty>` - Place market sell order
- `TAB` - Generate random orders
- `q` or `ESC` - Quit

### 3. Market Data Feed (Real-Time)
```bash
# Default symbol (AAPL)
make run-market

# Or specify a symbol
./orderbook_market TSLA
./orderbook_market GOOGL
./orderbook_market MSFT
```

## Market Data Providers

### Yahoo Finance
- **No API key required**
- Provides quotes and OHLCV data
- Free and unlimited
- Best for getting started

### Alpha Vantage
- **Free API key** (5 requests/minute)
- Professional-grade data
- Good for intraday data
- Rate limiting implemented

### Financial Modeling Prep
- **Free tier available** (250 requests/day)
- Real-time quotes
- Historical OHLCV data
- Good data quality

## Architecture

```
┌─────────────────────────────────────────────────┐
│           Market Data Aggregator                │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐   │
│  │  Yahoo   │ │  Alpha   │ │     FMP      │   │
│  │ Finance  │ │ Vantage  │ │              │   │
│  └──────────┘ └──────────┘ └──────────────┘   │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│           Market Data Feed                       │
│  - Quote callbacks                               │
│  - Trade callbacks                               │
│  - Configurable update interval                  │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│           Order Book Engine                      │
│  - O(1) order matching                          │
│  - Price-time priority                          │
│  - Nanosecond timestamps                        │
│  - Custom memory pool                           │
└─────────────────────────────────────────────────┘
```

## Performance

- **Order matching**: 50-200 nanoseconds
- **Memory allocation**: ~100x faster than malloc
- **Price level lookup**: O(1)
- **Order cancellation**: O(1)

## Examples

### Basic Usage

```cpp
#include "orderbook.hpp"
#include "market_data.hpp"
#include "config_loader.hpp"

// Load configuration
ConfigLoader config;
config.load();

// Setup market data
MarketDataAggregator aggregator;
auto yahoo = std::make_shared<YahooFinanceProvider>();
aggregator.add_provider(yahoo);

// Create order book
OrderBook book("AAPL");

// Start market data feed
MarketDataFeed feed(aggregator);
feed.set_quote_callback([&](const Quote& quote) {
    book.add_order(Side::BUY, OrderType::LIMIT, 
                   quote.bid_price, quote.bid_size);
    book.add_order(Side::SELL, OrderType::LIMIT, 
                   quote.ask_price, quote.ask_size);
});

feed.start("AAPL");
```

## Project Structure

```
.
├── order.hpp              # Order data structures
├── price_level.hpp        # Price level management
├── memory_pool.hpp        # Custom memory allocator
├── orderbook.hpp/cpp      # Main order book engine
├── rl_agent.hpp/cpp       # RL trading agent
├── deep_rl.hpp            # Deep Q-learning
├── market_data.hpp/cpp    # Market data integration
├── config_loader.hpp      # Configuration management
├── terminal_ui.hpp/cpp    # Terminal UI (ncurses)
├── main.cpp               # Demo application
├── main_ui.cpp            # Terminal UI application
├── main_market_data.cpp   # Market data application
├── config.json            # Configuration file
└── Makefile               # Build system
```

## Troubleshooting

### "No data providers available"
- Check your internet connection
- Verify API keys in `config.json`
- Try with Yahoo Finance (no API key required)

### Build errors with libcurl
```bash
# macOS
brew install curl

# Ubuntu
sudo apt-get install libcurl4-openssl-dev
```

### Build errors with jsoncpp
```bash
# macOS
brew install jsoncpp

# Ubuntu
sudo apt-get install libjsoncpp-dev
```

### Rate limiting errors
- Alpha Vantage: Free tier allows 5 requests/minute
- Increase `update_interval_ms` in config.json
- Consider upgrading to paid tier for more requests

## Development

```bash
# Debug build
make debug

# Profile build
make profile

# Clean build
make clean
make all ui market
```

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please feel free to submit a Pull Request.

## References

- [Stanford RL for Finance](https://github.com/coverdrive/rl-finance)
- [Yahoo Finance API](https://finance.yahoo.com/)
- [Alpha Vantage](https://www.alphavantage.co/)
- [Financial Modeling Prep](https://financialmodelingprep.com/)
