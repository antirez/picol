picol: picol.c
	$(CC) -o picol -O2 -Wall picol.c

test: picol_test.c
	$(CC) -o picol_test -O2 -Wall picol_test.c
	./picol_test

clean:
	rm -f picol picol_test
