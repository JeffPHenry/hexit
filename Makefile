OBJS    = main.o hexit.o
CC      = g++
DEBUG   = -g
CFLAGS  = -Wall -c
LFLAGS  = -Wall
LDLIBS  = -lcurses -ltermkey
EXE     = hexit

SRCS = \
    main.cpp \
    hexit.cpp

.PHONY: all clean
all: $(EXE)

$(EXE): $(OBJS)
	$(CC) $(DEBUG) $(LFLAGS) -o $(EXE) $(OBJS) $(LDLIBS)

%.o: %.cpp hexit.h hexit_def.h
	$(CC) $(DEBUG) $(CFLAGS) $< -o $@

clean:
	rm -f *.o $(EXE)
