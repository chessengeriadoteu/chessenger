
#include <stdio.h>
#include "zew.h"
#include "global.h"


int alphabeta (int, int, int) ;
int wykonaj_ruch (int, int) ;
int cofnij_ruch (int, int) ;
char konwertkolumna (int) ;
char konwertwiersz (int) ;
void pokaz_szachownice (void) ;
int wez_i_ustaw_ruch (void) ;


int tot, best, best1, best2, best3, best4 ;

int main()
{

extern int tot, best, best1, best2, best3, best4 ;

int sila_pozycji ;


init () ;

kolor=CZARNE ;


if (kolor == BIALE)
	printf("\n\tmoj kolor to BIALE\n\n") ;
else
	printf("\n\tmoj kolor to CZARNE\n\n") ;


poczatek:

if (wez_i_ustaw_ruch() == 1)
 return 0 ;

tot=1 ;
sila_pozycji=alphabeta(5, -10000, 10000) ;

if (tot==0) {
	tot=1 ;
}
wykonaj_ruch(5,tot) ;

pokaz_szachownice() ;



switch(best) {

	case 1: case 11:
	printf(" pion %c%c-%c%c", konwertkolumna(best1),konwertwiersz(best2),konwertkolumna(best3),konwertwiersz(best4)) ;
	break ;

	case 2: case 12:
	printf(" skoczek %c%c-%c%c", konwertkolumna(best1),konwertwiersz(best2),konwertkolumna(best3),konwertwiersz(best4)) ;
	break ;

	case 3: case 13:
	printf(" goniec %c%c-%c%c", konwertkolumna(best1),konwertwiersz(best2),konwertkolumna(best3),konwertwiersz(best4)) ;
	break ;

	case 4: case 14:
	printf(" wieza %c%c-%c%c", konwertkolumna(best1),konwertwiersz(best2),konwertkolumna(best3),konwertwiersz(best4)) ;
	break ;

	case 5: case 15:
	printf(" hetman %c%c-%c%c", konwertkolumna(best1),konwertwiersz(best2),konwertkolumna(best3),konwertwiersz(best4)) ;
	break ;

	case 6: case 16:
	printf(" krol %c%c-%c%c", konwertkolumna(best1),konwertwiersz(best2),konwertkolumna(best3),konwertwiersz(best4)) ;
	break ;
}

	printf(", wartosc pozycji=%d ", sila_pozycji) ;

goto poczatek ;


}


int alphabeta (glebokosc, alpha, beta)
{

extern int tot ;
int wartosc_pozycji ;
int t ;
int ilosc_posuniec ;

if (glebokosc == 0)
	return eval() ;

ilosc_posuniec=gen(glebokosc) ;

for (t=1; t<=ilosc_posuniec; t++)  {
	wykonaj_ruch(glebokosc, t) ;
	kolor=!kolor ;
	wartosc_pozycji = -alphabeta(glebokosc-1, -beta, -alpha) ;
	kolor=!kolor ;
	cofnij_ruch(glebokosc, t) ;

	if (wartosc_pozycji > alpha)  {

		if (glebokosc == 5)
			tot=t ;

		if (wartosc_pozycji >= beta)
		return beta ;

	 alpha=wartosc_pozycji ;
	}

}

return alpha ;
}


int wykonaj_ruch (glebokoscc, tt)  {

extern int best, best1, best2, best3, best4 ;
int ktore, a ;
int moja_bierka, miejscex, miejscey ;
int x, y ;
ktore=0 ;
a=0 ;

while (posuniecia[glebokoscc][a] != -1) {
	moja_bierka=best=posuniecia[glebokoscc][a] ;
	miejscex=best1=posuniecia[glebokoscc][a+1] ;
  miejscey=best2=posuniecia[glebokoscc][a+2] ;

  ++ktore ;
  if (ktore == tt)  {
    a=a+3 ;
    goto loop3 ;
  }

  a=a+5 ;


  while (posuniecia[glebokoscc][a] != KONIEC_WSP) {
    ++ktore ;
    if (ktore == tt)
      goto loop3 ;
    a=a+2 ;
  }

  ++a ;
}

loop3:

y=best4=posuniecia[glebokoscc][a+1] ;

x=best3=posuniecia[glebokoscc][a] ;
obca_bierka[glebokoscc][tt]=szachownica[x][y] ;
szachownica[x][y]=moja_bierka ;
szachownica[miejscex][miejscey]=PUSTE_POLE ;
return 0 ;
}


int cofnij_ruch (glebokoscc, tt)  {

int ktore, a ;
int moja_bierka, miejscex, miejscey ;
int x, y ;

ktore=0 ;
a=0 ;

while (posuniecia[glebokoscc][a] != -1) {
  moja_bierka=posuniecia[glebokoscc][a] ;
  miejscex=posuniecia[glebokoscc][a+1] ;
  miejscey=posuniecia[glebokoscc][a+2] ;

  ++ktore ;
  if (ktore == tt)  {
    a=a+3 ;
    goto loop4 ;
  }
  a=a+5 ;

  while (posuniecia[glebokoscc][a] != KONIEC_WSP) {
    ++ktore ;
	 if (ktore == tt)
      goto loop4 ;
    a=a+2 ;
  }

  ++a ;
}

loop4:

y=posuniecia[glebokoscc][a+1] ;

x=posuniecia[glebokoscc][a] ;

  
szachownica[x][y]=obca_bierka[glebokoscc][tt] ;


szachownica[miejscex][miejscey]=moja_bierka ;

return 0 ;
}


char konwertkolumna(int cyfra)
{

switch(cyfra) {

case 0:
return 'a' ;
case 1:
return 'b' ;
case 2:
return 'c' ;
case 3:
return 'd' ;
case 4:
return 'e' ;
case 5:
return 'f' ;
case 6:
return 'g' ;
case 7:
return 'h' ;

}
printf("\n\nBLAD! f-cja konwertkolumna\n\n") ;
return '!' ;
}


char konwertwiersz(int cyfra2)
{

switch(cyfra2) {

case 0:
return '8' ;
case 1:
return '7' ;
case 2:
return '6' ;
case 3:
return '5' ;
case 4:
return '4' ;
case 5:
return '3' ;
case 6:
return '2' ;
case 7:
return '1' ;

}
printf("\n\nBLAD! f-cja konwertwiersz\n\n") ;
return '!' ;
}


void pokaz_szachownice (void)
{

int a, b, c ;

printf("\n\n") ;
c=0 ;

for (a=0; a<8; a++)
	for (b=0; b<8; b++) {
	 ++c ;
	 if (c>8) {
		printf("\n") ;
		c=1 ;
	 }
	 switch(szachownica[b][a]) {
		case 0:
			printf(" .") ;
			break ;
			case 1:
			printf("p.") ;
			break ;
			case 2:
			printf("s.") ;
			break ;
			case 3:
			printf("g.") ;
			break ;
			case 4:
			printf("w.") ;
			break ;
			case 5:
			printf("h.") ;
			break ;
			case 6:
			printf("k.") ;
			break ;

			case 11:
			printf("P.") ;
			break ;
			case 12:
			printf("S.") ;
			break ;
			case 13:
			printf("G.") ;
			break ;
			case 14:
			printf("W.") ;
			break ;
			case 15:
			printf("H.") ;
			break ;
			case 16:
			printf("K.") ;
			break ;
			}
    }
}


int wez_i_ustaw_ruch (void)
{

int aa, xx, yy, xxx ;
char klawisz ;

loop:
printf("\n\n wprowadz swoje posuniecie: ") ;

scanf("%s", posprzeciwnika) ;
if (posprzeciwnika[0] == 'q')  {
  printf ("\n\n czy napewno chcesz zakonczyc partie? t/n  ") ;
  
  do
  klawisz=getchar() ;
  while (klawisz != 't' && klawisz != 'n') ;
  
  if (klawisz == 'n')
   goto loop ; 
  else 
	 return 1 ;
}
 


xx=xxx=konwertwsp(posprzeciwnika[0]) ;
yy=konwertwsp(posprzeciwnika[1]) ;
aa=szachownica[xx][yy] ;
szachownica[xx][yy]=PUSTE_POLE ;
xx=konwertwsp(posprzeciwnika[2]) ;
yy=konwertwsp(posprzeciwnika[3]) ;

if (aa==BPION && szachownica[xx][yy]==PUSTE_POLE && xx!=xxx) {
	szachownica[xx][yy+1]=PUSTE_POLE ;
}
if (aa==CZPION && szachownica[xx][yy]==PUSTE_POLE && xx!=xxx) {
	 szachownica[xx][yy-1]=PUSTE_POLE ;
}

szachownica[xx][yy]=aa ;

if (aa==BPION && yy==0) {
	 szachownica[xx][yy]=BHETMAN ;
}
if (aa==CZPION && yy==7) {
	 szachownica[xx][yy]=CZHETMAN ;
}

return 0 ;
}



