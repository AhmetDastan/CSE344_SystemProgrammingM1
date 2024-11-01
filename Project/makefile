All: compile run clean 

compile: server/server.c client/client.c
	@gcc server/server.c -o serverExe
	@gcc client/client.c -o clientExe

run:  
	./serverExe neHosServer 1 & ./clientExe Connect 10

clean: 
	@rm -f *.txt exe
