
#include "zew.h"

void init()

{

int a, b, c ;

char pozycja[]={ "Ke1Hd1Wh1Wa1Gc1Gf1Sg1Sb1Pa2Pb2Pc2Pd2Pe2Pf2Pg2Ph2ke8hd8wh8wa8gc8gf8sg8sb8pa7pb7pc7pd7pe7pf7pg7ph7!" } ;



for(a=0; a<8; a++)
	for(b=0; b<8; b++)
	szachownica[a][b]=0 ;

for (a=0; a<97; a++)
	zlokalizowane[a]=0 ;


c=0 ;
while (pozycja[c] != '!') {
	switch (pozycja[c])			{
		case 'K':	{
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=BKROL ;
								c=c+3 ;
								break ;	}

		case 'H': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=BHETMAN ;
								c=c+3 ;
								break ;	}

		case 'W':	{
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=BWIEZA ;
								c=c+3 ;
								break ;	}

		case 'G': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=BGONIEC ;
								c=c+3 ;
								break ;	}

		case 'S': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=BSKOCZEK ;
								c=c+3 ;
								break ;	}

		case 'P': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=BPION ;

								c=c+3 ;
								break ;	}

		case 'k':	{
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=CZKROL ;
								c=c+3 ;
								break ;	}

		case 'h': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=CZHETMAN ;
								c=c+3 ;
								break ;	}

		case 'w':	{
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=CZWIEZA ;
								c=c+3 ;
								break ;	}

		case 'g': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=CZGONIEC ;
								c=c+3 ;
								break ;	}

		case 's': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=CZSKOCZEK ;
								c=c+3 ;
								break ;	}

		case 'p': {
								a=konwertwsp(pozycja[c+1]) ;
								b=konwertwsp(pozycja[c+2]) ;
								szachownica[a][b]=CZPION ;
								c=c+3 ;
								break ;	}

	}
 }


}



