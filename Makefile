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
	@echo "Compiling Application (master server)..."
	@echo "#########################################"
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation -lPocoDataSQLite -lPocoData -lPocoCrypto
	@echo "done."
	@echo

bin/disthcs: src/engines/hashcat.cpp src/djob.cpp src/dtalk.cpp src/disthcs.cpp
	@echo "Compiling Application (slave)..."
	@echo "#################################"
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation
	@echo "done."
	@echo

bin/disthcc: src/dtalk.cpp src/tinycon.cpp src/disthcc.cpp
	@echo "Compiling Application (console)..."
	@echo "###################################"
	$(CC) -o $@ $(CCFLAGS) $^ -lPocoNet -lPocoUtil -lPocoFoundation
	@echo "done."
	@echo

clean:
	@echo "Cleaning..."
	@echo "############"
	rm -rf *.o $(EXECS)
	@echo "done."
	@echo
