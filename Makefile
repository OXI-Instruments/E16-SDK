BUILD_DIR ?= build
BUILD_TYPE ?= MinSizeRel

all: $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) --parallel

$(BUILD_DIR)/Makefile:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
