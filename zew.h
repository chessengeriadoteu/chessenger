

#define BIALE 1
#define CZARNE 0
#define PUSTE_POLE 0
#define KONIEC_WSP 8

#define BPION 1
#define BSKOCZEK 2
#define BGONIEC 3
#define BWIEZA 4
#define BHETMAN 5
#define BKROL 6

#define CZPION 11
#define CZSKOCZEK 12
#define CZGONIEC 13
#define CZWIEZA 14
#define CZHETMAN 15
#define CZKROL 16


extern char posprzeciwnika[4+1] ;

extern int kolor ;

extern int szachownica [8][8] ;

extern int posuniecia [20][1000] ;
extern int obca_bierka [20][1000] ;

extern int zlokalizowane [97] ;


int konwertwsp (char) ;
int eval (void) ;
int gen (int) ;
void init (void) ;
