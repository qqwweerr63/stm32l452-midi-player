#ifndef synth_Playtune_h_
#define synth_Playtune_h_

#define DBUG 0 // output console debugging messages?

#if 1
//19k采样率
#define AUDIO_SAMPLE_RATE 19531 //音频采样率,也是pwm的周期
#define MAX_OUTPUT_VALUT 4096
#define SHIFT_G 4

#else
//39k采样率
#define AUDIO_SAMPLE_RATE 39062 //音频采样率,也是pwm的周期
#define MAX_OUTPUT_VALUT 2048
#define SHIFT_G 5
#endif

#define MAX_TGENS 16	// maximum simultaneous tone generators
#define ASSUME_VOLUME 0 // assume volume information is present in bytestream files without headers?

#define DO_PERCUSSION 1	   // generate code for percussion instruments?
#define BOOST_PERCUSSION 1 // amplify percussion instruments?
#define DO_ENVELOPE 1	   // generate code to do DAHDSR tone amplitude envelope?
#define DYNAMIC_VOLUME 1   // dynamically adjust volume depending on how many instruments are playing?

struct file_hdr_t
{							  // the optional bytestream file header
	char id1;				  // 'P'
	char id2;				  // 't'
	unsigned char hdr_length; // length of whole file header
	unsigned char f1;		  // flag byte 1
	unsigned char f2;		  // flag byte 2
	unsigned char num_tgens;  // how many tone generators are used by this score
};
#define HDR_F1_VOLUME_PRESENT 0x80
#define HDR_F1_INSTRUMENTS_PRESENT 0x40
#define HDR_F1_PERCUSSION_PRESENT 0x20

// note commands in the bytestream
#define CMD_PLAYNOTE 0x90	/* play a note: low nibble is generator #, note is next byte, maybe volume */
#define CMD_STOPNOTE 0x80	/* stop a note: low nibble is generator # */
#define CMD_INSTRUMENT 0xc0 /* change instrument; low nibble is generator #, instrument is next byte */
#define CMD_RESTART 0xe0	/* restart the score from the beginning */
#define CMD_STOP 0xf0		/* stop playing */
/* if CMD < 0x80, then the other 7 bits and the next byte are a 15-bit big-endian number of msec to wait */

enum env_state_t
{
	ENV_IDLE,
	ENV_DELAY,
	ENV_ATTACK,
	ENV_HOLD,
	ENV_DECAY,
	ENV_SUSTAIN,
	ENV_RELEASE
};

typedef unsigned char byte;
typedef unsigned char bool;
#define false 0
#define true 1

//void tune_playnote (byte tgen, byte note, byte vol);
//void tune_stopnote (byte tgen);
//void tune_stopscore(void);
//void tune_playscore (const byte * score);
//void tune_stepscore (void);

void play(const byte *score, unsigned int num_tgens);
void stop(void);
bool isPlaying(void);

void tune_stepscore(void);

#endif
