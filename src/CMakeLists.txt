# First we check that libraries are installed

find_package(Poco)

# The master executable
add_executable(disthcm djob.cpp dtalk.cpp disthcm.cpp)
target_link_libraries(disthcm PocoNet PocoUtil PocoFoundation PocoDataSQLite PocoData PocoCrypto)

# The console executable
add_executable(disthcc dtalk.cpp tinycon.cpp disthcc.cpp)
target_link_libraries(disthcc PocoNet PocoUtil PocoFoundation)


# The slave executable
add_executable(disthcs engines/hashcat.cpp djob.cpp dtalk.cpp disthcs.cpp)
target_link_libraries(disthcs PocoNet PocoUtil PocoFoundation)





