myshell : myshell.o lex.yy.o
	cc myshell.o lex.yy.o -o myshell -lfl

myshell.o : myshell.c
	cc -c myshell.c

lex.yy.o : lex.yy.c
	cc -c lex.yy.c

lex.yy.c : shell.l 
	lex shell.l


clean:
	rm myshell *.o
