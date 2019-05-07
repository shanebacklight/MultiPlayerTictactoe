/**********************************************************/
/* Extend basic functions of TictactoeOriginal.c */
/**********************************************************/

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include "tictactoeUtil.h"

int ROWS=3;
int COLUMNS=3;

// int main(int argc, char const *argv[])
// {
// 	/* code */
// 	return 0;
// }

// client win: 1, server win:2
int checkwin(char board[ROWS][COLUMNS]){
  /************************************************************************/
  /* brute force check to see if someone won, or if there is a draw       */
  /* return a 0 if the game is 'over' and return -1 if game should go on  */
  /************************************************************************/
  if (board[0][0] == board[0][1] && board[0][1] == board[0][2] ) // row matches
    return board[0][0]=='X'? 1 : 2;
        
  else if (board[1][0] == board[1][1] && board[1][1] == board[1][2] ) // row matches
    return board[1][0]=='X'? 1 : 2;
        
  else if (board[2][0] == board[2][1] && board[2][1] == board[2][2] ) // row matches
    return board[2][0]=='X'? 1 : 2;
        
  else if (board[0][0] == board[1][0] && board[1][0] == board[2][0] ) // column
    return board[0][0]=='X'? 1 : 2;
        
  else if (board[0][1] == board[1][1] && board[1][1] == board[2][1] ) // column
    return board[0][1]=='X'? 1 : 2;
        
  else if (board[0][2] == board[1][2] && board[1][2] == board[2][2] ) // column
    return board[0][2]=='X'? 1 : 2;
        
  else if (board[0][0] == board[1][1] && board[1][1] == board[2][2] ) // diagonal
    return board[0][0]=='X'? 1 : 2;
        
  else if (board[2][0] == board[1][1] && board[1][1] == board[0][2] ) // diagonal
    return board[2][0]=='X'? 1 : 2;
        
  else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
	   board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' && 
	   board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')

    return 0; // Return of 0 means draw
  else
    return  - 1; // return of -1 means keep playing
}


void print_board(char board[ROWS][COLUMNS]){
  /*****************************************************************/
  /* brute force print out the board and all the squares/values    */
  /*****************************************************************/

  printf("\tCurrent TicTacToe Game\n");

  printf("Player 1 (X)  -  Player 2 (O)\n");

  printf("     |     |     \n");
  printf("  %c  |  %c  |  %c \n", board[0][0], board[0][1], board[0][2]);

  printf("_____|_____|_____\n");
  printf("     |     |     \n");

  printf("  %c  |  %c  |  %c \n", board[1][0], board[1][1], board[1][2]);

  printf("_____|_____|_____\n");
  printf("     |     |     \n");

  printf("  %c  |  %c  |  %c \n", board[2][0], board[2][1], board[2][2]);

  printf("     |     |     \n\n");
}

int initSharedState(char board[ROWS][COLUMNS]){    
  /* this just initializing the shared state aka the board */
  int i, j, count = 1;
  for (i=0;i<3;i++)
    for (j=0;j<3;j++){
      board[i][j] = count + '0';
      count++;
    }

  return 0;
}

int valid(int pos, char board[ROWS][COLUMNS]){
    if(pos<1 || pos>9){
        return -1;
    }
    int row = (pos-1)/3;
    int col = (pos-1)%3;
    if(board[row][col] != pos+'0'){
        return -1;
    }
    return 0;
}

// role:{1:"client", 2:"server"}
// denote client's piece as 'X', server's piece as 'O'
void place(int role, int pos, char board[ROWS][COLUMNS]){
    int row = (pos-1)/3;
    int col = (pos-1)%3;
    board[row][col] = role==1? 'X' : 'O';
}

int aimove(char board[ROWS][COLUMNS]){
    int count=1;
    int i, j;
    for (i=0;i<3;i++){
        for (j=0;j<3;j++){
            if(board[i][j] == (char)(count + '0')){
                return count;
            }
            count++;
        }
    }
    return -1;
}
