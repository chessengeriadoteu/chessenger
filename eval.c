

#include "zew.h"


int eval()
{

int wartoscbierek[]= { 0,100,300,300,500,900,20000,0,0,0,0,100,300,300,500,900,20000 } ;

int hbfigury[8][8]= {
                      {-9,-7,-5,-4,-4,-5,-7,-9},
                      {-6,2,1,0,0,1,2,-6},
                      {-4,3,6,8,8,6,3,-4},
                      {-4,6,9,10,10,9,6,-4},
                      {-5,2,6,7,7,6,2,-5},
                      {-7,1,5,3,3,5,1,-7},
                      {-8,-3,-1,-1,-1,-1,-3,-8},
                      {-10,-9,-8,-7,-7,-8,-9,-10}
                                                } ;


int hbpiona[8][8]= {
                     {891,893,895,896,896,895,893,891},
                     {8,9,9,9,9,9,9,8},
                     {4,6,7,8,8,7,6,4},
                     {0,2,4,6,6,4,2,0},
                     {-4,-2,1,2,2,1,-2,-4},
                     {-8,-6,-4,-2,-2,-4,-6,-8},
                     {-10,-8,-6,-4,-4,-6,-8,-10},
                     {99,99,99,99,99,99,99,99}
                                             } ;



int hczfigury[8][8]={
                      {-10,-9,-8,-7,-7,-8,-9,-10},
                      {-8,-3,-1,-1,-1,-1,-3,-8},
                      {-7,1,5,3,3,7,1,-7},
                      {-5,2,6,7,7,6,2,-5},
                      {-4,6,9,10,10,9,6,-4},
                      {-4,3,6,8,8,6,3,-4},
                      {-6,2,1,0,0,1,2,-6},
                      {-9,-7,-5,-4,-4,-5,-7,-9}
                                              } ;

int hczpiona[8][8]={
                     {99,99,99,99,99,99,99,99},
                     {-10,-8,-6,-4,-4,-6,-8,-10},
                     {-8,-6,-4,-2,-2,-4,-6,-8},
                     {-4,-2,1,2,2,1,-2,-4},
                     {0,2,4,6,6,4,2,0},
                     {4,6,7,8,8,7,6,4},
                     {8,9,9,9,9,9,9,8},
                     {891,893,895,896,896,895,893,891},
                                              } ;


int a, b, smb, smcz ;

smb=smcz=0 ;

for	(a=0; a<8; a++)
  for	(b=0; b<8; b++) {
    if (szachownica[a][b] != PUSTE_POLE)  {
      if (szachownica[a][b] < 11) {

        switch(szachownica[a][b]) {
          case 1:
          smb=smb+wartoscbierek[szachownica[a][b]]+hbpiona[b][a] ;
          break ;

          case 2: case 3: case 4: case 5:
          smb=smb+wartoscbierek[szachownica[a][b]]+hbfigury[b][a] ;
          break ;

          case 6:
          smb=smb+wartoscbierek[szachownica[a][b]] ;
          break ;
        }

      }
      else  {
        switch(szachownica[a][b]) {
          case 11:
          smcz=smcz+wartoscbierek[szachownica[a][b]]+hczpiona[b][a] ;
          break ;

          case 12: case 13: case 14: case 15:
          smcz=smcz+wartoscbierek[szachownica[a][b]]+hczfigury[b][a] ;
          break ;

          case 16:
          smcz=smcz+wartoscbierek[szachownica[a][b]] ;
          break ;
        }
      }
    }
  }



 return smb-smcz ;

}
