#-*- mode: makefile -*-
#
# Fred Xia (fxia@yahoo.com)
#

CCFLAGS = -ggdb -g3 -O0 -fPIC -fstack-protector-strong -fvar-tracking \
	-fvar-tracking-assignments -std=c++0x -Wall -m64

all : task_controller task_worker

%.o : %.cc
	g++ $(CCFLAGS) -o $@ -c $<

task_worker : task_worker.o util.o
	g++ -o $@ $^

task_controller : task_controller.o server.o task_db.o util.o 
	g++ -o $@ $^ -lsqlite3

clean :
	rm -rf *.o task_worker task_controller
