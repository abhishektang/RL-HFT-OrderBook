CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -march=native -mtune=native -flto -I/opt/homebrew/include -Ibackend -Ifrontend -Iagent -Iconfig
LDFLAGS = -pthread
UI_LDFLAGS = -pthread -lncurses
MARKET_LDFLAGS = -pthread -L/opt/homebrew/lib -lcurl -ljsoncpp

# Debug flags
DEBUG_FLAGS = -g -O0 -DDEBUG

# Source files with new paths
BACKEND_DIR = backend
FRONTEND_DIR = frontend
AGENT_DIR = agent
CONFIG_DIR = config

SOURCES = $(BACKEND_DIR)/orderbook.cpp $(AGENT_DIR)/rl_agent.cpp main.cpp
UI_SOURCES = $(BACKEND_DIR)/orderbook.cpp $(AGENT_DIR)/rl_agent.cpp $(FRONTEND_DIR)/terminal_ui.cpp $(BACKEND_DIR)/market_data.cpp main_ui.cpp
MARKET_SOURCES = $(BACKEND_DIR)/orderbook.cpp $(AGENT_DIR)/rl_agent.cpp $(BACKEND_DIR)/market_data.cpp main_market_data.cpp
OBJECTS = $(SOURCES:.cpp=.o)
UI_OBJECTS = $(UI_SOURCES:.cpp=.o)
MARKET_OBJECTS = $(MARKET_SOURCES:.cpp=.o)
TARGET = orderbook
UI_TARGET = orderbook_ui
MARKET_TARGET = orderbook_market

# Profiling build
PROFILE_FLAGS = -pg -O2

.PHONY: all clean debug profile benchmark ui market

all: $(TARGET)

ui: $(UI_TARGET)

market: $(MARKET_TARGET)

$(TARGET): $(BACKEND_DIR)/orderbook.o $(AGENT_DIR)/rl_agent.o main.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(UI_TARGET): $(BACKEND_DIR)/orderbook.o $(AGENT_DIR)/rl_agent.o $(FRONTEND_DIR)/terminal_ui.o $(BACKEND_DIR)/market_data.o main_ui.o
	$(CXX) $(CXXFLAGS) $(UI_LDFLAGS) $(MARKET_LDFLAGS) -o $@ $^

$(MARKET_TARGET): $(BACKEND_DIR)/orderbook.o $(AGENT_DIR)/rl_agent.o $(BACKEND_DIR)/market_data.o main_market_data.o
	$(CXX) $(CXXFLAGS) $(MARKET_LDFLAGS) -o $@ $^

# Object files
$(BACKEND_DIR)/orderbook.o: $(BACKEND_DIR)/orderbook.cpp $(BACKEND_DIR)/orderbook.hpp $(BACKEND_DIR)/order.hpp $(BACKEND_DIR)/price_level.hpp $(BACKEND_DIR)/memory_pool.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(AGENT_DIR)/rl_agent.o: $(AGENT_DIR)/rl_agent.cpp $(AGENT_DIR)/rl_agent.hpp $(BACKEND_DIR)/orderbook.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

main.o: main.cpp $(BACKEND_DIR)/orderbook.hpp $(AGENT_DIR)/rl_agent.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(FRONTEND_DIR)/terminal_ui.o: $(FRONTEND_DIR)/terminal_ui.cpp $(FRONTEND_DIR)/terminal_ui.hpp $(BACKEND_DIR)/orderbook.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

main_ui.o: main_ui.cpp $(BACKEND_DIR)/orderbook.hpp $(AGENT_DIR)/rl_agent.hpp $(FRONTEND_DIR)/terminal_ui.hpp $(BACKEND_DIR)/market_data.hpp $(CONFIG_DIR)/config_loader.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BACKEND_DIR)/market_data.o: $(BACKEND_DIR)/market_data.cpp $(BACKEND_DIR)/market_data.hpp $(BACKEND_DIR)/order.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

main_market_data.o: main_market_data.cpp $(BACKEND_DIR)/orderbook.hpp $(BACKEND_DIR)/market_data.hpp $(CONFIG_DIR)/config_loader.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS = -std=c++17 $(DEBUG_FLAGS) -Wall -Wextra
debug: clean $(TARGET)

profile: CXXFLAGS = -std=c++17 $(PROFILE_FLAGS) -Wall -Wextra
profile: clean $(TARGET)

benchmark: $(TARGET)
	./$(TARGET)

run: $(TARGET)
	./$(TARGET)

run-ui: $(UI_TARGET)
	./$(UI_TARGET)

clean:
	rm -f $(BACKEND_DIR)/*.o $(AGENT_DIR)/*.o $(FRONTEND_DIR)/*.o *.o $(TARGET) $(UI_TARGET) $(MARKET_TARGET) gmon.out
