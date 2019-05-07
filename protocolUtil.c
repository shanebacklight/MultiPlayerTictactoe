#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include "protocolUtil.h"

#define BUFSIZE 1000
#define PRTCLSIZE 1000

int TIMEOUT       = 15;

int VERSION       = 8;

int NEWGAME       = 0;
int MOVE          = 1;
int END           = 2;
int RECONNECT     = 3;

int GAMECONTINUED = 0;
int COMPLETE      = 1;
int GAMEERROR     = 2;

int NORESOURCE    = 1;
int MALFORM       = 2;
int CLIENTTIMEOUT = 4;
int TRYAGAIN      = 5;

int DRAW          = 1;
int CLIENTWIN     = 2;
int SERVERWIN     = 3;

int ASKADDR = 1;
int GIVEADDR = 2;

void importBoard(struct game_prtcl *protocol, char board[3][3]){
    board[0][0] = protocol->pos1;
    board[0][1] = protocol->pos2;
    board[0][2] = protocol->pos3;
    board[1][0] = protocol->pos4;
    board[1][1] = protocol->pos5;
    board[1][2] = protocol->pos6;
    board[2][0] = protocol->pos7;
    board[2][1] = protocol->pos8;
    board[2][2] = protocol->pos9;
}

void exportBoard(char board[3][3], struct game_prtcl * protocol){
    protocol->pos1 = board[0][0];
    protocol->pos2 = board[0][1];
    protocol->pos3 = board[0][2];
    protocol->pos4 = board[1][0];
    protocol->pos5 = board[1][1];
    protocol->pos6 = board[1][2];
    protocol->pos7 = board[2][0];
    protocol->pos8 = board[2][1];
    protocol->pos9 = board[2][2];
}


int readMoveTCP(int sd, struct game_prtcl *protocol){
    uint8_t buffer[BUFSIZE];
    memset(buffer, 0, sizeof(buffer));

    int rc;

    rc = read(sd, &buffer, 1000);

    if(rc <= 0){
        perror("Warning: fail to recv() due to connectin error");
        return -1;
    }   

    if(rc != 1000){
        fprintf(stderr,"Warning: fail to read the entire packet, size=%d\n", rc);
        return -1;
    }

    memset(protocol, 0, sizeof(struct game_prtcl));
    protocol->version   = buffer[0];
    protocol->pos       = buffer[1];
    protocol->state     = buffer[2];
    protocol->dscrpt    = buffer[3];
    protocol->gametype  = buffer[4];
    protocol->gamenum   = buffer[5];
    protocol->seqNum    = buffer[6];
    protocol->pos1 = buffer[7];
    protocol->pos2 = buffer[8];
    protocol->pos3 = buffer[9];
    protocol->pos4 = buffer[10];
    protocol->pos5 = buffer[11];
    protocol->pos6 = buffer[12];
    protocol->pos7 = buffer[13];
    protocol->pos8 = buffer[14];
    protocol->pos9 = buffer[15];

    return 0;
}


int sendErrorTCP(int sd, struct game_prtcl *protocol, int err_dscrpt){
    protocol->version = VERSION;
    protocol->state   = GAMEERROR;
    protocol->dscrpt  = err_dscrpt;
    protocol->seqNum++;
    return sendMoveTCP(sd, protocol);
}

int sendMoveTCP(int sd, struct game_prtcl *protocol){
    // use MSG_NOSIGNAL, to avoid the program exit immediately if the connection has been closed.
    // https://cboard.cprogramming.com/c-programming/67822-socket-send-exits-app-unexceptively.html
    // explicitly call signal(SIGPIPE, SIG_IGN) outside instead of MSG_NOSIGNAL, some platfrom doens't support MSG_NOSIGNAL
    // if(send(connect_sd, &version, sizeof(uint8_t), MSG_NOSIGNAL) <= 0){
    uint8_t buffer[BUFSIZE];
    memset(buffer, 0, sizeof(buffer));
    int rc = 0;
    buffer[0] = protocol->version;
    buffer[1] = protocol->pos;
    buffer[2] = protocol->state;
    buffer[3] = protocol->dscrpt;
    buffer[4] = protocol->gametype;
    buffer[5] = protocol->gamenum;
    buffer[6] = protocol->seqNum;
    buffer[7] = protocol->pos1;
    buffer[8] = protocol->pos2;
    buffer[9] = protocol->pos3;
    buffer[10] = protocol->pos4;
    buffer[11] = protocol->pos5;
    buffer[12] = protocol->pos6;
    buffer[13] = protocol->pos7;
    buffer[14] = protocol->pos8;
    buffer[15] = protocol->pos9;

    rc = write(sd, buffer, BUFSIZE);
    
    if(rc <= 0){
    	perror("Warning: fail to send() due to connection error");
        return -1;
    }

    if(rc != PRTCLSIZE){
        fprintf(stderr, "Error: fail to send entire packet\n");
        return -1;
    }
    
    return 0;
}


int getNumFromStr(char *s, uint32_t *num){
    *num = 0;
    uint32_t tmp = 0;
    while(*s){
        if(*s < '0' || *s > '9'){
            fprintf(stderr, "Error: Invalid number: contains non-digit characters\n");
            return -1;
        }
        tmp = *num;
        *num = ((*s)-'0') + (*num)*10;
        // check overflow
        if((*num - (*s-'0'))/10 != tmp){
            fprintf(stderr, "Error: Number overflow, larger than 2^32-1\n");
            return -1;
        }
        s++;
    }
    return 0;
}

// argc: 'U':UDP, 'T': TCP
// return <0, fail to set
int setTimeoutSD(char argv, int second){
    int sd;
    if(argv!='T' && argv!='U'){
        fprintf(stderr, "Error: argument error in setTimeoutSD\n");
        return -1;
    }
    if((sd = socket(AF_INET, argv=='U'? SOCK_DGRAM : SOCK_STREAM, 0)) < 0 ) { 
        perror("Error: Socket creation failed"); 
        return -1; 
    }

    struct timeval tv;
    tv.tv_sec = second;
    tv.tv_usec = 0;
    if(setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Error: Fail to set time out");
        close(sd);
        return -1;
    }

    int option =1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) < 0){
        perror("Fail to set the option of reuse port");
        close(sd);
        return -1;
    }
    return sd;
}


// void copy(struct game_prtcl *dst, struct game_prtcl *src){
//     dst->version  = src->version;
//     dst->pos      = src->pos;
//     dst->state    = src->state;
//     dst->dscrpt   = src->dscrpt;
//     dst->gametype = src->gametype;
//     dst->gamenum  = src->gamenum;
//     dst->seqNum   = src->seqNum;
// }
