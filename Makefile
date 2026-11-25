CC         := gcc
INC_DIR    := include
SRC_DIR    := src
TOOLS_DIR  := tools
BUILD_DIR  := build

CFLAGS     := -std=c11 -Wall -Wextra -O2 -I$(INC_DIR) -I$(TOOLS_DIR)

ifeq ($(OS),Windows_NT)
    TARGET_EXT := .exe
    SO_EXT     := .dll
    LDFLAGS    :=
    PYTHON     := python
    MKDIR      := if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
    RM         := cmd /C if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)
else
    TARGET_EXT :=
    SO_EXT     := .so
    LDFLAGS    := -ldl
    PYTHON     := python3
    MKDIR      := mkdir -p $(BUILD_DIR)
    RM         := rm -rf $(BUILD_DIR)
endif

HOST   := $(BUILD_DIR)/bumpless_host$(TARGET_EXT)
BENCH  := $(BUILD_DIR)/bumpless_bench$(TARGET_EXT)
PROBE  := $(BUILD_DIR)/probe_motor$(TARGET_EXT)
PLAN_1_3 := $(BUILD_DIR)/plan_v1_to_v3.bp_plan
PLAN_3_1 := $(BUILD_DIR)/plan_v3_to_v1.bp_plan
PLANS    := $(PLAN_1_3) $(PLAN_3_1)
STUBS    := $(INC_DIR)/bp_stubs.h

MODULE_NAMES := fn_main fn_motor_v1 fn_motor_v2_logic fn_motor_v3_layout fn_motor_v4_crash
LIBS         := $(patsubst %,$(BUILD_DIR)/lib%$(SO_EXT),$(MODULE_NAMES))

all: $(BUILD_DIR) $(STUBS) $(HOST) $(LIBS) $(PLANS)

$(BUILD_DIR):
	$(MKDIR)

$(STUBS): $(INC_DIR)/common_ids.h $(TOOLS_DIR)/bp_stub_gen.py
	$(PYTHON) $(TOOLS_DIR)/bp_stub_gen.py generate --header $(INC_DIR)/common_ids.h --out $@

$(HOST): $(SRC_DIR)/host/host_main.c $(INC_DIR)/bumpless.h $(INC_DIR)/common_ids.h $(STUBS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BENCH): $(SRC_DIR)/benchmark/bench_main.c $(INC_DIR)/bumpless.h $(INC_DIR)/common_ids.h $(STUBS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lm

$(BUILD_DIR)/libfn_%$(SO_EXT): $(SRC_DIR)/modules/fn_%.c $(INC_DIR)/bumpless.h $(INC_DIR)/common_ids.h $(STUBS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $<

$(PROBE): $(TOOLS_DIR)/probe_motor.c $(TOOLS_DIR)/bp_probe.h $(INC_DIR)/bumpless.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/layout_v1.json $(BUILD_DIR)/layout_v3.json: $(PROBE)
	./$(PROBE) $(BUILD_DIR)

$(PLAN_1_3): $(BUILD_DIR)/layout_v1.json $(BUILD_DIR)/layout_v3.json $(TOOLS_DIR)/bp_plan_compiler.py
	$(PYTHON) $(TOOLS_DIR)/bp_plan_compiler.py \
		--src $(BUILD_DIR)/layout_v1.json \
		--dst $(BUILD_DIR)/layout_v3.json \
		--fn-id 1 \
		--out-bin $@ \
		--out-c $(BUILD_DIR)/copy_plan_v1_to_v3.h

$(PLAN_3_1): $(BUILD_DIR)/layout_v1.json $(BUILD_DIR)/layout_v3.json $(TOOLS_DIR)/bp_plan_compiler.py
	$(PYTHON) $(TOOLS_DIR)/bp_plan_compiler.py \
		--src $(BUILD_DIR)/layout_v3.json \
		--dst $(BUILD_DIR)/layout_v1.json \
		--fn-id 1 \
		--out-bin $@ \
		--out-c $(BUILD_DIR)/copy_plan_v3_to_v1.h

run: all
	./$(HOST)

bench: all $(BENCH)
	./$(BENCH)

plot: bench
	$(PYTHON) $(TOOLS_DIR)/plot_benchmarks.py

qemu-test:
	$(PYTHON) scripts/run_qemu_test.py

clean:
	$(RM)

.PHONY: all run bench plot qemu-test clean
