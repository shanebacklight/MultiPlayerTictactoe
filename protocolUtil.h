struct game_prtcl{
	uint8_t version;
	uint8_t pos;
	uint8_t state;
	uint8_t dscrpt;
	uint8_t gametype;
	uint8_t gamenum;
    uint8_t seqNum;
    uint8_t pos1;
    uint8_t pos2;
    uint8_t pos3;
    uint8_t pos4;
    uint8_t pos5;
    uint8_t pos6;
    uint8_t pos7;
    uint8_t pos8;
    uint8_t pos9;
};

struct mltcst_send_prtcl{
    uint8_t version;
    uint8_t command;
};

struct mltcst_recv_prtcl{
    uint8_t version;
    uint8_t command;
    // network order
    uint16_t port;
};

extern int VERSION;
extern int TIMEOUT;
extern int NEWGAME;
extern int MOVE;
extern int END;
extern int RECONNECT;
extern int GAMECONTINUED;
extern int COMPLETE;
extern int GAMEERROR;
extern int NORESOURCE;
extern int MALFORM;
extern int CLIENTTIMEOUT;
extern int TRYAGAIN;
extern int DRAW;
extern int CLIENTWIN;
extern int SERVERWIN;
extern int ASKADDR;
extern int GIVEADDR;

void importBoard(struct game_prtcl *protocol, char board[3][3]);
void exportBoard(char board[3][3], struct game_prtcl * protocol);
int readMoveTCP(int sd, struct game_prtcl *protocol);
int sendErrorTCP(int sd, struct game_prtcl *protocol, int err_dscrpt);
int sendMoveTCP(int sd, struct game_prtcl *protocol);
int getNumFromStr(char *s, uint32_t *num);
int setTimeoutSD(char argv, int second);