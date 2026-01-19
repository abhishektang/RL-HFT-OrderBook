# Project Structure

This project is organized into logical directories for better maintainability and clarity.

## Directory Layout

```
OrderBook/
├── backend/              # Core order book engine and market data
│   ├── orderbook.hpp     # Main order book implementation
│   ├── orderbook.cpp
│   ├── order.hpp         # Order data structures
│   ├── price_level.hpp   # Price level management
│   ├── memory_pool.hpp   # Memory pool allocator
│   ├── market_data.hpp   # Market data providers interface
│   ├── market_data.cpp
│   └── yfinance_provider.hpp  # YFinance data provider
│
├── frontend/             # User interface components
│   ├── terminal_ui.hpp   # ncurses terminal UI
│   └── terminal_ui.cpp
│
├── agent/                # Reinforcement learning trading agent
│   ├── rl_agent.hpp      # RL agent interface
│   ├── rl_agent.cpp
│   └── deep_rl.hpp       # Deep RL extensions
│
├── server/               # Python market data server
│   └── yfinance_server.py  # HTTP server for real-time quotes
│
├── config/               # Configuration files
│   ├── config.json       # API keys and settings
│   └── config_loader.hpp # Configuration loader
│
├── main.cpp              # Demo executable (basic order book)
├── main_ui.cpp           # Interactive UI executable
├── main_market_data.cpp  # Market data feed executable
├── Makefile              # Build system
│
├── README.md             # Main documentation
├── ARCHITECTURE.md       # Architecture overview
└── Documentation files   # Various guides

```

## Component Responsibilities

### Backend (`backend/`)
- **Order Book Engine**: Ultra-low latency matching engine (50-200ns per operation)
- **Market Data**: Real-time quotes, trades, and OHLCV data from multiple providers
- **Core Types**: Orders, price levels, memory management

### Frontend (`frontend/`)
- **Terminal UI**: Interactive ncurses interface with 5 windows
- **Display**: Order book visualization, trade history, statistics
- **Input**: Command parsing for manual order entry

### Agent (`agent/`)
- **RL Agent**: Deep Q-Network (DQN) based trading agent
- **Strategy**: Market making, position management, risk control
- **Features**: 34-dimensional observation space, 8 action types

### Server (`server/`)
- **YFinance Server**: Python HTTP server providing real-time stock quotes
- **Endpoints**: `/quote?symbol=X`, `/health`
- **Caching**: 2-second quote cache to reduce API calls

### Config (`config/`)
- **Configuration**: API keys, provider settings, defaults
- **Loader**: JSON parser for runtime configuration

## Build Targets

```bash
make            # Build basic demo (orderbook)
make ui         # Build interactive UI (orderbook_ui)
make market     # Build market data feed (orderbook_market)
make clean      # Clean all build artifacts
make debug      # Debug build with symbols
make profile    # Profiling build with -pg
```

## Running the Applications

### Interactive UI with Real Market Data
```bash
# Start the yfinance server
python3 server/yfinance_server.py 8080 &

# Run the UI
./orderbook_ui AAPL
```

### Commands
- `buy limit <qty> <price>` - Place limit buy order
- `sell market <qty>` - Place market sell order
- `a` - Toggle automated RL agent mode
- `h` - Show help menu
- `q` - Quit

## Development Workflow

1. **Backend changes**: Modify order book engine or market data providers
2. **Frontend changes**: Update terminal UI or add new visualizations
3. **Agent changes**: Improve RL strategy or add new actions
4. **Rebuild**: `make clean && make ui`
5. **Test**: Run with real or simulated data

## Include Paths

The Makefile sets up include directories so you can use:
- `#include "orderbook.hpp"` from within `backend/`
- `#include "../backend/orderbook.hpp"` from `agent/` or `frontend/`
- Main files use full paths: `#include "backend/orderbook.hpp"`

## Future Enhancements

- Add `strategies/` for different trading algorithms
- Add `tests/` for unit and integration tests
- Add `tools/` for analysis and visualization scripts
- Add `docs/` for comprehensive API documentation
