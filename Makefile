CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-function -Isrc -Ibuild
BUILD    = build
BIN      = cppparser

SRCS = src/main.cpp src/sema.cpp src/interp.cpp src/codegen.cpp src/json.cpp
OBJS = $(SRCS:src/%.cpp=$(BUILD)/%.o) $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread

$(BUILD)/parser.tab.cpp $(BUILD)/parser.tab.hpp: src/parser.y | $(BUILD)
	bison -d -v -o $(BUILD)/parser.tab.cpp $<

$(BUILD)/lex.yy.cpp: src/lexer.l $(BUILD)/parser.tab.hpp | $(BUILD)
	flex -o $@ $<

$(BUILD)/%.o: $(BUILD)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.cpp $(BUILD)/parser.tab.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

demo: $(BIN)
	mkdir -p out
	./$(BIN) examples/shapes.cpp --run --gen out/shapes.py --viz viz/ast.js --ast out/shapes.json
	python3 out/shapes.py

test: $(BIN)
	./run_tests.sh

serve: $(BIN)
	python3 web/server.py 8000

clean:
	rm -rf $(BUILD) $(BIN) out viz/ast.js

.PHONY: all demo test serve clean
