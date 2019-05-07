#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "tictactoeUtil.h"
#include "protocolUtil.h"

#define STARTNUMBER 100
#define RETRYTIME 3
#define MLTTIMEOUT 10

// state machine field
#define HandShake 0
#define Recnnt 1
#define PlayingGame 2
#define WaitingEndAck 3
#define SendEnd 4

#define MC_PORT 1818
#define MC_GROUP "239.0.0.1"

#define ClientFinal 1
#define ServerFinal 2

struct game_info{
    char board[3][3];
    int gameID;
    // time_t second;
    int waitingseq;
    // int retries;
    int statemachine;
    // 0 continue, 1 draw, 2 client win, 3 server win
    int win;
    // 0 no final move, 1 client make a final move, 2 server make a final move;
    int finalRole;
};

static uint8_t role =1;
static uint8_t peerRole = 2;
static int reconnect_snd = 10;
static int tryagain_snd = 4;
int net_tictoctoe_client(int tcp_sd, int udp_sd, struct sockaddr_in *multicast_addr);
int valid_digit(char *ip_str);
int is_valid_ip(char *ip_str);
int multicastGetConnectSD(int udp_sd, struct sockaddr_in *multicast_addr);
int getPiece(int tcp_sd, struct game_info *game, struct game_prtcl *protocol);
void makePiece(int tcp_sd, struct game_info *game, struct game_prtcl *protocol);
int mySendError(int sd, struct game_prtcl *protocol, int err_dscrpt);
int mycheckwin(char board[3][3]);
void printRecvPacket(const struct game_prtcl *protocol);
void init(struct game_info *game);
int connectLocalServer();

int main(int argc, char *argv[]){
    int tcp_sd, udp_sd;
    struct sockaddr_in server_address;
    struct sockaddr_in multicast_addr;
    uint32_t portNumber;
    char serverIP[29];

    if(argc != 3){
        perror("Invalid input, correct command: ./client <portNumber> <remoteIP>");
        exit(1);
    }

    if(getNumFromStr(argv[1], &portNumber) < 0 || portNumber < 1024 || portNumber > 65535){
        perror("Error: Invalid port_number");
        exit(1);
    }

    // server IP
    strcpy(serverIP, argv[2]);
    if(is_valid_ip(argv[2]) < 0){
        perror("Error: Invalid IP"); 
        exit(1);
    }

    // handle broken EPIPE locally
    signal(SIGPIPE, SIG_IGN);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family      = AF_INET;
    server_address.sin_port        = htons(portNumber);
    server_address.sin_addr.s_addr = inet_addr(serverIP);

    // init multicast IP
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(MC_PORT);
    multicast_addr.sin_addr.s_addr = inet_addr(MC_GROUP);

    if( (tcp_sd = setTimeoutSD('T', TIMEOUT) )<0 || (udp_sd = setTimeoutSD('U', MLTTIMEOUT))<0){return -1;}

    printf("start to connect server\n");
    if(connect(tcp_sd, (struct sockaddr *)&server_address, sizeof(struct sockaddr_in)) < 0){
        perror("Warning: Fail to connect server");
        close(tcp_sd);
        if( (tcp_sd = multicastGetConnectSD(udp_sd, &multicast_addr)) <0){
            if((tcp_sd = connectLocalServer()) < 0){
                fprintf(stderr, "Error: No server response\n");
                return -1;
            }
            
        };
    }

    net_tictoctoe_client(tcp_sd, udp_sd, &multicast_addr);
    close(tcp_sd);
    return 0;
}

// one tictoctoe game
int net_tictoctoe_client(int tcp_sd, int udp_sd, struct sockaddr_in *multicast_addr){
    struct game_info mygame;
    struct game_prtcl protocol;
    init(&mygame);

    for(;;){
    	// Part 1: send phase
    	if(mygame.statemachine == HandShake){
    	    // memset(&protocol, 0, sizeof(protocol));
        	protocol.version  = (uint8_t) VERSION;
        	protocol.gametype = NEWGAME;
        	protocol.seqNum   = (uint8_t)STARTNUMBER;
        	mygame.waitingseq = (protocol.seqNum+1)%256;

    	}
    	else if(mygame.statemachine == Recnnt){
    	    // memset(&protocol, 0, sizeof(protocol));
        	protocol.version  = (uint8_t) VERSION;
        	protocol.gametype = RECONNECT;
        	protocol.seqNum   = (mygame.waitingseq-1+256)%256;
        	exportBoard(mygame.board, &protocol);    		
    	}
    	else if(mygame.statemachine == PlayingGame){
    		makePiece(tcp_sd, &mygame, &protocol);
    	}
        else if(mygame.statemachine == SendEnd){
            protocol.version  = (uint8_t) VERSION;
            protocol.gametype = END;
            protocol.gamenum  = mygame.gameID;
            protocol.seqNum   = (mygame.waitingseq+1)%256;
            protocol.state    = COMPLETE;
            protocol.dscrpt   = mygame.win;
            // dummy add
            mygame.waitingseq = (mygame.waitingseq+2)%256;
        }
    	else{
    		fprintf(stderr, "Error: Wrong Logic In Send\n");
    		return -1;
    	}

    	if(sendMoveTCP(tcp_sd, &protocol)<0){
            fprintf(stderr, "Warning: fail to send.\n");
    		if((tcp_sd = multicastGetConnectSD(udp_sd, multicast_addr))<0){
    			if((tcp_sd = connectLocalServer()) < 0)
                    return -1;
    		}
            fprintf(stderr, "Info: Success to reconnect\n");
			mygame.statemachine = mygame.statemachine==HandShake? HandShake : Recnnt;
			continue;
    	}
        else{
            if(mygame.statemachine == SendEnd){
                fprintf(stderr, "Info: Send End Successfully\n");
                break;
            }
            else if(mygame.finalRole == ServerFinal){
                fprintf(stderr, "Info: Send Reconnect taking the role of END command. Exit Successfully\n");
                break;               
            }
        }

    	// Part2: recv phase
        if(readMoveTCP(tcp_sd, &protocol)<0){
            // server fail, multicast
            fprintf(stderr, "Warning: fail to read the opponent's move.\n");
            if((tcp_sd = multicastGetConnectSD(udp_sd, multicast_addr))<0){
                if((tcp_sd = connectLocalServer()) < 0)
                    return -1;
            }
            fprintf(stderr, "Info: Success to reconnect\n");
            mygame.statemachine = mygame.statemachine==HandShake? HandShake : Recnnt;
            continue;
        }

        printRecvPacket(&protocol);

        if(protocol.version != VERSION){
            fprintf(stderr, "Error: expected version is %d, but received version is %d. Drop this packet\n", VERSION, (int)protocol.version);
            mySendError(tcp_sd, &protocol, MALFORM);
            return -1;
        }

        if(protocol.seqNum != mygame.waitingseq){
            fprintf(stderr, "Error: I am waiting seq %d, but receive an unexpected seq %d\n", mygame.waitingseq, protocol.seqNum);
            mySendError(tcp_sd, &protocol, MALFORM);
            return -1;
        }

        if(protocol.state > GAMEERROR){
            fprintf(stderr, "Error: Invalid received state: %d\n", (int)protocol.state);
            mySendError(tcp_sd, &protocol, MALFORM);
            return -1;
        }  

        if(mygame.statemachine == HandShake){
            mygame.gameID = protocol.gamenum;
        	if(protocol.gametype != NEWGAME){
                fprintf(stderr, "Error: Invalid game command %d while I am waiting for the reply to new game request\n", (int)protocol.gametype);
                mySendError(tcp_sd, &protocol, MALFORM);
                return -1;
            }
            if(protocol.state == GAMECONTINUED){
                mygame.statemachine = PlayingGame;
                print_board(mygame.board);
            }
            else if(protocol.state == GAMEERROR){
                if(protocol.dscrpt == TRYAGAIN){
                    fprintf(stderr, "Warning: Received error descriptor is 5, thus automatically try again after %ds\n", tryagain_snd);
                    sleep(tryagain_snd);
                    // continue;
                }
                else if(protocol.dscrpt == NORESOURCE){
                    fprintf(stderr, "Warning: out of resource. Trying to request again after %ds\n", reconnect_snd);
                    sleep(reconnect_snd);
                    // continue;
                }
                else{
                    fprintf(stderr, "Error: Fail to new game due to unexpected modifier %d\n", (int)protocol.dscrpt);
                    mySendError(tcp_sd, &protocol, MALFORM);
                    return -1;             
                }
            }
            else{
                fprintf(stderr, "Error: Invalid state field %d in the reply to new game request\n", (int)protocol.state);
                mySendError(tcp_sd, &protocol, MALFORM);
                return -1;
            }
        }
        else if(mygame.statemachine == Recnnt){
            // if(protocol.gametype != RECONNECT){
            //     fprintf(stderr, "Error: Invalid game command %d while I am waiting for the reply to reconnect request\n", (int)protocol.gametype);
            //     mySendError(tcp_sd, &protocol, MALFORM);
            //     return -1;
            // }
            if(protocol.state == GAMEERROR){
                if(protocol.dscrpt == TRYAGAIN){
                    fprintf(stderr, "Warning: Received error descriptor is 5, thus automatically try again after %ds\n", tryagain_snd);
                    sleep(tryagain_snd);
                    // continue;
                }
                else if(protocol.dscrpt == NORESOURCE){
                    fprintf(stderr, "Warning: out of resource. Trying to request again after %ds\n", reconnect_snd);
                    sleep(reconnect_snd);
                    // continue;
                }
                else{
                    fprintf(stderr, "Warning: Server's error modifier %d\n, halt game", (int)protocol.dscrpt);
                    return -1;             
                }
            }
            else if(protocol.state == GAMECONTINUED || protocol.state == COMPLETE){
                mygame.gameID = protocol.gamenum;
                if(mygame.finalRole == ClientFinal){
                    if(protocol.gametype != END || protocol.state != COMPLETE || protocol.dscrpt != mygame.win){
                        fprintf(stderr, "Error: Client win, but server' command %d,  state %d and modifier %d don't match. Exit anyway\n", 
                            (int) protocol.gametype, (int) protocol.state, (int) protocol.dscrpt);
                        // mySendError(tcp_sd, &protocol, MALFORM);
                        return -1;
                    }
                    else{
                        printf("Success. Receive Client Win Flag\n");
                        break;
                    }
                }
                else if(mygame.finalRole == ServerFinal){
                    // if(protocol.state != COMPLETE || protocol.dscrpt != mygame.win){
                    //     fprintf(stderr, "Error: Server win, but server'state %d and modifier %d don't match. \nBut client would still send End\n", (int) protocol.state, (int) protocol.dscrpt);
                    // }
                    // mygame.statemachine = SendEnd;
                    fprintf(stderr, "Error: Logic Error\n");
                    return -1;
                }
                else{
                    if(protocol.gametype != MOVE){
                        fprintf(stderr, "Error: Server command %d wrong, expecting MOVE", (int) protocol.gametype);
                        mySendError(tcp_sd, &protocol, MALFORM);
                        return -1;
                    }                    
                    if(getPiece(tcp_sd, &mygame, &protocol)<0){
                        return -1;
                    }
                }
            }
            else{
                fprintf(stderr, "Error: Invalid state field %d\n", (int)protocol.state);
                mySendError(tcp_sd, &protocol, MALFORM);
                return -1;
            }
        }
        else if(mygame.statemachine == PlayingGame){
            if(protocol.gametype != MOVE){
                fprintf(stderr, "Error: Invalid game command %d while I am waiting for the reply to reconnect request\n", (int)protocol.gametype);
                mySendError(tcp_sd, &protocol, MALFORM);
                return -1;
            }
            if(protocol.gamenum != mygame.gameID){
                fprintf(stderr, "Error: Invalid game Id\n");
                mySendError(tcp_sd, &protocol, MALFORM);
                return -1;
            }
            if(protocol.state == GAMEERROR){
                fprintf(stderr, "Halt game since server send an error packet. error modifier %d\n", (int)protocol.dscrpt);
                return -1;
            }
            if(getPiece(tcp_sd, &mygame, &protocol)<0){return -1;}
        }
        else if(mygame.statemachine == WaitingEndAck){
            if(protocol.gamenum==mygame.gameID && protocol.gametype==END && protocol.state==COMPLETE && protocol.dscrpt==mygame.win){
                printf("Success. Receive End Acknowledgement\n");
                break;
            }
            else{
                fprintf(stderr, "Error: Invalid End. Exit anyway.\n");
                return -1;
            }
        }
        else{
            fprintf(stderr, "Error: Wrong Logic In Recv\n");
            return -1;           
        }
    }

    return 0;
}

int multicastGetConnectSD(int udp_sd, struct sockaddr_in *multicast_addr){
    printf("start multicasting\n");
    int i, rc;

    for(i=0; i<RETRYTIME; i++){
        struct sockaddr_in mul_addr= *multicast_addr;
        socklen_t mul_len = sizeof(struct sockaddr_in);

        struct mltcst_send_prtcl sendmsg;
        sendmsg.version = (uint8_t) VERSION;
        sendmsg.command = (uint8_t) ASKADDR;

        rc = sendto(udp_sd, &sendmsg, sizeof(sendmsg), 0, (struct sockaddr*) &mul_addr, mul_len);
        if(rc<=0){
            perror("Error: fail to send msg in multicast");
            continue;
        }
        else if(rc!=sizeof(struct mltcst_send_prtcl)){
            fprintf(stderr, "Error: fail to send entire packet in multicast\n");
            continue;
        }

        // wait some server response
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        socklen_t server_len = sizeof(struct sockaddr_in);
        struct mltcst_recv_prtcl recvmsg;
        memset(&recvmsg, 0, sizeof(recvmsg));

        rc = recvfrom(udp_sd, &recvmsg, sizeof(&recvmsg), 0, (struct sockaddr*)&server_addr, &server_len);
        if(rc<=0){
            perror("Waring: read timeout in multicast");
            fprintf(stderr, "Polling turn = %d\n", i);
            continue;
        }
        else if(rc!=sizeof(struct mltcst_recv_prtcl)){
            fprintf(stderr, "Error: fail to read entire packet in multicast\n");
            continue;
        }
        if(recvmsg.version!=VERSION){
            fprintf(stderr, "Error: version number wrong in multicast\n");
        }
        if(recvmsg.command!=GIVEADDR){
            fprintf(stderr, "Error: command type wrong in multicast\n");
        }
        server_addr.sin_port = recvmsg.port;

        int tcp_sd = setTimeoutSD('T', TIMEOUT);
        if( tcp_sd < 0){return -1;}
        printf("start to connect server in multicast\n");
        if(connect(tcp_sd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) < 0){
            perror("Warning: Fail to connect server in multicast");
            close(tcp_sd);
            continue;
        }
        else{
            return tcp_sd;
        }
    }
    return -1;
}

int connectLocalServer(){
    FILE *fi;
    if((fi = fopen("backupServer", "rb")) == NULL)
    {
        printf("Fail to open backup file. Please check the input file is in current directory.\n");
        return -1;
    }   

    char str[100];
    char serverIP[29];
    int cnt = 0;
    uint32_t portNumber;
    while(fscanf(fi,"%s", str) != EOF){
        if(cnt%2==0){
            if(getNumFromStr(str, &portNumber) < 0){
                portNumber = 0;
            }
        }

        if(cnt%2==1){
            strcpy(serverIP, str);
            if(is_valid_ip(str) < 0 || (portNumber < 1024 || portNumber > 65535)){
                //perror("Error: Invalid IP or port number");
                cnt++; 
                continue;
            }
            struct sockaddr_in server_address;
                // handle broken EPIPE locally
            signal(SIGPIPE, SIG_IGN);

            memset(&server_address, 0, sizeof(server_address));
            server_address.sin_family      = AF_INET;
            server_address.sin_port        = htons(portNumber);
            server_address.sin_addr.s_addr = inet_addr(serverIP);

            int tcp_sd;
            if( (tcp_sd = setTimeoutSD('T', TIMEOUT) )<0){
                cnt++;
                continue;
            }

            printf("start to connect local server\n");
            int rc = connect(tcp_sd, (struct sockaddr *)&server_address, sizeof(struct sockaddr_in));
            if(rc < 0){
                perror("Warning: Fail to connect local server");
                close(tcp_sd);
            }
            else
                return tcp_sd;
        }

        cnt++;
    }
    perror("Warning: no local server to connect");
    fclose(fi);

    return -1;
}

// assumption: server makes a move, use this function to check the protocol field
// If valid, change the state machine
int getPiece(int tcp_sd, struct game_info *game, struct game_prtcl *protocol){
    if(valid(protocol->pos, game->board)<0){
        fprintf(stderr, "Error: Invalid the peer's position %d\n", (int)protocol->pos);
        mySendError(tcp_sd, protocol, MALFORM);
        return -1;
    }
    fprintf(stderr, "Info: Peer is player %d, moves at position: %d\n", peerRole, (int)protocol->pos);
    place(peerRole, protocol->pos, game->board);
    print_board(game->board);

    // check the consistence between the field state and the field pos.
    int win = mycheckwin(game->board);
    if(protocol->state == COMPLETE && win == 0){
        fprintf(stderr, "Error: the peer said the game was completed but it should be continued\n");
        mySendError(tcp_sd, protocol, MALFORM);
        return -1;
    }
    if(win > 0 && protocol->state != COMPLETE){
        fprintf(stderr, "Error: I check that peer's move made the game completed, but the peer didn't report it\n");
        mySendError(tcp_sd, protocol, MALFORM);
        return -1;
    }
    game->win = win;
    if(win>0){
        game->statemachine = SendEnd;
        game->finalRole = ServerFinal;
    }
    else{
        game->statemachine = PlayingGame;
    }
    if(win==SERVERWIN){
        printf("Server win. Try to send END\n");
    }
    else if(win==DRAW){
        printf("Draw. Try to send END\n");
    }

    return 0;
}

void makePiece(int tcp_sd, struct game_info *game, struct game_prtcl *protocol){
    // Part 3: server make a move
    int enterpos;

    printf("I am player %d, please enter a position: ", (int) role);
    scanf("%d", &enterpos);
    // read newline mark
    setbuf(stdin, NULL);
    while(valid((uint8_t)enterpos, game->board)<0){
        fprintf(stderr, "Error: Invalid position %d\n", enterpos);

        printf("Player %d enter a position: ", (int) role);
        scanf("%d", &enterpos);
        setbuf(stdin, NULL);
    }

    place(role, (uint8_t) enterpos, game->board);
    print_board(game->board);

    // check whether the game is already completed
    // if so, release the resource
    int win = mycheckwin(game->board);
    protocol->version = (uint8_t) VERSION;
    protocol->pos = (uint8_t) enterpos;
    protocol->state = (uint8_t) (win<1? GAMECONTINUED : COMPLETE);
    protocol->dscrpt = (uint8_t) win;
    protocol->gametype = (uint8_t) MOVE;
    protocol->gamenum = (uint8_t) game->gameID;
    protocol->seqNum = (uint8_t) (game->waitingseq+1)%256;
    
    // update game information at local
    game->win = win;
    if(win>0){
        game->statemachine = WaitingEndAck;
        game->finalRole = ClientFinal;
    }
    if(win==CLIENTWIN){printf("Client win. Waiting End command\n");}
    else if(win==DRAW){printf("Draw. Waiting End command\n");}
    game->waitingseq = (game->waitingseq+2)%256;
    // game->second = time(NULL);
}

void printRecvPacket(const struct game_prtcl *protocol){
    printf("*** receive packet***\n");
    printf("version=%d, pos=%d, state=%d, modifier=%d, gametype=%d, gamenum=%d, sequence=%d\n",
        (int)protocol->version, (int)protocol->pos, (int)protocol->state, 
        (int)protocol->dscrpt, (int)protocol->gametype, (int)protocol->gamenum, (int)protocol->seqNum);
}

void init(struct game_info *game){
    memset(game, 0, sizeof(struct game_info));
    // game->second = time(NULL);
    game->statemachine = HandShake;
    initSharedState(game->board);
}

int mycheckwin(char board[3][3]){
    return 1+checkwin(board);
}

// if there's some malformed data in the reply to new game or reconnect request
// you have to change the game command before set the state error field 
int mySendError(int sd, struct game_prtcl *protocol, int err_dscrpt){
    protocol->gametype = MOVE;
    return sendErrorTCP(sd, protocol, err_dscrpt);
}

/* return 1 if string contain only digits, else return 0 */
int valid_digit(char *ip_str){
    if(strlen(ip_str)>3){return -1;}
    while (*ip_str) { 
        if (*ip_str < '0' || *ip_str > '9'){
            return -1;
        }
        ++ip_str;
    } 
    return 0; 
} 

/* return 0 if IP string is valid, else return -1 */
int is_valid_ip(char *ip_str){ 
    int num, dots = 0; 
    char *ptr; 
    if (ip_str == NULL){return -1; }
    // See following link for strtok() 
    // http://pubs.opengroup.org/onlinepubs/009695399/functions/strtok_r.html 
    ptr = strtok(ip_str, ".");
    while (ptr!=NULL) {
        /* after parsing string, it must contain only digits */
        if (valid_digit(ptr)<0){return -1;}
        num = atoi(ptr); 
        if(num < 0 || num > 255){return -1;}
        /* parse remaining string */
        ptr = strtok(NULL, "."); 
        if (ptr != NULL){ ++dots; }
    }
    return dots==3? 0: -1; 
}

// int sendNewPacket(int sd, struct game_info *game, struct game_prtcl *protocol){
//     // printf("*** Send Packet ***\n");
//     // printf("tcp_sd %d\n", sd);
//     // printf("version=%d, pos=%d, state=%d, descriptor=%d, gametype=%d, gamenum=%d, sequence=%d\n",
//     //     (int)protocol->version, (int)protocol->pos, (int)protocol->state, 
//     //     (int)protocol->dscrpt, (int)protocol->gametype, (int)protocol->gamenum, (int)protocol->seqNum);

//     game->waitingseq = (protocol->seqNum+1)%256;
//     game->second = time(NULL);
//     // copy(&(game->cache), protocol);

//     // printf("*** Game Info ***\n");
//     // printf("seq=%d, time=%d, waitingseq=%d, statemachine=%d\n", 
//     //     (int)game->seq, (int)game->second, (int)game->waitingseq, (int)game->statemachine);
//     return sendMoveTCP(sd, protocol);
// }