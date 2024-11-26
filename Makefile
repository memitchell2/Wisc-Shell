# Define the compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Werror -g

# Name of the executable
TARGET = wish

# Source files
SRCS = wisc-shell.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default rule to build the executable
all: $(TARGET)

# Rule to link the object files and create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile the source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up the build files
clean:
	rm -f $(OBJS) $(TARGET)

# Rule to run the program
run: $(TARGET)
	./$(TARGET)
# Rule to run given test cases by the university on school linux computers
test:
	~cs537-1/tests/p3/runtests
.PHONY: all clean run
