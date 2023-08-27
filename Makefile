CC=gcc
CFLAGS=-Wall 
LIBS= -lcomctl32 -lkernel32 -lgdi32 -lcomdlg32 -lwinmm -mwindows


gditetris.exe:	gditetris.o resources.o Makefile
	${CC} gditetris.o resources.o -o gditetris.exe ${LIBS}
	strip gditetris.exe

gditetris.o:	gditetris.c resources.o Makefile
	${CC} -c gditetris.c -o gditetris.o ${CFLAGS}

resources.o:	resources.rc resources.h Makefile
	windres -i resources.rc -o resources.o

clean:
	@del *.*~
	@del *.o
