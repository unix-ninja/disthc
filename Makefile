CC=g++
CCFLAGS=-g
LDIR=/usr/lib
IDIR=/usr/include
EXECS=server slave console

all: $(EXECS)


server: djob.cpp dtalk.cpp disthcm.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoData -lPocoNet -lPocoUtil -lPocoFoundation -lPocoSQLite

console: dtalk.cpp tinycon.cpp disthcc.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation

slave: engines/hashcat.cpp djob.cpp dtalk.cpp disthcs.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundationd

clean:
	rm -rf *.o $(EXECS)