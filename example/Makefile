CXX = clang++
CXXFLAGS = -std=c++20 -Wall -Wextra -g

all: contracts_example

contracts_example: main.cpp contracts_runtime.cpp contracts_abi.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp contracts_runtime.cpp

clean:
	rm -f contracts_example

run: contracts_example
	./contracts_example || true

.PHONY: all clean run
