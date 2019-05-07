# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall

# the build target executable:
# TARGET = tictactoeOriginal server client

all: tictactoeServer tictactoeClient


tictactoeServer: tictactoeServer.c protocolUtil.c tictactoeUtil.c
	$(CC) $(CFLAGS) tictactoeServer.c protocolUtil.c tictactoeUtil.c -o tictactoeServer -lpthread

tictactoeClient: tictactoeClient.c protocolUtil.c tictactoeUtil.c protocolUtil.h tictactoeUtil.h
	$(CC) $(CFLAGS) tictactoeClient.c protocolUtil.c tictactoeUtil.c -o tictactoeClient

clean:
	$(RM) tictactoeServer tictactoeClient
