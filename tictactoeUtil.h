//https://stackoverflow.com/questions/8108634/global-variables-in-header-file
extern int ROWS;
extern int COLUMNS;

int checkwin(char board[ROWS][COLUMNS]);
void print_board(char board[ROWS][COLUMNS]);
int initSharedState(char board[ROWS][COLUMNS]);
int valid(int pos, char board[ROWS][COLUMNS]);
void place(int role, int pos, char board[ROWS][COLUMNS]);
int aimove(char board[ROWS][COLUMNS]);