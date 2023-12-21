CXX = g++

INCLUDES = -I./ 
CXXFLAGS = -O3 -Wall -DNDEBUG $(INCLUDES)
LDFLAGS = -lexanic -L./

TARGETS = shfe-sub-exanic shfe-sub-raw-socket

SRC = $(wildcard ./*.c)
OBJS = $(SRC:%.c=%.o)

#防止all clean文件时，make all或者make clean执行失败
.PHONY: all clean $(TARGETS)

all : $(TARGETS)

shfe-sub-exanic: shfe-sub-exanic.o
	$(CXX) $^ -o $@ $(LDFLAGS)

shfe-sub-raw-socket: shfe-sub-raw-socket.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o:%.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGETS) *.o
