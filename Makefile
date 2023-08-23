
ARGOBOTS_PATH=./argobots

all:
	g++ -g -I$(ARGOBOTS_PATH)/include -shared -fpic -dl -o pthread.so pthread.c -L $(ARGOBOTS_PATH)/lib -labt -fpermissive -ltbb

hw:
	g++ -g -I$(ARGOBOTS_PATH)/include -fpermissive hello_world.c  -L $(ARGOBOTS_PATH)/lib -labt -L./ -lhwp


hwp:
	g++ -g -I$(ARGOBOTS_PATH)/include -shared -fpic -dl -o hwp.so hwp.c -L $(ARGOBOTS_PATH)/lib -labt -fpermissive

test:
	g++ -g -O3 test.c -fpermissive
	LD_PRELOAD=./pthread.so ./a.out


test2:
	LD_PRELOAD=./hwp.so ./a.out

compr:
	g++ -g -O3 ring.cc -fpermissive

run:
	LD_PRELOAD=./pthread.so ./a.out
