CC ?= cc
CFLAGS := -std=gnu89 -O2
SOURCES := eval.c gen.c init.c main.c

sedar: $(SOURCES) global.h zew.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

clean:
	rm -f sedar

.PHONY: clean
