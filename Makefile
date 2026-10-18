# VU LED meter — cross-compile MinGW-w64 a partir do WSL.
#
#   make          compila src/main.cpp -> output/VULed.exe (objeto em build/)
#   make clean    apaga só os temporários (build/); preserva output/
#
# -static é OBRIGATÓRIO: sem ele o .exe depende de libstdc++-6.dll /
# libgcc_s_seh-1.dll / libwinpthread-1.dll, que não existem no Windows.

CXX      := x86_64-w64-mingw32-g++
CXXFLAGS := -O2 -std=c++17 -Wall -municode -mwindows
LDFLAGS  := -static -municode -mwindows
LDLIBS   := -lole32 -luuid -lgdi32 -luser32 -lcomdlg32 -lcomctl32

SRC := src/main.cpp
OBJ := build/main.o
OUT := output/VULed.exe

.PHONY: all clean

all: $(OUT)

$(OBJ): $(SRC) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT): $(OBJ) | output
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build output:
	mkdir -p $@

clean:
	rm -rf build
