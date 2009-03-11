ifdef TCL_STANDALONE

CFLAGS=-g -Wall -fPIC -DTCL_STANDALONE
LDFLAGS=-shared -L/usr/local/lib -L/usr/X11R6/lib
LIBS=-ltclstub8.4 -lpng -lX11 -lXtst

all: tclsys.so

tclsys.so: tclsys.o
	gcc ${LDFLAGS} -o tclsys.so -DUSE_TCL_STUBS tclsys.o ${LIBS}

tclsys.o: tclsys.c
	${CC} ${CFLAGS} -c tclsys.c

clean:
	-rm *.so *~ *.o

else

ifndef NAVISERVER
    NAVISERVER  = /usr/local/ns
endif

#
# Module name
#
MOD      =  nssys.so

#
# Objects to build.
#
OBJS     = tclsys.o

MODLIBS=-lpng -L/usr/X11R6/lib -lX11 -lXtst

include  $(NAVISERVER)/include/Makefile.module

endif
