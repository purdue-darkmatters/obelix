CC = g++
OBJDIR = obj
SRCDIR = src
INCDIR = inc
CFLAGS = -g -Wall -Iinc -std=c++11 -O2 -lsqlite3 -lboost
CPPFLAGS = $(CFLAGS) $(DEFS)
#DEFS := $(DEFS) -DCCM_ONLY
INSTALLDIR = /usr/local/bin
INSTALL = -o $(INSTALLDIR)/obelix
TEST = -o test_exe
sources := $(wildcard src/*.cpp)
objects := $(sources:.cpp=.o)
VPATH = src:inc

test : $(objects)
	$(CC) $(CPPFLAGS) $(TEST) $(sources)

install : $(objects)
	$(CC) $(CPPFLAGS) $(INSTALL) $(objects)

$(L)%.o : %.cpp %.h %.d
	$(CC) $(CPPFLAGS) -c $< -o $@

$(L)%.d : %.cpp %.h
	$(CC) -MM $(CPPFLAGS) $< -o $@

.PHONY: clean

clean:
	-rm -f $(objects)
