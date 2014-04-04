CC=g++
CCFLAGS=-g
LDIR=/usr/lib
IDIR=/usr/include

all: disthcm disthcs disthcc


disthcm: djob.cpp dtalk.cpp disthcm.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoData -lPocoNet -lPocoUtil -lPocoFoundation -lPocoSQLite

disthcc: dtalk.cpp tinycon.cpp disthcc.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation

disthcs: engines/hashcat.cpp djob.cpp dtalk.cpp disthcs.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundationd

clean:
	rm -rf *.o disthcm disthcs disthcc