CC 	= gcc

CFLAGS  = -Wall -g -I ../include

LD 	= gcc

LDFLAGS  = -Wall -g -L../lib64

PROGS	= snakes nums hungry

SNAKEOBJS  = randomsnakes.o util.o

SNAKELIBS = -lPLN -lsnakes -lncurses -lrt

HUNGRYOBJS = hungrysnakes.o util.o

NUMOBJS    = numbersmain.o

OBJS	= $(SNAKEOBJS) $(HUNGRYOBJS) $(NUMOBJS) 

EXTRACLEAN = core $(PROGS)

.PHONY: all allclean clean rs hs ns liblwp.so lwp.o roundRobin.o magic64.o

liblwp.so: lwp.c lwp.h magic64.o roundRobin.o lwp.o
	$(CC) -shared -fPIC -o liblwp.so lwp.o roundRobin.o magic64.o

lwp.o: lwp.c lwp.h
	$(CC) -Wall -g -fPIC -c -o lwp.o -c lwp.c

roundRobin.o: roundRobin.h roundRobin.c
	$(CC) -Wall -g -fPIC -c -o roundRobin.o roundRobin.c

magic64.o: magic64.S
	$(CC) -o magic64.o -c magic64.S

all: 	$(PROGS)

allclean: clean
	@rm -f $(EXTRACLEAN)

clean:	
	rm -f $(OBJS) *~ TAGS

snakes: randomsnakes.o util.o ../lib64/libPLN.so ../lib64/libsnakes.so
	$(LD) $(LDFLAGS) -o snakes randomsnakes.o util.o $(SNAKELIBS)

hungry: hungrysnakes.o  util.o ../lib64/libPLN.so ../lib64/libsnakes.so
	$(LD) $(LDFLAGS) -o hungry hungrysnakes.o util.o $(SNAKELIBS)

nums: numbersmain.o  util.o ../lib64/libPLN.so 
	$(LD) $(LDFLAGS) -o nums numbersmain.o -lPLN

hungrysnakes.o: hungrysnakes.c ../include/lwp.h ../include/snakes.h
	$(CC) $(CFLAGS) -c hungrysnakes.c

randomsnakes.o: randomsnakes.c ../include/lwp.h ../include/snakes.h
	$(CC) $(CFLAGS) -c randomsnakes.c

numbermain.o: numbersmain.c lwp.h
	$(CC) $(CFLAGS) -c numbersmain.c

util.o: util.c ../include/lwp.h ../include/util.h ../include/snakes.h
	$(CC) $(CFLAGS) -c util.c

rs: snakes
	(export LD_LIBRARY_PATH=../lib64; ./snakes)

hs: hungry
	(export LD_LIBRARY_PATH=../lib64; ./hungry)

ns: nums
	(export LD_LIBRARY_PATH=../lib64; ./nums)
