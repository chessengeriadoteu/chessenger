

#include "zew.h"


int gen (int depth)
{

int *wskaznik, *wskaznik2 ;

int a, b, c ;
int d, e ;

int ilosc_wygener_pos ;
int czygener ;



wskaznik=&zlokalizowane[0] ;

for(a=0; a<8; a++)
	for(b=0; b<8; b++)
		if (szachownica[a][b] != 0) {
			*wskaznik=szachownica[a][b] ;
			++wskaznik ;
			*wskaznik=a ;
			++wskaznik ;
			*wskaznik=b ;
			++wskaznik ;
	}
*wskaznik=-1 ;


wskaznik=&posuniecia[depth][0] ;
ilosc_wygener_pos=a=0 ;

 while (zlokalizowane[a]>=0)	 {
	if ((kolor == BIALE  &&  zlokalizowane[a]<=6  &&  zlokalizowane[a]>=1)   ||   (kolor == CZARNE  &&  zlokalizowane[a]>=11  &&  zlokalizowane[a]<=16))  {

	switch (zlokalizowane[a]) {


		case BKROL:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

c=c-1 ;
if (c >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
		++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
				++ilosc_wygener_pos ;
			}
	}
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

c=c+1 ;
if (c <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b-1 ;
if (b >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
				++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b+1 ;
if (b <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b-1 ;
c=c-1 ;
if (b>=0 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b+1 ;
c=c-1 ;
if (b<=7 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
	}
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b-1 ;
c=c+1 ;
if (b>=0 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
				++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b+1 ;
c=c+1 ;
if (b<=7 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
		++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


case CZKROL:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

c=c-1 ;
if (c >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

c=c+1 ;
if (c <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b-1 ;
if (b >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b+1 ;
if (b <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b-1 ;
c=c-1 ;
if (b>=0 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b+1 ;
c=c-1 ;
if (b<=7 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b-1 ;
c=c+1 ;
if (b>=0 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

b=b+1 ;
c=c+1 ;
if (b<=7 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


		case BHETMAN:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hgora:
c=c-1 ;
if (c >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hgora ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hdol2:
c=c+1 ;
if (c <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hdol2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hlewo2:
b=b-1 ;
if (b >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
		++ilosc_wygener_pos ;
		goto hlewo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hprawo2:
b=b+1 ;
if (b <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hprawo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hgorailewo:
b=b-1 ;
c=c-1 ;
if (b>=0 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hgorailewo ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hgoraiprawo2:
b=b+1 ;
c=c-1 ;
if (b<=7 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hgoraiprawo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hdolilewo2:
b=b-1 ;
c=c+1 ;
if (b>=0 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hdolilewo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hdoliprawo2:
b=b+1 ;
c=c+1 ;
if (b<=7 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hdoliprawo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
  ++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


case CZHETMAN:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hgora2:
c=c-1 ;
if (c >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
		++ilosc_wygener_pos ;
		goto hgora2 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hdol3:
c=c+1 ;
if (c <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hdol3 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hlewo4:
b=b-1 ;
if (b >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hlewo4 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hprawo5:
b=b+1 ;
if (b <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hprawo5 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hgorailewo7:
b=b-1 ;
c=c-1 ;
if (b>=0 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hgorailewo7 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hgoraiprawo3:
b=b+1 ;
c=c-1 ;
if (b<=7 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hgoraiprawo3 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hdolilewo4:
b=b-1 ;
c=c+1 ;
if (b>=0 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hdolilewo4 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

hdoliprawo6:
b=b+1 ;
c=c+1 ;
if (b<=7 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto hdoliprawo6 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}

		case BWIEZA:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

gora2:
c=c-1 ;
if (c >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto gora2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

dol3:
c=c+1 ;
if (c <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto dol3 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

lewo3:
b=b-1 ;
if (b >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto lewo3 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

prawo3:
b=b+1 ;
if (b <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto prawo3 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
				++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


case CZWIEZA:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

gora:
c=c-1 ;
if (c >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto gora ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

dol9:
c=c+1 ;
if (c <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto dol9 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

lewo9:
b=b-1 ;
if (b >= 0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto lewo9 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

prawo9:
b=b+1 ;
if (b <= 7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto prawo9 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


		case BGONIEC:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

gorailewo:
b=b-1 ;
c=c-1 ;
if (b>=0 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto gorailewo ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

goraiprawo2:
b=b+1 ;
c=c-1 ;
if (b<=7 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto goraiprawo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
				++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

dolilewo2:
b=b-1 ;
c=c+1 ;

if (b>=0 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto dolilewo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

doliprawo2:
b=b+1 ;
c=c+1 ;
if (b<=7 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto doliprawo2 ;
	}
	else {
			if (szachownica[b][c] >10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


case CZGONIEC:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

gorailewo2:
b=b-1 ;
c=c-1 ;
if (b>=0 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto gorailewo2 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

goraiprawo3:
b=b+1 ;
c=c-1 ;
if (b<=7 && c>=0) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
    ++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto goraiprawo3 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

dolilewo3:
b=b-1 ;
c=c+1 ;

if (b>=0 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto dolilewo3 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
        ++wskaznik ;
				*wskaznik=c ;
        ++wskaznik ;
				czygener=1 ;
				++ilosc_wygener_pos ;
			}
  }
}

b=zlokalizowane[a+1] ;
c=zlokalizowane[a+2] ;

doliprawo3:
b=b+1 ;
c=c+1 ;
if (b<=7 && c<=7) {
	if (szachownica[b][c] == PUSTE_POLE) {
		*wskaznik=b ;
		++wskaznik ;
		*wskaznik=c ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		goto doliprawo3 ;
	}
	else {
			if (szachownica[b][c] <10) {
				*wskaznik=b ;
				++wskaznik ;
				*wskaznik=c ;
				++wskaznik ;
				czygener=1 ;
        ++ilosc_wygener_pos ;
			}
  }
}

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


		case BSKOCZEK:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

d=zlokalizowane[a+1] ;
e=zlokalizowane[a+2] ;

if (d+1 <=7 && e-2 >=0) {
	if (szachownica[d+1][e-2] == PUSTE_POLE) {
		*wskaznik=d+1 ;
    ++wskaznik ;
		*wskaznik=e-2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+1][e-2] >10) {
					*wskaznik=d+1 ;
          ++wskaznik ;
					*wskaznik=e-2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d+2 <=7 && e-1 >=0) {
	if (szachownica[d+2][e-1] == PUSTE_POLE) {
		*wskaznik=d+2 ;
		++wskaznik ;
		*wskaznik=e-1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+2][e-1] >10) {
					*wskaznik=d+2 ;
          ++wskaznik ;
					*wskaznik=e-1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d+2 <=7 && e+1 <=7) {
	if (szachownica[d+2][e+1] == PUSTE_POLE) {
		*wskaznik=d+2 ;
    ++wskaznik ;
		*wskaznik=e+1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+2][e+1] >10) {
					*wskaznik=d+2 ;
					++wskaznik ;
					*wskaznik=e+1 ;
          ++wskaznik ;
					czygener=1 ;
					++ilosc_wygener_pos ;
				}
	}
}

if (d+1 <=7 && e+2 <=7) {
	if (szachownica[d+1][e+2] == PUSTE_POLE) {
		*wskaznik=d+1 ;
    ++wskaznik ;
		*wskaznik=e+2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+1][e+2] >10) {
					*wskaznik=d+1 ;
          ++wskaznik ;
					*wskaznik=e+2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-1 >=0 && e+2 <=7) {
	if (szachownica[d-1][e+2] == PUSTE_POLE) {
		*wskaznik=d-1 ;
    ++wskaznik ;
		*wskaznik=e+2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-1][e+2] >10) {
					*wskaznik=d-1 ;
          ++wskaznik ;
					*wskaznik=e+2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-2 >=0 && e+1 <=7) {
	if (szachownica[d-2][e+1] == PUSTE_POLE) {
		*wskaznik=d-2 ;
		++wskaznik ;
		*wskaznik=e+1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-2][e+1] >10) {
					*wskaznik=d-2 ;
          ++wskaznik ;
					*wskaznik=e+1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-2 >=0 && e-1 >=0) {
	if (szachownica[d-2][e-1] == PUSTE_POLE) {
		*wskaznik=d-2 ;
    ++wskaznik ;
		*wskaznik=e-1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-2][e-1] >10) {
					*wskaznik=d-2 ;
					++wskaznik ;
					*wskaznik=e-1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-1 >=0 && e-2 >=0) {
	if (szachownica[d-1][e-2] == PUSTE_POLE) {
		*wskaznik=d-1 ;
    ++wskaznik ;
		*wskaznik=e-2 ;
    ++wskaznik ;
		czygener=1 ;
		++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-1][e-2] >10) {
					*wskaznik=d-1 ;
          ++wskaznik ;
					*wskaznik=e-2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}


if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


case CZSKOCZEK:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

d=zlokalizowane[a+1] ;
e=zlokalizowane[a+2] ;

if (d+1 <=7 && e-2 >=0) {
	if (szachownica[d+1][e-2] == PUSTE_POLE) {
		*wskaznik=d+1 ;
    ++wskaznik ;
		*wskaznik=e-2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+1][e-2] <10) {
					*wskaznik=d+1 ;
          ++wskaznik ;
					*wskaznik=e-2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d+2 <=7 && e-1 >=0) {
	if (szachownica[d+2][e-1] == PUSTE_POLE) {
		*wskaznik=d+2 ;
		++wskaznik ;
		*wskaznik=e-1 ;
    ++wskaznik ;
		czygener=1 ;
		++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+2][e-1] <10) {
					*wskaznik=d+2 ;
          ++wskaznik ;
					*wskaznik=e-1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d+2 <=7 && e+1 <=7) {
	if (szachownica[d+2][e+1] == PUSTE_POLE) {
		*wskaznik=d+2 ;
    ++wskaznik ;
		*wskaznik=e+1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+2][e+1] <10) {
					*wskaznik=d+2 ;
					++wskaznik ;
					*wskaznik=e+1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d+1 <=7 && e+2 <=7) {
	if (szachownica[d+1][e+2] == PUSTE_POLE) {
		*wskaznik=d+1 ;
    ++wskaznik ;
		*wskaznik=e+2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d+1][e+2] <10) {
					*wskaznik=d+1 ;
          ++wskaznik ;
					*wskaznik=e+2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-1 >=0 && e+2 <=7) {
	if (szachownica[d-1][e+2] == PUSTE_POLE) {
		*wskaznik=d-1 ;
    ++wskaznik ;
		*wskaznik=e+2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-1][e+2] <10) {
					*wskaznik=d-1 ;
          ++wskaznik ;
					*wskaznik=e+2 ;
          ++wskaznik ;
					czygener=1 ;
					++ilosc_wygener_pos ;
				}
	}
}

if (d-2 >=0 && e+1 <=7) {
	if (szachownica[d-2][e+1] == PUSTE_POLE) {
		*wskaznik=d-2 ;
		++wskaznik ;
		*wskaznik=e+1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-2][e+1] <10) {
					*wskaznik=d-2 ;
          ++wskaznik ;
					*wskaznik=e+1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-2 >=0 && e-1 >=0) {
	if (szachownica[d-2][e-1] == PUSTE_POLE) {
		*wskaznik=d-2 ;
    ++wskaznik ;
		*wskaznik=e-1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-2][e-1] <10) {
					*wskaznik=d-2 ;
					++wskaznik ;
					*wskaznik=e-1 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}

if (d-1 >=0 && e-2 >=0) {
	if (szachownica[d-1][e-2] == PUSTE_POLE) {
		*wskaznik=d-1 ;
    ++wskaznik ;
		*wskaznik=e-2 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	else	{
				if (szachownica[d-1][e-2] <10) {
					*wskaznik=d-1 ;
          ++wskaznik ;
					*wskaznik=e-2 ;
          ++wskaznik ;
					czygener=1 ;
          ++ilosc_wygener_pos ;
				}
	}
}


if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}


		case BPION:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

d=zlokalizowane[a+1] ;
e=zlokalizowane[a+2] ;

	if (zlokalizowane[a+2] == 3)	{
		b=konwertwsp(posprzeciwnika[2]) ;
		c=konwertwsp(posprzeciwnika[3]) ;
		if (szachownica[b][c] == CZPION)	{
			b=konwertwsp(posprzeciwnika[1]) ;
			c=konwertwsp(posprzeciwnika[3]) ;
			if (b - c == -2)	{
				c=konwertwsp(posprzeciwnika[0]) ;
				if ((zlokalizowane[a+1] - c  ==  -1)  ||  (zlokalizowane[a+1] - c  ==  1)) {
						*wskaznik = konwertwsp(posprzeciwnika[2]) ;
            ++wskaznik ;
						*wskaznik = 2 ;
            ++wskaznik ;
						czygener=1 ;
            ++ilosc_wygener_pos ;

		 }
		}
	 }
	}

if (e >=2 && e <=5) {
	if (szachownica[d][e-1] == PUSTE_POLE)	{
		*wskaznik=d ;
    ++wskaznik ;
		*wskaznik=e-1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	if (d-1 >=0)	{
		if (szachownica[d-1][e-1] == PUSTE_POLE)	{
			goto pbialyprawo ;
		}
		if (szachownica[d-1][e-1] >=11 && szachownica[d-1][e-1] <=16)	{
			*wskaznik=d-1 ;
      ++wskaznik ;
			*wskaznik=e-1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	pbialyprawo:
	if (d+1 <=7)	{
		if (szachownica[d+1][e-1] == PUSTE_POLE)	{
			goto pbialykoniec ;
		}
		if (szachownica[d+1][e-1] >=11 && szachownica[d+1][e-1] <=16)	{
			*wskaznik=d+1 ;
      ++wskaznik ;
			*wskaznik=e-1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	goto pbialykoniec ;
}

if (e==6) {
	if (szachownica[d][e-1] == PUSTE_POLE)	{
		*wskaznik=d ;
    ++wskaznik ;
		*wskaznik=e-1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		if (szachownica[d][e-2] == PUSTE_POLE)	{
			*wskaznik=d ;
      ++wskaznik ;
			*wskaznik=e-2 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	if (d-1 >=0)	{
		if (szachownica[d-1][e-1] == PUSTE_POLE)	{
			goto pbialyprawo2 ;
		}
		if (szachownica[d-1][e-1] >=11 && szachownica[d-1][e-1] <=16)	{
			*wskaznik=d-1 ;
      ++wskaznik ;
			*wskaznik=e-1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	pbialyprawo2:
	if (d+1 <=7)	{
		if (szachownica[d+1][e-1] == PUSTE_POLE)	{
			goto pbialykoniec ;
		}
		if (szachownica[d+1][e-1] >=11 && szachownica[d+1][e-1] <=16)	{
			*wskaznik=d+1 ;
			++wskaznik ;
			*wskaznik=e-1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	goto pbialykoniec ;
}

else {
	if (szachownica[d][e-1] == PUSTE_POLE)	{
		*wskaznik=d+90 ;
    ++wskaznik ;
		*wskaznik=e-1 ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	if (d-1 >=0)	{
		if (szachownica[d-1][e-1] == PUSTE_POLE)	{
			goto pbialyprawo3 ;
		}
		if (szachownica[d-1][e-1] >=11 && szachownica[d-1][e-1] <=16)	{
			*wskaznik=(d-1)+90 ;
      ++wskaznik ;
			*wskaznik=e-1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	pbialyprawo3:
	if (d+1 <=7)	{
		if (szachownica[d+1][e-1] == PUSTE_POLE)	{
			goto pbialykoniec ;
		}
		if (szachownica[d+1][e-1] >=11 && szachownica[d+1][e-1] <=16)	{
			*wskaznik=(d+1)+90 ;
      ++wskaznik ;
			*wskaznik=e-1 ;
			++wskaznik ;
			czygener=1 ;
			++ilosc_wygener_pos ;
		}
	}
	goto pbialykoniec ;
}

pbialykoniec:

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
  ++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
  ++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}



				case CZPION:

czygener=0 ;
wskaznik2=wskaznik ;

wskaznik+=3 ;

d=zlokalizowane[a+1] ;
e=zlokalizowane[a+2] ;

	if (zlokalizowane[a+2] == 4)	{
		b=konwertwsp(posprzeciwnika[2]) ;
		c=konwertwsp(posprzeciwnika[3]) ;
		if (szachownica[b][c] == BPION)	{
			b=konwertwsp(posprzeciwnika[1]) ;
			c=konwertwsp(posprzeciwnika[3]) ;
			if (b - c == 2)	{
				c=konwertwsp(posprzeciwnika[0]) ;
				if ((zlokalizowane[a+1] - c  ==  -1)  ||  (zlokalizowane[a+1] - c  ==  1)) {
						*wskaznik = konwertwsp(posprzeciwnika[2]) ;
            ++wskaznik ;
						*wskaznik = 5 ;
            ++wskaznik ;
						czygener=1 ;
            ++ilosc_wygener_pos ;

		 }
		}
	 }
	}

if (e >=2 && e <=5) {
	if (szachownica[d][e+1] == PUSTE_POLE)	{
		*wskaznik=d ;
    ++wskaznik ;
		*wskaznik=e+1 ;
		++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	if (d-1 >=0)	{
		if (szachownica[d-1][e+1] == PUSTE_POLE)	{
			goto pczarnyprawo ;
		}
		if (szachownica[d-1][e+1] <=6 && szachownica[d-1][e+1] >=1)	{
			*wskaznik=d-1 ;
      ++wskaznik ;
			*wskaznik=e+1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	pczarnyprawo:
	if (d+1 <=7)	{
		if (szachownica[d+1][e+1] == PUSTE_POLE)	{
			goto pczarnykoniec ;
		}
		if (szachownica[d+1][e+1] <=6 && szachownica[d+1][e+1] >=1)	{
			*wskaznik=d+1 ;
      ++wskaznik ;
			*wskaznik=e+1 ;
			++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	goto pczarnykoniec ;
}

if (e==1) {
	if (szachownica[d][e+1] == PUSTE_POLE)	{
		*wskaznik=d ;
    ++wskaznik ;
		*wskaznik=e+1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
		if (szachownica[d][e+2] == PUSTE_POLE)	{
			*wskaznik=d ;
      ++wskaznik ;
			*wskaznik=e+2 ;
			++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	if (d-1 >=0)	{
		if (szachownica[d-1][e+1] == PUSTE_POLE)	{
			goto pczarnyprawo2 ;
		}
		if (szachownica[d-1][e+1] <=6 && szachownica[d-1][e+1] >=1)	{
			*wskaznik=d-1 ;
      ++wskaznik ;
			*wskaznik=e+1 ;
			++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	pczarnyprawo2:
	if (d+1 <=7)	{
		if (szachownica[d+1][e+1] == PUSTE_POLE)	{
			goto pczarnykoniec ;
		}
		if (szachownica[d+1][e+1] <=6 && szachownica[d+1][e+1] >=1)	{
			*wskaznik=d+1 ;
      ++wskaznik ;
			*wskaznik=e+1 ;
      ++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	goto pczarnykoniec ;
}

else {
	if (szachownica[d][e+1] == PUSTE_POLE)	{
		*wskaznik=d+90 ;
    ++wskaznik ;
		*wskaznik=e+1 ;
    ++wskaznik ;
		czygener=1 ;
    ++ilosc_wygener_pos ;
	}
	if (d-1 >=0)	{
		if (szachownica[d-1][e+1] == PUSTE_POLE)	{
			goto pczarnyprawo3 ;
		}
		if (szachownica[d-1][e+1] <=6 && szachownica[d-1][e+1] >=1)	{
			*wskaznik=(d-1)+90 ;
      ++wskaznik ;
			*wskaznik=e+1 ;
			++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	pczarnyprawo3:
	if (d+1 <=7)	{
		if (szachownica[d+1][e+1] == PUSTE_POLE)	{
			goto pczarnykoniec ;
		}
		if (szachownica[d+1][e+1] <=6 && szachownica[d+1][e+1] >=1)	{
			*wskaznik=(d+1)+90 ;
			++wskaznik ;
			*wskaznik=e+1 ;
			++wskaznik ;
			czygener=1 ;
      ++ilosc_wygener_pos ;
		}
	}
	goto pczarnykoniec ;
}

pczarnykoniec:

if (czygener == 1)	{
	*wskaznik2=zlokalizowane[a] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+1] ;
	++wskaznik2 ;
	*wskaznik2=zlokalizowane[a+2] ;

	*wskaznik=KONIEC_WSP ;
	++wskaznik ;
	break ;
}

else	{
	wskaznik-=3 ;
	break ;
}



		}
	}

a+=3 ;
}

*wskaznik=-1 ;



return ilosc_wygener_pos ;

}



int konwertwsp (char x)
{
int wynik ;

switch (x) {
	case 'a':	wynik=0 ;
	break ;

	case 'b': wynik=1 ;
	break ;

	case 'c': wynik=2 ;
	break ;

	case 'd': wynik=3 ;
	break ;

	case 'e': wynik=4 ;
	break ;

	case 'f': wynik=5 ;
	break ;

	case 'g': wynik=6 ;
	break ;

	case 'h': wynik=7 ;
	break ;

	case '1': wynik=7 ;
	break ;

	case '2': wynik=6 ;
	break ;

	case '3': wynik=5 ;
	break ;

	case '4': wynik=4 ;
	break ;

	case '5': wynik=3 ;
	break ;

	case '6': wynik=2 ;
	break ;

	case '7': wynik=1 ;
	break ;

	case '8': wynik=0 ;
	break ;
}


return wynik ;
}



