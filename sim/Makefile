# Simulator: make -C sim (ohne Fenster, für --tour) oder make -C sim sdl (mit Fenster, braucht SDL2)
CXX ?= c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I../core -I../game
SRC = main.cpp $(wildcard ../core/*.cpp) $(wildcard ../game/*.cpp)
DEPS = $(SRC) $(wildcard ../core/*.h) $(wildcard ../game/*.h)

bcsim: $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lpthread

sdl: $(DEPS)
	$(CXX) $(CXXFLAGS) -DWITH_SDL $(shell sdl2-config --cflags) -o bcsim $(SRC) $(shell sdl2-config --libs)

clean:
	rm -f bcsim
