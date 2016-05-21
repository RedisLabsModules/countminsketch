all: librmutil countminsketch 

clean:
	$(MAKE) -C rmutil clean
	$(MAKE) -C src clean

.PHONY: librmutil
librmutil:
	$(MAKE) -C rmutil

.PHONY: countminsketch
countminsketch:
	$(MAKE) -C src
