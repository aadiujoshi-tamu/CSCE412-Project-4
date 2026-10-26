CXX ?= g++
CPPFLAGS ?=
CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -g
LDFLAGS ?=
LDLIBS ?=

TARGET := loadbalancer
BUILD_DIR := build
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
DEPFILES := $(OBJECTS:.o=.d)

.PHONY: all run test docs clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES)

run: $(TARGET)
	./$(TARGET) $(ARGS)

test:
	bash tests/run_tests.sh

docs:
	@command -v doxygen >/dev/null 2>&1 || { echo "Doxygen is required for this target." >&2; exit 1; }
	doxygen Doxyfile
	@test -f docs/doxygen/html/index.html || { echo "Doxygen did not create the HTML index." >&2; exit 1; }
	@echo "Doxygen HTML generated: docs/doxygen/html/index.html"

clean:
	rm -rf $(BUILD_DIR) $(TARGET) docs/doxygen
