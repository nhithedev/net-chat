# Lõi mạng phía client (Linux/WSL).  make  ->  build/netchat-core
# Dùng chung khung 12 byte, mã hóa Caesar (../../common) và cJSON (server) với server.
CC   ?= gcc
CFLAGS ?= -O2 -g
WARN := -std=gnu11 -Wall -Wextra -Wshadow
COM  := ../../common
SRV  := ../../server
INC  := -I$(COM) -I$(SRV)/cJSON

.PHONY: all clean
all: build/netchat-core

build/netchat-core: src/core.c $(COM)/frame.c $(COM)/cipher.c $(SRV)/cJSON/cJSON.c | build
	$(CC) $(CFLAGS) $(WARN) $(INC) -c src/core.c -o build/core.o
	$(CC) $(CFLAGS) $(WARN) $(INC) -c $(COM)/frame.c -o build/frame.o
	$(CC) $(CFLAGS) $(WARN) $(INC) -c $(COM)/cipher.c -o build/cipher.o
	$(CC) $(CFLAGS) -std=gnu11 -w -I$(SRV)/cJSON -c $(SRV)/cJSON/cJSON.c -o build/cJSON.o
	$(CC) $(CFLAGS) build/core.o build/frame.o build/cipher.o build/cJSON.o -lm -o $@

build:
	mkdir -p build

clean:
	rm -rf build

# test lõi với server thật (cần DB đang chạy và `make` trong ../../server); mất ~70 giây
.PHONY: test
test: build/netchat-core
	python3 -I tests/resume_test.py build/netchat-core ../../server/build/netchat-server
