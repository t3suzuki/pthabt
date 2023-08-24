
ARGOBOTS_PATH=./argobots

all:
	g++ -g -I$(ARGOBOTS_PATH)/include -shared -fpic -dl  -o pthread.so pthread.c hook.c -L $(ARGOBOTS_PATH)/lib -labt -fpermissive -ltbb -lopcodes

hw:
	g++ -g -I$(ARGOBOTS_PATH)/include -fpermissive hello_world.c  -L $(ARGOBOTS_PATH)/lib -labt -L./ -lhwp


hwp:
	g++ -g -I$(ARGOBOTS_PATH)/include -shared -fpic -dl -o hwp.so hwp.c -L $(ARGOBOTS_PATH)/lib -labt -fpermissive

test:
#	g++ -I$(ARGOBOTS_PATH)/include -shared -fPIC hook.c -o hook.so -L $(ARGOBOTS_PATH)/lib -labt -fpermissive
	g++ -g -O3 test.c -fpermissive -I$(ARGOBOTS_PATH)/include -L $(ARGOBOTS_PATH)/lib -labt
#	LIBZPHOOK=./hook.so  LD_PRELOAD=./pthread.so ./a.out
	LD_PRELOAD=./pthread.so ./a.out

test2:
	LD_PRELOAD=./hwp.so ./a.out

compr:
	g++ -g -O3 ring.cc -fpermissive

run:
	LD_PRELOAD=./pthread.so ./a.out
