.c.o:
	gcc -Wall -O2 -DPLATFORM_SDL -g -c $< -o $@
.cpp.o:
	gcc -Wall -O2 -DPLATFORM_SDL -g -c $< -o $@

default: one-headed-dog

esp32:
	pio run

src_cpp=src/emu_cpu.cpp src/Z80.cpp main-sdl.cpp src/cat.cpp
src_c=src/fake6502.c
objs = $(src_cpp:.cpp=.o) $(src_c:.c=.o)

one-headed-dog: $(objs)
	g++ $(objs) -lSDL2 -g -o one-headed-dog

clean:
	-rm *.o src/*.o one-headed-dog

format:
	clang-format-16 -i *.cpp src/*.cpp src/*.h src/*.c
