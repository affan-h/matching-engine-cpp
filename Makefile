CXX = g++
CXXFLAGS = -O3 -march=native -std=c++17 -Iinclude -I/usr/local/include -Wall -Wextra -DNDEBUG -lpthread

# ADD this line for libraries specifically needed by the benchmark
BENCH_LIBS = -L/usr/local/lib -lbenchmark -lpthread 

SRC_DIR = src
TEST_DIR = tests

ENGINE_SRC = $(SRC_DIR)/main.cpp $(SRC_DIR)/matching_engine.cpp $(SRC_DIR)/orderbook.cpp
CORE_SRC   = $(SRC_DIR)/matching_engine.cpp $(SRC_DIR)/orderbook.cpp

ENGINE_TARGET = engine
SIM_TARGET = simulation
BENCH_TARGET = benchmark

all: $(ENGINE_TARGET)

$(ENGINE_TARGET):
	$(CXX) $(CXXFLAGS) $(ENGINE_SRC) -o $(ENGINE_TARGET)

$(SIM_TARGET):
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/simulation.cpp $(CORE_SRC) -o $(SIM_TARGET)

# UPDATE THIS BLOCK to include $(BENCH_LIBS) at the end 
$(BENCH_TARGET):
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/benchmark.cpp $(CORE_SRC) -o $(BENCH_TARGET) $(BENCH_LIBS)

run: $(ENGINE_TARGET)
	./$(ENGINE_TARGET) $(ARGS)

sim: $(SIM_TARGET)
	./$(SIM_TARGET)

bench: $(BENCH_TARGET)
	./$(BENCH_TARGET)

clean:
	rm -f $(ENGINE_TARGET) $(SIM_TARGET) $(BENCH_TARGET)