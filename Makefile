CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I engine
LDFLAGS  :=

BUILD := build

# --- Targets -------------------------------------------------------------

all: $(BUILD)/ome-cli $(BUILD)/ome-tests

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/ome-cli: engine/matching_engine.cpp engine/order_book.cpp engine/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/ome-tests: engine/matching_engine.cpp engine/order_book.cpp tests/test_engine.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(BUILD)/ome-tests
	./$(BUILD)/ome-tests

bench: $(BUILD)/ome-cli
	./$(BUILD)/ome-cli --bench 1000000

clean:
	rm -rf $(BUILD)

.PHONY: all test bench clean
