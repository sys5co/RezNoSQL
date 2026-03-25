CC      = xlc
CFLAGS  = -qlanglvl=extc99 -I..

all: search search_indexed test_hashed

search: search.c ../reznosql.c ../reznosql.h
	$(CC) $(CFLAGS) -o $@ search.c ../reznosql.c

search_indexed: search_indexed.c ../reznosql.c ../reznosql.h
	$(CC) $(CFLAGS) -o $@ search_indexed.c ../reznosql.c

test_hashed: test_hashed.c ../reznosql.c ../reznosql.h
	$(CC) $(CFLAGS) -o $@ test_hashed.c ../reznosql.c

clean:
	rm -f search search_indexed test_hashed

.PHONY: all clean
