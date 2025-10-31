check-xxd:
	@xxd -v > /dev/null 2>&1 || { echo "Missing xxd"; exit 1; };

check-gcc:
	@gcc -v > /dev/null 2>&1 || { echo "Missing gcc"; exit 1; }

static: check-xxd
	@xxd -i config.txt > config.c

build: check-gcc static
	@gcc -Wall -Wextra -pedantic -O3 -o kit main.c config.c
	@rm config.c

install: build
	@mv kit $(HOME)/bin
	@echo "install kit to $(HOME)/bin/kit"

clean:
	rm kit config.c
