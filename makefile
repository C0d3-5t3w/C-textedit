CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -I./inc
LDFLAGS = 

SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj
BIN_DIR = bin

TARGET = $(BIN_DIR)/ctextedit
SRC = $(SRC_DIR)/main.c
OBJ = $(OBJ_DIR)/main.o

.PHONY: all clean build run

build: $(TARGET)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

run:
	./$(TARGET)