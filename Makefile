CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Iinclude -Wall -Wextra

# Benchmark needs Google Benchmark library
BENCH_FLAGS = -O3 -std=c++17 -Iinclude -Wall -Wextra
BENCH_LIBS  = -I/usr/local/include -L/usr/local/lib -lbenchmark -lpthread

SRC_DIR  = src
TEST_DIR = tests

CORE_SRC = $(SRC_DIR)/matching_engine.cpp $(SRC_DIR)/orderbook.cpp

# Targets
CLI_TARGET   = cli
SIM_TARGET   = sim
TEST_TARGET  = test
BENCH_TARGET = bench

.PHONY: all cli sim test bench clean

all: cli sim test

cli:
	$(CXX) $(CXXFLAGS) $(CORE_SRC) $(TEST_DIR)/cli.cpp -o $(CLI_TARGET)
	./$(CLI_TARGET)
	
sim:
	$(CXX) $(CXXFLAGS) $(CORE_SRC) $(TEST_DIR)/simulation.cpp -o $(SIM_TARGET)
	./$(SIM_TARGET)

test:
	$(CXX) $(CXXFLAGS) $(CORE_SRC) $(TEST_DIR)/test_engine.cpp -o $(TEST_TARGET)
	./$(TEST_TARGET)

bench:
	$(CXX) $(BENCH_FLAGS) $(CORE_SRC) $(TEST_DIR)/benchmark.cpp \
		-o $(BENCH_TARGET) $(BENCH_LIBS)
	./$(BENCH_TARGET)

clean:
	rm -f $(CLI_TARGET) $(SIM_TARGET) $(TEST_TARGET) $(BENCH_TARGET)