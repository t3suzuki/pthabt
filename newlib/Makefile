
FLAGS = -O3 -g
FLAGS += -fpic

ABT_PATH=$(HOME)/argobots/install

all:
	g++ $(FLAGS) -c -o mylibcpp.o mylibcpp.cc -I $(ABT_PATH)/include
	gcc $(FLAGS) -c -o mylib.o mylib.c -I $(ABT_PATH)/include
	gcc $(FLAGS) -c -o hook.o hook.c -I $(ABT_PATH)/include
	gcc $(FLAGS) -c -o zpoline.o zpoline.c
	gcc $(FLAGS) -c -o load_debug.o load_debug.c
	g++ $(FLAGS) -shared -o mylib.so mylibcpp.o mylib.o hook.o zpoline.o load_debug.o -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes
	gcc $(FLAGS) -shared -o debug.so debug.c
