CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -Wall

ENGINE_SRC = src/main.cpp src/matching_engine.cpp src/orderbook.cpp
CORE_SRC   = src/matching_engine.cpp src/orderbook.cpp

all: engine

engine:
	$(CXX) $(CXXFLAGS) $(ENGINE_SRC) -o engine

simulation:
	$(CXX) $(CXXFLAGS) tests/simulation.cpp $(CORE_SRC) -o simulation

performance:
	$(CXX) $(CXXFLAGS) tests/performance.cpp $(CORE_SRC) -o performance

run: engine
	./engine $(ARGS)

clean:
	rm -f engine simulation performance