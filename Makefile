CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -Wall

ENGINE_SRC = src/main.cpp src/orderbook.cpp src/exchange.cpp
CORE_SRC = src/orderbook.cpp src/exchange.cpp

all: engine

engine:
	$(CXX) $(CXXFLAGS) $(ENGINE_SRC) -o engine

simulation:
	$(CXX) $(CXXFLAGS) tests/simulation.cpp $(CORE_SRC) -o simulation

performance:
	$(CXX) $(CXXFLAGS) tests/performance.cpp $(CORE_SRC) -o performance

clean:
	rm -f engine simulation performance