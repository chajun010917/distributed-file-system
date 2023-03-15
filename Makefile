CC     := gcc
CFLAGS := -Wall -Werror 

SRCS   := client.c \
	server.c 

OBJS   := ${SRCS:c=o}
PROGS  := ${SRCS:.c=}

.PHONY: all
all: client.c libmfs.c server.c udp.h udp.c mfs.h ufs.h mkfs.c
	gcc server.c udp.c -o server
	gcc -fPIC -g -c -Wall libmfs.c udp.c
	gcc -shared -Wl,-soname,libmfs.so -o libmfs.so libmfs.o udp.o -lc
	gcc client.c udp.c -o client -L. -lmfs
	gcc mkfs.c -o mkfs

clean:
	rm -f ${PROGS} ${OBJS}

%.o: %.c Makefile
	${CC} ${CFLAGS} -c $<