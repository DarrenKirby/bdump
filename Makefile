# Define the compiler and installer flags
CC ?= cc
#CC = /usr/local/opt/llvm/bin/clang

# Default install prefix
PREFIX ?= /usr/local

CFLAGS       = -Wall -Wextra -Werror -Wdeprecated-declarations -O2 -std=gnu2x
CFLAGS_DEBUG = -Wall -Wextra -Werror -g -O0 -std=gnu2x -fsanitize=address -fno-omit-frame-pointer

# Define the target executable name
TARGET     = bdump
MAN_TARGET = bdump.1

# Default rule
all: $(TARGET)

# Rule to link the final executable
$(TARGET): bdump.c
	$(CC) $(CFLAGS) -o $(TARGET) bdump.c

# debug build
debug:
	$(CC) $(CFLAGS_DEBUG) -o $(TARGET) bdump.c

# Install target
install: $(TARGET)
	@mkdir -p $(PREFIX)/bin
	@mkdir -p $(PREFIX)/share/man/man1
	@install -v -m 0755 bdump $(PREFIX)/bin
	@install -v -m 0644 bdump.1 $(PREFIX)/share/man/man1

# Uninstall target
uninstall:
	@rm -v $(PREFIX)/bin/$(TARGET)
	@rm -v $(PREFIX)/share/man/man1/$(MAN_TARGET)

# Clean up build artifacts
.PHONY: clean all install uninstall
clean:
	@rm -f $(TARGET) $(TARGET).o
