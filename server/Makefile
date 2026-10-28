# Dùng:
#   make            build build/netchat-server và build/netchat-client
#   make test       unit test (C) + test tích hợp qua socket thật (Python)
#   make asan       build lại với AddressSanitizer + UBSan rồi chạy test
#   make clean
CC      ?= gcc
CFLAGS  ?= -O2 -g
WARN    := -std=gnu11 -Wall -Wextra -Wshadow
PGINC   := $(shell pg_config --includedir 2>/dev/null || echo /usr/include/postgresql)
INC     := -Iinclude -IcJSON -I$(PGINC)
BUILD   := build

LIB_SRC := src/net.c src/msg.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o) $(BUILD)/cJSON.o
APP_SRC := store auth
APP_OBJ := $(APP_SRC:%=$(BUILD)/%.o)
FAST_OBJ := $(BUILD)/fast/net.o $(BUILD)/fast/msg.o $(APP_SRC:%=$(BUILD)/fast/%.o) $(BUILD)/cJSON.o

.PHONY: all test unit integration asan clean
all: $(BUILD)/netchat-server $(BUILD)/netchat-client

$(BUILD)/netchat-server: $(BUILD)/server.o $(LIB_OBJ) $(APP_OBJ)
	$(CC) $(CFLAGS) $^ -lm -lcrypto -lpq -o $@

# client dòng lệnh để thử tay; chỉ cần phần framing của net.c
$(BUILD)/netchat-client: $(BUILD)/client.o $(BUILD)/net.o
	$(CC) $(CFLAGS) $^ -o $@

# bản có timeout ngắn (2s chưa đăng nhập, 4s im lặng) để test nhanh
$(BUILD)/netchat-server-fast: $(BUILD)/fast/server.o $(FAST_OBJ)
	$(CC) $(CFLAGS) $^ -lm -lcrypto -lpq -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) $(INC) -MMD -c $< -o $@

$(BUILD)/fast/%.o: src/%.c | $(BUILD)/fast
	$(CC) $(CFLAGS) $(WARN) $(INC) -DNET_AUTH_TIMEOUT_S=2 -DNET_IDLE_TIMEOUT_S=4 \
		-MMD -c $< -o $@

# thư viện bên thứ ba: không áp cảnh báo của mình
$(BUILD)/cJSON.o: cJSON/cJSON.c | $(BUILD)
	$(CC) $(CFLAGS) -std=gnu11 -w -IcJSON -c $< -o $@

$(BUILD)/test_%: tests/test_%.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(WARN) $(INC) $^ -lm -o $@

$(BUILD) $(BUILD)/fast:
	mkdir -p $@

unit: $(BUILD)/test_frame $(BUILD)/test_msg
	$(BUILD)/test_frame
	$(BUILD)/test_msg

integration: $(BUILD)/netchat-server $(BUILD)/netchat-server-fast
	python3 -I tests/integration.py $(BUILD)/netchat-server $(BUILD)/netchat-server-fast

test: unit integration

# build riêng trong build-asan/ để không lẫn object có/không có sanitizer
asan:
	$(MAKE) BUILD=build-asan \
		CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf build build-asan

-include $(wildcard $(BUILD)/*.d $(BUILD)/fast/*.d)
