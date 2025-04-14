CC = gcc
CFLAGS = -Wall
LDFLAGS = lm
SRC = rpc.c
OBJ = $(SRC:.c=.o)
OUT = rpc

all: $(OUT)

$(OUT): $(OBJ)
	$(CC) $(CFLAGS) -lm -o $(OUT) $(OBJ)

%.o: %.c rpc.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(OUT)

.PHONY: all clean
