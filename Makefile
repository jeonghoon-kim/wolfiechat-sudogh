all: server client chat auditlog_cli

sfwrite.o: sfwrite.c
	gcc -Wall -Werror -c sfwrite.c

Database.o: Database.c sfwrite.c
	gcc -Wall -Werror -c Database.c sfwrite.c

User.o: User.c sfwrite.c
	gcc -Wall -Werror -c User.c sfwrite.c

Wrapper.o: Wrapper.c sfwrite.c
	gcc -Wall -Werror -c Wrapper.c sfwrite.c

chat: Chat.c Wrapper.o sfwrite.o
	gcc -Wall -Werror -o chat Chat.c Wrapper.o sfwrite.o

server: Server.c Database.o User.o Wrapper.o sfwrite.o
	gcc -Wall -Werror -o server Server.c Database.o User.o Wrapper.o sfwrite.o -pthread -lssl -lcrypto -lsqlite3 

client: Client.c User.o Wrapper.o sfwrite.o
	gcc -Wall -Werror -o client Client.c User.o Wrapper.o sfwrite.o -pthread

auditlog_cli: auditlog_cli.c
	gcc -Wall -Werror -o auditlog_cli auditlog_cli.c

clean:
	rm -f *~ *.o server client chat