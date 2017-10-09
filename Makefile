CC = g++
SRCDIR = src
INCDIR = inc
CFLAGS = -g -Wall -Iinc -std=c++11 -O2
CPPFLAGS = $(CFLAGS)
LDFLAGS = -lCAENDigitizer -lmongoclient -lsqlite3 -lpthread
INSTALLDIR = /usr/local/bin
INSTALL = cp $(EXE) $(INSTALLDIR)
EXE = obelix
CHMOD = chmod 0755

sources := $(wildcard src/*.cpp)
objects := $(sources:.cpp=.o)
VPATH = src:inc

exe : $(objects)
	$(CC) $(CPPFLAGS) -o $(EXE) $(objects) $(LDFLAGS)

install :
	$(INSTALL)

$(L)%.o : %.cpp %.h %.d
	$(CC) $(CPPFLAGS) -c $< -o $@

$(L)%.d : %.cpp %.h
	$(CC) -MM $(CPPFLAGS) $< -o $@

.PHONY: clean

clean:
	-rm -f $(objects)
