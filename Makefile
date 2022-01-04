PROJECT_NAME = server
BUILD_DIR ?= .
OBJ_DIR ?= $(BUILD_DIR)/obj
PROJECT_BINARY ?= $(BUILD_DIR)/$(PROJECT_NAME)

.DEFAULT_GOAL = app

# Compilation flags
CXX = g++
LD = g++
CXXFLAGS   += -O2 -MMD -std=c++11 -pthread -lmysqlclient -lhiredis

# Files to be compiled
SRCS = $(shell find ./src -name "*.cpp")
OBJS = $(SRCS:./%.cpp=$(OBJ_DIR)/%.o)

# Compilation patterns
$(OBJ_DIR)/%.o: ./%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# Depencies
-include $(OBJS:.o=.d)

# Some convinient rules

.PHONY: app run clean

app: $(PROJECT_BINARY)

# Link
$(PROJECT_BINARY): $(OBJS)
	@$(LD) -O2 -o $@ $^ -pthread -lmysqlclient -lhiredis

clean: 
	rm -rf $(OBJ_DIR)
	rm -rf $(BUILD_DIR)/$(PROJECT_NAME)