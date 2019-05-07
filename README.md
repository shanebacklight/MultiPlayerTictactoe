# Team memeber
Zepiao Han

Xuanyi Li


# File structure
*tictactoeServer.c* and *tictactoeClient.c* are source C files.  
*makefile* the file to compile and run C program.  
*prtocolUtilUDP.c* and *protocolUtilUDP.h* are utility to handle the protocol.  
*tictactoeUtil.h* and *tictactoeUtil.c* are utility to handle the game board.  

# Functionality

Multiple servers play with multiple client, including three major phases: init handshake (game creation or reconnection), play, end handshake (acknowledge win or draw).

Fault tolerant to server crash: clients could find available servers and seamlessly recover from the interrupted point.

Servers validate the packet, evict out of time games, reject overwhelmed request of game creation and reconnection.

Clients make pollings to find available servers.

# Claim
*MAX GAME* = 3. (Macro Name: MAXGAME, Location tictactoeServer.c)

*port number* should falls into the range [1024, 65535].  

*tcp timeout unit* is set to 15s for game play. (Global Variable Name: TIMEOUT, Location: protocolUtil.c)

*multicast timeout* is set to 10s and *polling times* is set to 3 at client end. (Macro Name: MLTTIMEOUT, RETRYTIMES, Location: tictactoeClient.c)

Multicast Address: *MC_PORT* = 1818, *MC_GROUP* = 239.0.0.1


# Run Instruction
Step 1 Initialize
```
make clean
```

Step 2: Compile all source codes
```
make
```

Step 3: Get server ip address at **Server side**.
```
hostname -I
```

Step 4: Start server program at **Server side**.
```
./server <server port>
```

Step 5: Start one or more client programs to send move at **Client side**.
```
./client <server port> <server ip>
```

Step 6: Stop the server by typing *Ctrl c* at **Server side**.
