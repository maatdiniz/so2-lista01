CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
TARGETS = ex01 ex02 ex03 ex04 ex05 ex06 ex07 ex08 ex09 ex10

.PHONY: all clean run

all: $(TARGETS)

ex%: ex%.c
	$(CC) $(CFLAGS) -o $@ $<

run: all
	./ex01 3
	./ex02
	./ex03
	./ex04
	printf "104729\n1299709\n15485863\n32452843\n49979687\n67867967\n86028121\n104395303\n" | ./ex05 4
	./ex06
	./ex07
	./ex08
	./ex09
	./ex10

clean:
	rm -f $(TARGETS) pipeline_output.csv dados_ex06.bin

