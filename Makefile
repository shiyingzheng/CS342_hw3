TARGETS=chatroom

chatroom: chatroom.c
	clang -g -pthread -o chatroom chatroom.c

all: $(TARGETS)

clean:
	rm -f $(TARGETS)
