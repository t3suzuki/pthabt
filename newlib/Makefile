

ABT_PATH=$(HOME)/argobots/install

all:
	gcc -O3 -g  -shared -fpic -o mylib.so mylib.c hook.c zpoline.c load_debug.c -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes
	gcc -O3 -g  -shared -fpic -o debug.so debug.c
