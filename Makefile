# Twin chess engine — build file
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -flto -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?=

# Optional native tuning (uncomment for a small speed boost on the build machine):
# CXXFLAGS += -march=native

TARGET = twin

all: $(TARGET)

$(TARGET): main.cpp bitboard.h position.h eval.h search.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp $(LDFLAGS)

# Correctness harnesses
perft: perft_test.cpp bitboard.h position.h
	$(CXX) $(CXXFLAGS) -o perft_test perft_test.cpp
	./perft_test

evaltest: eval_test.cpp bitboard.h position.h eval.h
	$(CXX) $(CXXFLAGS) -o eval_test eval_test.cpp
	./eval_test

selfplay: selfplay_test.cpp bitboard.h position.h eval.h search.h
	$(CXX) $(CXXFLAGS) -o selfplay_test selfplay_test.cpp
	./selfplay_test 8

test: all perft evaltest selfplay
	python3 uci_driver.py ./$(TARGET)

clean:
	rm -f $(TARGET) perft_test eval_test selfplay_test

.PHONY: all clean test perft evaltest selfplay
