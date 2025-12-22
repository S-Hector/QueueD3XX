# Install the correct cross-compilers... this can differ...
# sudo apt install gcc-x86-64-linux-gnu //For x64.
# sudo apt install gcc-i686-linux-gnu //For x86.
# sudo apt install gcc-aarch64-linux-gnu //For ARM 64-bit/V8.
# sudo apt install gcc-arm-linux-gnueabihf //For ARM 32-bit/V7 hard float.
# ^ You need to do the g++ variants to get the stdc++ libs you need.
# Quick console compile/link check: gcc test.c -I./Linux/ -L./ -l:QueueD3XX.so -Wl,-rpath,'$ORIGIN'

# Set gcc as default compiler.
CC ?= gcc
# Set header/.h search directories.
H_DIRS = -I./Linux/
# Set library/.so search directories.
LIB_DIRS = -L./
# Set library/.a links.
LIB_LINKS = -l:libftd3xx.so
# Set final library output .so file name.
LIB_NAME = QueueD3XX
# Set final library output .so directory.
LIB_END_DIR = ./Linux/QueueD3XX/Lib/
# Extra compiler flags go here.
CFLAGS := -D_QUEUE_D3XX_EXPORT -w -fPIC -fpermissive -shared -fvisibility=hidden -Wl,--version-script=LinkPublic.map -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/../'
TARGET = Unknown

# Compile library for all architectures.
all:
	@$(MAKE) x64
	@$(MAKE) x86
	@$(MAKE) arm64
	@$(MAKE) arm32

# ---| x86/x64 64-bit Compilation |---
x64:	TARGET = 64-bit
x64:	LIB_DIRS += -L./Linux/linux-x86_64
x64:	CC = x86_64-linux-gnu-g++
x64:	LIB_END_DIR := $(LIB_END_DIR)x86_64/
x64:	compile

# ---| x86/x64 32-bit Compilation |---
x86:	TARGET = 32-bit
x86:	LIB_DIRS += -L./Linux/linux-x86_32
x86:	CC = i686-linux-gnu-g++
x86:	LIB_END_DIR := $(LIB_END_DIR)x86_32/
x86:	compile

# ---| ARM 64-bit Compilation |---
arm64:	TARGET = ARM 64-bit
arm64:	LIB_DIRS += -L./Linux/linux-arm-v8
arm64:	CC = aarch64-linux-gnu-g++
arm64:	LIB_END_DIR := $(LIB_END_DIR)arm_64/
arm64:	compile

arm32:	TARGET = ARM 32-bit
arm32:	LIB_DIRS += -L./Linux/linux-arm-v7-hf
arm32:	CC = arm-linux-gnueabihf-g++
arm32:	LIB_END_DIR := $(LIB_END_DIR)arm_32/
arm32:	compile

# Our end compile target. Makes end directories as needed.
compile:
	@mkdir -p $(LIB_END_DIR)
	@cp QueueD3XX.h Linux/$(LIB_NAME)/
	@cp Linux/ftd3xx.h Linux/$(LIB_NAME)/
	@echo "---| COMPILING $(TARGET) LIBRARY |---";
	$(CC) HS_QueueD3XX.c QueueD3XX.c  $(CFLAGS) $(H_DIRS) $(LIB_DIRS) $(LIB_LINKS) -o $(LIB_END_DIR)$(LIB_NAME).so

clean:
	rm -rf Linux/$(LIB_NAME)/
