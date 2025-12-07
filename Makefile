CXX = g++
CXXFLAGS = -Wall -std=c++17 -Iinclude -DUNICODE -D_UNICODE
LDFLAGS = -mwindows -static-libgcc -static-libstdc++
LIBS = -lcomctl32

SRC_DIR = src
OBJ_DIR = build
BIN_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Add sqlite3.c if it exists in lib/
ifneq ("$(wildcard lib/sqlite3.c)","")
	SRCS += lib/sqlite3.c
	OBJS += $(OBJ_DIR)/sqlite3.o
	CXXFLAGS += -DSQLITE_THREADSAFE=1
endif

TARGET = $(BIN_DIR)/NoteSoFast.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/sqlite3.o: lib/sqlite3.c
	@if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	del /Q $(OBJ_DIR)\*.o $(TARGET)

.PHONY: all clean
