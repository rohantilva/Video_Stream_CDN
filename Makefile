CC=g++
CXXFLAGS=-std=c++11

miProxy: miproxy.cpp
	$(CC) $(CXXFLAGS) miproxy.cpp -o miProxy
