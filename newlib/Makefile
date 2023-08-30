
FLAGS = -O3 -g
FLAGS += -fpic

ABT_PATH=$(HOME)/argobots/install

all:
	g++ $(FLAGS) -c -o mylibcpp.o mylibcpp.cc -I $(ABT_PATH)/include
	gcc $(FLAGS) -shared -o mylib.so mylibcpp.o mylib.c hook.c zpoline.c load_debug.c -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes
	gcc $(FLAGS) -shared -o debug.so debug.c
