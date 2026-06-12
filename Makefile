CC      = gcc
CFLAGS  = -Wall -Wextra -g $(shell pkg-config --cflags gtk+-3.0)
LIBS    = $(shell pkg-config --libs gtk+-3.0) -lpthread

COMMON_SRC = common/protocol.c

SERVER_SRC = server/main.c \
             server/server_net.c \
             server/devices.c \
             $(COMMON_SRC)

CLIENT_SRC = client/main.c \
             client/client_net.c \
             $(COMMON_SRC)

.PHONY: all clean server client

all: server client

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -o server_app $(SERVER_SRC) $(LIBS)

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o client_app $(CLIENT_SRC) $(LIBS)

clean:
	rm -f server_app client_app
