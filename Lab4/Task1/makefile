LDLIBS=-lpthread -lm

CC=gcc
CFLAGS= -std=gnu99 -Wall
L_FLAGS=-fsanitize=address,undefined

TARGET=server
FILES=${TARGET}.o

.PHONY: clean all

${TARGET} : ${FILES}
	${CC} ${L_FLAGS} -o ${TARGET} ${FILES}

${TARGET}.o: ${TARGET}.c
	${CC} ${C_FLAGS} -o ${TARGET}.o -c ${TARGET}.c

client : client.c
	gcc client.c ${C_FLAGS} ${L_FLAGS} -o client

all: ${TARGET} client

clean:
	rm -f ${FILES} ${TARGET}