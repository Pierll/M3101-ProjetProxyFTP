GCC = gcc
SOURCES = $(wildcard *.c)
BINAIRES = $(patsubst %.c,%.o,${SOURCES})
FLAGS = -g -Wall 

all: proxy
	mv *.o obj

proxy: ${BINAIRES}
	${GCC} $^ -o $@ 
%.o: %.c
	${GCC} -c ${FLAGS} $< 	
clean:
	rm -f obj/*.o proxy