# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -pthread
CFLAGS_RAYLIB = -lraylib

# Output executable names
PROG1 = client
PROG2 = server
PROG3 = client_gui

# Source files
SRC1 = client.c
SRC2 = server.c
SRC3 = client_gui.c

# Default target
all: $(PROG1) $(PROG2) $(PROG3)

# Compile first threaded program
$(PROG1): $(SRC1)
	$(CC) $(CFLAGS) -o $(PROG1) $(SRC1)

# Compile second threaded program
$(PROG2): $(SRC2)
	$(CC) $(CFLAGS) -o $(PROG2) $(SRC2)

# Compile second threaded program
$(PROG3): $(SRC3)
	$(CC) $(CFLAGS) $(CFLAGS_RAYLIB) -o $(PROG3) $(SRC3)

# Clean build files
clean:
	rm -f $(PROG1) $(PROG2)

# Help target
help:
	@echo "Available targets:"
	@echo "  all    : Build both threaded programs (default)"
	@echo "  clean  : Remove compiled executables"
	@echo "  help   : Show this help message"

.PHONY: all clean help