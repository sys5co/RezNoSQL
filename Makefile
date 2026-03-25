CC      = xlc
AR      = ar
ARFLAGS = rcs
CFLAGS  = -qcpluscmt -qlanglvl=extc99 -I.

# Shared library (DLL) flags
CFLAGS_DLL  = $(CFLAGS) -qDLL -qEXPORTALL
LDFLAGS_DLL = -qDLL -qEXPORTALL

# Library names
LIB_STATIC = libreznosql.a
LIB_SHARED = libreznosql.so
LIB_SIDE   = libreznosql.x

# Source
LIB_SRC = reznosql.c

# Default: shared library + demo
all: $(LIB_SHARED) demo

# Static library
static: $(LIB_STATIC)

$(LIB_STATIC): reznosql.o
	$(AR) $(ARFLAGS) $@ reznosql.o

reznosql.o: reznosql.c reznosql.h
	$(CC) $(CFLAGS) -c -o $@ reznosql.c

# Shared library (DLL)
$(LIB_SHARED): reznosql_dll.o
	export _C89_LSYSDEFSD=$(LIB_SIDE) && $(CC) $(LDFLAGS_DLL) -Wl,DLL -o $@ reznosql_dll.o

reznosql_dll.o: reznosql.c reznosql.h
	$(CC) $(CFLAGS_DLL) -c -o $@ reznosql.c

# Demo linked against shared library
demo: demo.c $(LIB_SHARED) reznosql.h
	$(CC) $(CFLAGS) -qDLL -o $@ demo.c $(LIB_SIDE)

# Demo linked statically
demo_static: demo.c $(LIB_STATIC) reznosql.h
	$(CC) $(CFLAGS) -o $@ demo.c $(LIB_STATIC)

# Full sample (static link)
reznosql_sample: reznosql_sample.c reznosql.c reznosql.h
	$(CC) $(CFLAGS) -o $@ reznosql_sample.c reznosql.c

clean:
	rm -f *.o $(LIB_STATIC) $(LIB_SHARED) $(LIB_SIDE)
	rm -f demo demo_static reznosql_sample

.PHONY: all static clean
