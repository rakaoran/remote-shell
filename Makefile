all: bin/sshd bin/ssh

bin/sshd: sshd.c
	@mkdir -p bin
	clang -Wall -Wextra -g -O0 sshd.c -o bin/sshd 

bin/ssh: ssh.c
	@mkdir -p bin
	@clang -Wall -Wextra -g -O0 ssh.c -o bin/ssh

run-server: bin/sshd
	@./bin/sshd

run-client: bin/ssh
	@./bin/ssh

clean:
	@rm -rf bin
