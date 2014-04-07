CC=g++
CCFLAGS=-g
LDIR=/usr/local/lib
IDIR=/usr/local/include
EXECS=master slave console

.PHONY: master slave console

all: $(EXECS)

master: bin/disthcm
slave: bin/disthcs
console: bin/disthcc

bin/disthcm: src/djob.cpp src/dtalk.cpp src/disthcm.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation -lPocoDataSQLite -lPocoData -lPocoCrypto

bin/disthcc: src/dtalk.cpp src/tinycon.cpp src/disthcc.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation

bin/disthcs: src/engines/hashcat.cpp src/djob.cpp src/dtalk.cpp src/disthcs.cpp
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation

clean:
	rm -rf *.o $(EXECS)
