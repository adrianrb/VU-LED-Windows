# VU LED meter — cross-compile MinGW-w64 a partir do WSL.
#
#   make          compila src/main.cpp + src/app.rc -> output/VULed.exe
#                 (objetos em build/)
#   make clean    apaga só os temporários (build/); preserva output/
#
# -static é OBRIGATÓRIO: sem ele o .exe depende de libstdc++-6.dll /
# libgcc_s_seh-1.dll / libwinpthread-1.dll, que não existem no Windows.

CXX      := x86_64-w64-mingw32-g++
CXXFLAGS := -O2 -std=c++17 -Wall -municode -mwindows
LDFLAGS  := -static -municode -mwindows
LDLIBS   := -lole32 -luuid -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -ladvapi32 -lshell32
RC       := x86_64-w64-mingw32-windres
RCFLAGS  := -I src

SRC := src/main.cpp
OBJ := build/main.o
RES := build/app.res
OUT := output/VULed.exe

.PHONY: all clean

all: $(OUT)

$(OBJ): $(SRC) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Ícone embutido no .exe (o Explorer/taskbar lê o recurso ICON, não o .ico).
$(RES): src/app.rc src/app.ico src/resource.h | build
	$(RC) $(RCFLAGS) -O coff -o $@ src/app.rc

$(OUT): $(OBJ) $(RES) | output
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build output:
	mkdir -p $@

clean:
	rm -rf build
