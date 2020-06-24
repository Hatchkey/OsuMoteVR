./bin/Linux/main: src/render.cpp src/glad.c src/textrendering.cpp include/matrices.h include/utils.h include/dejavufont.h src/tiny_obj_loader.cpp
	mkdir -p bin/Linux
		g++ -std=c++11 -Wall -Wno-unused-function -g -I ./include/ -I ./include/wiic/ -o ./bin/Linux/WM_VR src/render.cpp src/glad.c src/textrendering.cpp src/tiny_obj_loader.cpp -L./lib-linux/ ./lib-linux/libglfw3.a ./lib-linux/libwiicpp.so -lrt -lm -ldl -lX11 -lpthread -lXrandr -lXinerama -lXxf86vm -lXcursor -lwiicpp

.PHONY: clean run
clean:
	rm -f bin/Linux/main

run: ./bin/Linux/main
	cd bin/Linux && ./main
