#include "playtune.h"

extern void playtest();

void synth_test()
{
#define playscore(s)                \
	extern const unsigned char s[]; \
	play(s, 16);
	//playscore(boss2);
	//playscore(mm1);
	//playscore(ykzzldx);
    //playscore(mangzhong);
	//playscore(MoneyMoney_score);
	//playscore(Godknows);
	//playscore(Everytimeyoukissedme);
	//playscore(only_my_railgun);
	//playscore(daoxiang);
	//playscore(Melody);
	//playscore(Dusk);
	//playscore(Astronomia);
	//playscore(tkzc);
	playscore(bfxk);
	//playscore(lzlh);
	//playscore(lzlh2);
	//playscore(UnsquareDance_score); // (lots of percussion)
	//playscore(jordu_score);
	
    //playtest();
}
