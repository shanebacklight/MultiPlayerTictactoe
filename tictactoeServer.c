#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include "tictactoeUtil.h"
#include "protocolUtil.h"

#define MAXGAMES 3

#define MC_PORT 1818
#define MC_GROUP "239.0.0.1"

// state machine field
#define PlayingGame 0
#define WaitingEndAck 1
#define SendEnd 2

struct game_info{
    char board[3][3];
    // In the local end, seq is zero based. But when return back to the user, it is translated to 1 based.
    int seq;
    int connectSD;
    time_t second;
    int waitingseq;
    // int retries;
    int statemachine;
    // struct game_prtcl cache;
};

int net_tictoctoe_server(int sd);
void initGame(struct game_info *game, int seq);
void resetGame(struct game_info *game);
int communicate(int sd, struct game_info *games);
void printRecvPacket(const struct game_prtcl *protocol, int connect_sd);
void findresourceTCP(struct game_info *games, int sd, int *idleslot, int *sameaddr);
int sameinetaddrTCP(struct game_info *game, int connect_sd);
void sendNewPacket(int sd, struct game_info *game, struct game_prtcl *protocol);
void makePiece(struct game_info *game, struct game_prtcl *protocol);
int mycheckwin(char board[3][3]);
void multicast(void *arg);
int hasResource(struct game_info *games);

static uint8_t role = 2;
static uint32_t port_number;

int main(int argc, char* argv[]){
    if(argc != 2){
        perror("Invalid input, expected command: ./server <port>");
        exit(1);
    }


    if(getNumFromStr(argv[1], &port_number) < 0 || port_number < 1024 || port_number > 65535){
        perror("Error: Invalid port_number");
        exit(1);
    }

    // socket descriptor, ipv4, tcp, default protocol
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if(sd<0){
        perror("socket creation failed"); 
        exit(1);
    }

    // handle broken EPIPE locally
    signal(SIGPIPE, SIG_IGN);

    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Fail to set time out");
        exit(1);
    }

    // set address
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_number);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    int option =1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) < 0){
        perror("Fail to set the option of reuse port");
        exit(1);
    }

    // bind socket descriptor with assinged address
    if(bind(sd, (struct sockaddr *)&server_address, sizeof(server_address))<0){
        perror("Fail to bind()");
        exit(1);
    }
    if(listen(sd, MAXGAMES)){
        perror("Fail to listen()");
        exit(1);
    }

    net_tictoctoe_server(sd);
    close(sd);

    return 0;
}


// one tictoctoe game
int net_tictoctoe_server(int sd){
	pthread_t t;
    struct sockaddr_in their_address;
    socklen_t theirLen = sizeof(struct sockaddr_in);
    fd_set socketFDS;
    struct game_info games[MAXGAMES];
    memset(games, 0, sizeof(games));
    int i, maxSD = sd;
    // int i;

    for(i=0; i<MAXGAMES; i++){
        initGame(&(games[i]), i+1);
    }

    pthread_create(&t, NULL, (void *)&multicast, games);

    for(;;){

      //  autoresend(sd, games);

        printf("\n--------------- Waiting Game Play request---------------\n");
        // Part1: read protocol field from the socket

        memset(&their_address, 0, sizeof(their_address));
        // don't forget to initialize theirLen at least >= sizeof(sockaddr_in), otherwise, their_address would be truncated
        theirLen = sizeof(struct sockaddr_in);

        FD_ZERO(&socketFDS);
        FD_SET(sd, &socketFDS);
        maxSD = sd;
        for(i=0 ; i<MAXGAMES ; i++){
            // printf("games[%d].connectSD=%d\n", i, games[i].connectSD);
            if(games[i].connectSD > 0){
                FD_SET(games[i].connectSD, &socketFDS);
                if(games[i].connectSD > maxSD){
                    maxSD = games[i].connectSD;
                }
            }
        }

        select(maxSD+1, &socketFDS, NULL, NULL, NULL);

        // if sd is set, that is new connection from client
        if(FD_ISSET(sd, &socketFDS)){
            int connectSD = accept(sd, (struct sockaddr *) &their_address, &theirLen);
            // printf("hahahasd = %d\n", connectSD);
            communicate(connectSD, games);
        }
        
        // if clientsd is set, that is receiving client message
        // printf("move\n");
        for(i=0 ; i<MAXGAMES ; i++){
            if(games[i].connectSD>0 && FD_ISSET(games[i].connectSD, &socketFDS)){
                communicate(games[i].connectSD, games);
            }
        }
    }

    return 0;
}

int communicate(int sd, struct game_info *games){
    struct game_prtcl protocol;
    uint8_t peerRole = 3-role; 
    // -1:continue, 0: draw, 1: someone win
    int rc, win;

    rc = readMoveTCP(sd, &protocol);
    if(rc !=0 ){
        fprintf(stderr, "Warning: fail to read peer's request\n");
        int j;
        for(j=0;j<MAXGAMES;j++){
            if(sd == games[j].connectSD){
                resetGame(&games[j]);
            }
        }
        close(sd);
        return -1;
    }

    printRecvPacket(&protocol, sd);

    // Part2: check the field version

    // 2.1 First of all, check version 
    if(protocol.version != VERSION){
        fprintf(stderr, "Error: expected version is %d, but received version is %d. Drop this packet\n", VERSION, (int)protocol.version);
        return -1;
    }

    // 2.2 check by gametype
    if(protocol.gametype==NEWGAME || protocol.gametype == RECONNECT){
        // handle newmove request
        int idleindex, sameaddr;

        findresourceTCP(games, sd, &idleindex, &sameaddr);
        if(idleindex<0){
        	if(protocol.gametype == RECONNECT){protocol.gametype = MOVE;}
            if(sameaddr>=0){
                fprintf(stderr, "Warning: NewGame or Reconnect occurs during the game\n");
                sendErrorTCP(sd, &protocol, TRYAGAIN);
            }
            else{
                fprintf(stderr, "Warning: Out of resources\n");
                sendErrorTCP(sd, &protocol, NORESOURCE);
            }
            return 0;
        }

        fprintf(stderr, "Info: find a resource, game number is %d\n", idleindex+1);
        struct game_info *game = &games[idleindex];

        //send packet
        //set protocol fields and game state machine first;
        protocol.gamenum = (uint8_t)(idleindex+1);
        protocol.seqNum = (uint8_t)(protocol.seqNum+1)%256;
        if(protocol.gametype==NEWGAME){
            fprintf(stderr, "Info: Accept a newgame request and allocate board\n");
            protocol.state = GAMECONTINUED; 
            protocol.dscrpt = 0;
            game->statemachine = PlayingGame;    
        }
        else if(protocol.gametype==RECONNECT){
            importBoard(&protocol, game->board);
            fprintf(stderr, "Info: Accept a rcnnct request and allocate board\n");
            print_board(game->board);
            win = mycheckwin(game->board);
            if(win==0){
                protocol.gametype = MOVE;
                makePiece(game, &protocol);
            }
            else if(win==SERVERWIN){
                resetGame(game);
                return 0;
            }
            else if(win==CLIENTWIN){
                protocol.gametype = END;
                protocol.state = COMPLETE; 
                protocol.dscrpt = CLIENTWIN;
                game->statemachine = SendEnd;
            }
            else if(win==DRAW){
                // since the board size is 3*3 and client plays first, only client could make a draw move
                protocol.gametype = END;
                protocol.state = COMPLETE;
                protocol.dscrpt = DRAW;
                game->statemachine = SendEnd;
            }
            else{
                fprintf(stderr, "Logic Error\n");
                return -1;
            }
        }
        else {
            fprintf(stderr, "Logic Error once receiving newgame or reconnect.\n"); 
            return -1;
        }

        game->waitingseq = (protocol.seqNum+1)%256;
        game->second = time(NULL);  
        rc = sendMoveTCP(game->connectSD, &protocol);
        if(game->statemachine == SendEnd){
            if(rc!=0){fprintf(stderr, "Fail to send end. Close anyway\n");}
            else{printf("Send End Successfully.\n");}
            resetGame(game);
        }
        return rc!=0? -1 : 0;
    }
    else if(protocol.gametype == MOVE || protocol.gametype == END){
        //handle move request

        // check gamenum
        if(protocol.gamenum<1 || protocol.gamenum>MAXGAMES){
            fprintf(stderr, "Error: Invalid Game Number\n");
            sendErrorTCP(sd, &protocol, MALFORM);
            return -1;
        }

        struct game_info *game = &games[protocol.gamenum-1];

        if(!sameinetaddrTCP(game, sd)){
            fprintf(stderr, "Error: Peer's connect_sd is no binded with the game number. Thus peer is timeout or a hacker\n");
            sendErrorTCP(sd, &protocol, CLIENTTIMEOUT);
            return -1;       
        }

        // // lab7 check packet sequence number
        if(protocol.seqNum != game->waitingseq){
            fprintf(stderr, "Error: I am waiting seq %d, but receive an unexpected seq %d\n", game->waitingseq, protocol.seqNum);
            sendErrorTCP(sd, &protocol, MALFORM);
            resetGame(game);
            return 0;
        }

        // lab7, check timeout
        if(time(NULL)-game->second >= TIMEOUT){
            fprintf(stderr, "Error: You are Time out 1min. Clean the game\n");
            sendErrorTCP(sd, &protocol, CLIENTTIMEOUT);
            resetGame(game);
            return -1;
        }

        if(game->statemachine == PlayingGame){
            if(protocol.gametype != MOVE){
                fprintf(stderr, "Error: during the game receive invalid command type %d\n. Close game", (int)protocol.gametype);
                sendErrorTCP(sd, &protocol, MALFORM);
                resetGame(game);
                return -1;
            }
            // check state
            if(protocol.state>GAMEERROR){
                fprintf(stderr, "Error: expected state ranges from 0 to 2, but received state is %d\n", (int)protocol.state);
                sendErrorTCP(sd, &protocol, MALFORM);
                resetGame(game);
                return -1;
            }

            if(protocol.state == GAMEERROR){
                fprintf(stderr, "Error: Halt the current game, since the peer returns an error state. Error descriptor: %d\n", (int)protocol.dscrpt);
                resetGame(game);
                return -1;
            }

            // check pos
            if(valid(protocol.pos, game->board)<0){
                fprintf(stderr, "Error: Invalid the peer's position %d\n", (int)protocol.pos);
                sendErrorTCP(sd, &protocol, MALFORM);
                resetGame(game);
                return -1;
            }
            fprintf(stderr, "Info: Peer is player %d, moves at position: %d\n", peerRole, (int)protocol.pos);
            place(peerRole, protocol.pos, game->board);
            print_board(game->board);

            // check the consistence between the field state and the field pos.
            win = mycheckwin(game->board);
            if(protocol.state == COMPLETE && win == 0){
                fprintf(stderr, "Error: the peer said the game was completed but it should be continued\n");
                sendErrorTCP(sd, &protocol, MALFORM);
                resetGame(game);
                return -1;
            }

            if(win > 0 && protocol.state != COMPLETE){
                fprintf(stderr, "Error: I check that peer's move made the game completed, but the peer didn't report it\n");
                sendErrorTCP(sd, &protocol, MALFORM);
                resetGame(game);
                return -1;
            }

            // has to send a packet, set all protocol fields and game state machine
            protocol.seqNum = (uint8_t)(protocol.seqNum+1)%256;
            if(win >0){
                // peer's move make game completed, thus release current resource
                if(win == DRAW){ printf("Draw. Try to send END\n");}
                else{printf("Client wins. Try to send END\n");}
                protocol.gametype = END;
                protocol.state = COMPLETE;
                protocol.dscrpt = win;
                game->statemachine = SendEnd;
            }
            else{
                // ai move
                makePiece(game, &protocol);
            }

            game->waitingseq = (uint8_t)(protocol.seqNum+1)%256;
            game->second = time(NULL); 
            rc = sendMoveTCP(game->connectSD, &protocol);
            if(game->statemachine == SendEnd){
                if(rc!=0){fprintf(stderr, "Fail to send end. Close anyway\n");}
                else{printf("Send End Successfully.\n");}
                resetGame(game);
            }
            return rc!=0? -1 : 0;
        }

        else if(game->statemachine == WaitingEndAck){
            // lab6
            if(protocol.gametype == END){
                fprintf(stderr, "Success: Receive END, exit\n");
                resetGame(game);
                return 0;
            }
            else{
                sendErrorTCP(sd, &protocol, MALFORM);
                resetGame(game);
                fprintf(stderr, "Error: wait for END command. but receive gametype=%d. Close game\n", (int)protocol.gametype );
                return -1;
            }
        }

        else{
            resetGame(game);
            fprintf(stderr, "Logic Error.\n");
            return -1;
        }
    }

    else{
        // unrecognized command type
        fprintf(stderr, "Error: expected gametype is either 0 or 1, but received gametype is %d\n", (int)protocol.gametype);
        return -1;
    }

    return 0;
}


void initGame(struct game_info *game, int seq){
    memset(game, 0, sizeof(struct game_info));
    game->seq              = seq;
    game->connectSD        = -1;
    initSharedState(game->board);
}

void resetGame(struct game_info *game){
    int fixedseq = game->seq;
    close(game->connectSD);
    memset(game, 0, sizeof(struct game_info));
    game->seq = fixedseq;
    game->connectSD = -1;
    initSharedState(game->board);
}

void printRecvPacket(const struct game_prtcl *protocol, int connect_sd){
    printf("*** peer info ***\n");
    printf("connect_sd %d\n", connect_sd);
    printf("version=%d, pos=%d, state=%d, descriptor=%d, gametype=%d, gamenum=%d, sequence=%d\n",
        (int)protocol->version, (int)protocol->pos, (int)protocol->state, 
        (int)protocol->dscrpt, (int)protocol->gametype, (int)protocol->gamenum, (int)protocol->seqNum);
}


int sameinetaddrTCP(struct game_info *game, int connect_sd){
    return game->connectSD == connect_sd;
    // return ntohl(their_address->sin_addr.s_addr) == game->ip;
}

// find idle resource. If found, set the corresponding resource with ip, port and time.

void findresourceTCP(struct game_info *games, int sd, int *idleslot, int *sameaddr){
    *idleslot = -1;
    *sameaddr = -1;
    int i = 0;

    // skip other perple's game
    while(i<MAXGAMES && !sameinetaddrTCP(&games[i], sd)){
        i++;
    }
    // for(i=0; i<MAXGAMES && !sameinetaddr(&games[i], their_address); i++){
    //     // nop
    //     // printf("1, i=%d, seq=%d, ip=%ld, second=%ld\n", i, games[i].seq, games[i].ip, games[i].second);
    // }
    time_t cur_time = time(NULL);

    // find same address
    if(i<MAXGAMES){
        // printf("2, i=%d, seq=%d, ip=%ld, second=%ld\n", i, games[i].seq, games[i].ip, games[i].second);
        *sameaddr = 1;
        if(cur_time-games[i].second >= TIMEOUT ){
        	//|| games[i].statemachine == SendEnd
            *idleslot = i;
        }
        else{
            // send a new game request in the middle of game
            resetGame(&games[i]);
            return;
        }
    }

    if((*idleslot)<0){
        // skip those game which are not time out
        for(i=0; i<MAXGAMES && games[i].connectSD>=0 && cur_time-games[i].second < TIMEOUT ; i++){
            // nop
            // printf("3, i=%d, seq=%d, ip=%ld, second=%ld\n", i, games[i].seq, games[i].ip, games[i].second);
        }
        if(i<MAXGAMES){
            // printf("find idle slot\n");
            // printf("4, i=%d, seq=%d, ip=%ld, second=%ld\n", i, games[i].seq, games[i].ip, games[i].second);
            *idleslot = i;
        }        
    }


    if((*idleslot)>=0){
        i=*idleslot;
        resetGame(&games[i]);
        games[i].connectSD = sd;
        games[i].second = time(NULL);
    }

}

int mycheckwin(char board[3][3]){
    return 1+checkwin(board);
}

// set game's state machine, protocol's position, state, and modifier
void makePiece(struct game_info *game, struct game_prtcl *protocol){
    int enterpos = aimove(game->board);
    printf("I am player %d, moves at position: %d\n", role, enterpos);

    place(role, (uint8_t) enterpos, game->board);
    print_board(game->board);

    // check whether the game is already completed
    // if so, release the resource
    int win = mycheckwin(game->board);
    if(win>0) {game->statemachine = WaitingEndAck;}
    else {game->statemachine = PlayingGame;}
    if(win==SERVERWIN) {printf("Server wins. Wait End command\n");}
    else if(win==DRAW) {printf("Draw. Wait End command\n");}

    protocol->pos    = (uint8_t) enterpos;
    protocol->state  = (uint8_t)(win==0? GAMECONTINUED : COMPLETE);
    protocol->dscrpt = (uint8_t) win;
}

void multicast(void *arg){
    printf("sub thread for multicasting\n");
    struct ip_mreq mreq;
    struct game_info *games = arg;
    struct sockaddr_in local_addr;
    int sd, rc;

    if((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        perror("Error: Socket creation failed in multicast"); 
        close(sd);
        exit(1); 
    }

    int option =1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) < 0){
        perror("Error: Fail to set the option of reuse port in multicast");
        close(sd);
        exit(1);
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;        
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    local_addr.sin_port = htons(MC_PORT);   

    if(bind(sd, (struct sockaddr*)&local_addr, sizeof(local_addr))<0){
        perror("Error: Fail to bind in multicast");
        close(sd);
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = inet_addr(MC_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))<0){
        perror("error in set opt");
        close(sd);
        exit(1);
    }

    for(;;){
        struct sockaddr_in clientaddr;
        memset(&clientaddr, 0, sizeof(clientaddr));
        socklen_t clientlen = sizeof(struct sockaddr_in);
        struct mltcst_send_prtcl sendmsg;
        memset(&sendmsg, 0, sizeof(sendmsg));

        rc = recvfrom(sd, &sendmsg, sizeof(&recvmsg), 0, (struct sockaddr*)&clientaddr, &clientlen);
        if(rc<0){
            perror("Warning: no multicast request in waiting time gap.");
            continue;
        }
        else if(rc!=sizeof(struct mltcst_send_prtcl)){
            fprintf(stderr, "Error: fail to read entire packet in multicast\n");
            continue;
        }
        if(sendmsg.version!=VERSION){
            fprintf(stderr, "Error: version number wrong in multicast\n");
            continue;
        }
        if(sendmsg.command!=ASKADDR){
            fprintf(stderr, "Error: command type wrong in multicast\n");
            continue;
        }
        if(hasResource(games)<0){
        	fprintf(stderr, "Warning: currently no available resource. No response to the multicast request\n");
        	continue;
        }

        struct mltcst_recv_prtcl recvmsg;
        recvmsg.version = VERSION;
        recvmsg.command = GIVEADDR;
        recvmsg.port = htons(port_number);
        rc = sendto(sd, &recvmsg, sizeof(recvmsg), 0, (struct sockaddr*)&clientaddr, clientlen);
        if(rc<0){
            perror("Error, fail to send multicast reply");
        }
        else if(rc!=sizeof(struct mltcst_recv_prtcl)){
            fprintf(stderr, "Error: fail to send entire packet in multicast\n");
        }
        else{
            fprintf(stderr,"Info: Send tcp port %d to user in multicast\n", (int) port_number);
        }
    }

}

int hasResource(struct game_info *games){
	int i;
	for(i=0; i<MAXGAMES; i++){
		if(games[i].connectSD<0 || time(NULL)-games[i].second>=TIMEOUT){return 0;}
	}
	return -1;
}

// update time and waiting seq. Assumption: protocl is all set
// void sendNewPacket(int sd, struct game_info *game, struct game_prtcl *protocol){

//     // printf("*** Send Packet ***\n");
//     // printf("connect_sd %d\n", sd);
//     // printf("version=%d, pos=%d, state=%d, descriptor=%d, gametype=%d, gamenum=%d, sequence=%d\n",
//     //     (int)protocol->version, (int)protocol->pos, (int)protocol->state, 
//     //     (int)protocol->dscrpt, (int)protocol->gametype, (int)protocol->gamenum, (int)protocol->seqNum);
//     game->waitingseq = (protocol->seqNum+1)%256;
//     game->second = time(NULL);

//     // printf("*** Game Info ***\n");
//     // printf("seq=%d, connectSD=%d, time=%d, waitingseq=%d, statemachine=%d\n", 
//     //     (int)game->seq, (int)game->connectSD, (int)game->second, (int)game->waitingseq, (int)game->statemachine);
//     sendMoveTCP(sd, protocol);
// }
