
FLAGS = -O3 -g
FLAGS += -fpic

ABT_PATH=$(HOME)/argobots/install

all:
	gcc $(FLAGS) -shared -o mylib.so mylib.c hook.c zpoline.c load_debug.c -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes
	gcc $(FLAGS) -shared -o debug.so debug.c


patch:
	./e9patch/e9tool  -M 'addr==0x1158' -P 'pref_yield<naked>(rdi)@prefyield' --syntax=intel ./mytest
