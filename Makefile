# VU LED meter — cross-compile MinGW-w64 a partir do WSL.
#
#   make          compila todos os src/**/*.cpp + src/res/app.rc -> output/VULed.exe
#                 (objetos em build/, espelhando as pastas de src/)
#   make clean    apaga só os temporários (build/); preserva output/
#
# -static é OBRIGATÓRIO: sem ele o .exe depende de libstdc++-6.dll /
# libgcc_s_seh-1.dll / libwinpthread-1.dll, que não existem no Windows.

CXX      := x86_64-w64-mingw32-g++
CXXFLAGS := -O2 -std=c++17 -Wall -municode -mwindows -I src
LDFLAGS  := -static -municode -mwindows
LDLIBS   := -lole32 -luuid -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -ladvapi32 -lshell32
RC       := x86_64-w64-mingw32-windres
RCFLAGS  := -I src/res

SRCS := $(wildcard src/*.cpp src/*/*.cpp)
OBJS := $(SRCS:src/%.cpp=build/%.o)
DEPS := $(OBJS:.o=.d)
RES  := build/app.res
OUT  := output/VULed.exe

.PHONY: all clean

all: $(OUT)

# build/ui/main_window.o <- src/ui/main_window.cpp (mkdir para subpastas)
build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Ícone embutido no .exe (o Explorer/taskbar lê o recurso ICON, não o .ico).
$(RES): src/res/app.rc src/res/app.ico src/res/resource.h | build
	$(RC) $(RCFLAGS) -O coff -o $@ src/res/app.rc

$(OUT): $(OBJS) $(RES) | output
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build output:
	mkdir -p $@

-include $(DEPS)

clean:
	rm -rf build
