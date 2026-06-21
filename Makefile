# Define the compiler and installer flags
CC           = gcc

CFLAGS       = -Wall -Wextra -Werror -Wdeprecated-declarations -O2 -std=gnu2x
CFLAGS_DEBUG = -Wall -Wextra -Werror -g -O0 -std=gnu2x -fsanitize=address -fno-omit-frame-pointer

# Define the target executable name
TARGET   = bdump

# Default rule
all: $(TARGET)

# Rule to link the final executable
$(TARGET): bdump.c
	$(CC) $(CFLAGS) -o $(TARGET) bdump.c

# debug build
debug:
	$(CC) $(CFLAGS_DEBUG) -o $(TARGET) bdump.c


# Clean up build artifacts
.PHONY: clean all
clean:
	@rm -f $(TARGET) $(TARGET).o
