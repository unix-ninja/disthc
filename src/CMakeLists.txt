# First we check that libraries are installed

find_package(Poco)

set(COMMON_LIB PocoNet PocoUtil PocoFoundation)

# Sources for each target
set(MASTER_SRC djob.cpp dtalk.cpp disthcm.cpp)
set(CONSOLE_SRC dtalk.cpp tinycon.cpp disthcc.cpp)
set(SLAVE_SRC engines/hashcat.cpp djob.cpp dtalk.cpp disthcs.cpp)


# The master executable
add_executable(disthcm ${MASTER_SRC})
target_link_libraries(disthcm ${COMMON_LIB} PocoDataSQLite PocoData PocoCrypto)

# The console executable
add_executable(disthcc ${CONSOLE_SRC})
target_link_libraries(disthcc ${COMMON_LIB})


# The slave executable
add_executable(disthcs ${SLAVE_SRC})
target_link_libraries(disthcs ${COMMON_LIB})





