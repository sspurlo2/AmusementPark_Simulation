# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread

# Source files
SRC = park.c
OBJ = $(SRC:.c=.o)

# Output executable
TARGET = park

# Default rule
all: $(TARGET)

# Linking
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile individual files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(TARGET) *.o