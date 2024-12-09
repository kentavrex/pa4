all: pa4

clean:
	rm -rf *.log *.o pa4

ipc.o: ipc.c context.h ipc.h pipes.h
	clang -c -std=c99 -pedantic -Werror -Wall ipc.c -o ipc.o

pa4.o: pa4.c common.h context.h ipc.h pa2345.h pipes.h
	clang -c -std=c99 -pedantic -Werror -Wall pa4.c -o pa4.o

pa4: ipc.o pa4.o pipes.o queue.o
	clang ipc.o pa4.o pipes.o queue.o -o pa4 -Llib64 -lruntime

pipes.o: pipes.c ipc.h pipes.h
	clang -c -std=c99 -pedantic -Werror -Wall pipes.c -o pipes.o

queue.o: queue.c ipc.h queue.h
	clang -c -std=c99 -pedantic -Werror -Wall queue.c -o queue.o
