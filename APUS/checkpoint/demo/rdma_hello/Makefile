CC=gcc
CFLAGS=-g -O0
LDLIBS=-lrdmacm -libverbs -pthread
BINS= server client

server:rdma_server.o
	$(CC) -o "$@" "$<" $(LDLIBS)

client:rdma_client.o
	$(CC) -o "$@" "$<" $(LDLIBS)

all: $(BIN)

clean:
	rm -rf *.o $(BINS)

.PHONY: server client clean
