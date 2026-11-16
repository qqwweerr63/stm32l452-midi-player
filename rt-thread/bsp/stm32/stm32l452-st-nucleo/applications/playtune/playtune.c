#include "playtune.h"
#include "stdint.h"
#include "string.h"
#include <rtthread.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define MIN_NOTE 21 // we only do the piano range
#define MAX_NOTE 108
#define NUM_NOTES (MAX_NOTE - MIN_NOTE + 1)

#define INT_MAX 0x7FFFFFFF

// the following should really be private, but are public temporarily for test code
bool tune_playing = false;
byte num_tgens_used = MAX_TGENS;
int32_t amplitude_fraction = 0x10000; // fraction of 2^16 to reduce amplitude by

/* MIDI velocity (1..127) -> amplitude fraction (2^16), perceptual sqrt curve.
 * A linear velocity->amplitude map sounds too quiet in the middle; sqrt keeps
 * the loudness subjectively even while retaining real dynamics.
 *   vel 32 -> 0.51, vel 64 -> 0.71, vel 96 -> 0.87, vel 127 -> 1.00
 * Generated as: round(65536 * sqrt((v+1)/128)), with v=0 forced to 0. */
static const uint16_t velocity_frac[128] = {
       0,  8192, 10033, 11585, 12953, 14189, 15326, 16384,
   17378, 18318, 19212, 20066, 20886, 21674, 22435, 23170,
   23884, 24576, 25249, 25905, 26545, 27170, 27780, 28378,
   28963, 29537, 30099, 30652, 31194, 31727, 32252, 32768,
   33276, 33776, 34270, 34756, 35235, 35708, 36175, 36636,
   37091, 37540, 37985, 38424, 38858, 39287, 39712, 40132,
   40548, 40960, 41368, 41771, 42171, 42567, 42959, 43348,
   43733, 44115, 44494, 44869, 45242, 45611, 45977, 46341,
   46702, 47059, 47415, 47767, 48117, 48465, 48809, 49152,
   49492, 49830, 50166, 50499, 50830, 51159, 51486, 51811,
   52134, 52454, 52773, 53090, 53405, 53719, 54030, 54340,
   54647, 54954, 55258, 55561, 55862, 56162, 56459, 56756,
   57051, 57344, 57636, 57926, 58215, 58503, 58789, 59073,
   59357, 59639, 59919, 60199, 60477, 60753, 61029, 61303,
   61576, 61848, 62119, 62388, 62657, 62924, 63190, 63455,
   63719, 63982, 64243, 64504, 64763, 65022, 65279, 65536};

/* Live audio diagnostics, sampled in the ISR and read with the "audiostat" msh command. */
volatile int32_t  audio_peak_level = 0;   /* peak |mix| before master volume (pre-scale) */
volatile int32_t  audio_peak_scaled = 0;  /* peak |mix| after master volume (what reaches PWM) */
volatile uint32_t audio_clip_count = 0;   /* number of clipped samples since last read */

/* Master volume, as a 2^16 fraction: 0x10000 = 100%, 0x8000 = 50%, 0 = mute.
 * Applied to the SIGNED mix before the DC-offset mapping so the 50% silence
 * point never moves (scaling the final PWM value would inject DC into the buzzer). */
volatile uint32_t master_volume_q16 = 0x10000;

void audio_set_volume_q16(uint32_t v)
{
    master_volume_q16 = (v > 0x10000) ? 0x10000 : v;
}

uint32_t audio_get_volume_q16(void)
{
    return master_volume_q16;
}

bool volume_present = ASSUME_VOLUME; // is there volume information in the bytestream?
int num_tgens_playing_last = 0;		 // how many tone generators played at the last sample
const byte *score_start;			 // the start of the Playtune bytestream
const byte *score_cursor;			 // where we are currently playing in the bytestream
unsigned scorewait_samples = 0;		 // how many samples to wait through for next score event
struct tone_gen_t
{									   // the internal state of each tone generator
	int32_t tone_phase;				   // where we are playing in an instrument sample (2^16 fraction)
	int32_t tone_incr;				   // increment from one sample to another (2^16 fraction)
	int32_t volume_frac;			   // midi volume from 1..127 code (2^16 fraction)
	uint16_t drum_ending_sample_index; // the index of the last sample for a percussion instrument
	byte instrument_index;			   // the instrument we're playing: I_PIANO, etc.
	byte playing;					   // is this channel playing?
	byte percussion;				   // is it a percussion instrument?
#if DO_ENVELOPE
	enum env_state_t env_state; // envelope state variables:
	int32_t env_mult, env_incr; // amplitude multiplier and increment, as fractions * 2^16
	int env_count;				// duration count for this state, as number of samples
#endif
	const int16_t *waveform_array; // pointer to the waveform sample array
								   //                                with 256 points for instruments, up to 16383 for percussion
} tone_gen[MAX_TGENS];
struct file_hdr_t file_header; // a possible file header from the Playtune bytestream

// computes ((a[31:0] * b[15:0]) >> 16)
static inline int32_t signed_multiply_32x16b(int32_t a, uint32_t b)
{
	return ((int64_t)a * (int16_t)(b & 0xFFFF)) >> 16;
}

// well-tempered MIDI note frequencies, based on the 12th root of 2.
const uint32_t freq4096[NUM_NOTES] = { // note frequencies * 4096
	/* 21..108*/ 112640, 119338, 126434, 133952, 141918, 150356, 159297,
	168769, 178805, 189437, 200702, 212636, 225280, 238676, 252868,
	267905, 283835, 300713, 318594, 337539, 357610, 378874, 401403,
	425272, 450560, 477352, 505737, 535809, 567670, 601425, 637188,
	675077, 715219, 757749, 802807, 850544, 901120, 954703, 1011473,
	1071618, 1135340, 1202851, 1274376, 1350154, 1430439, 1515497,
	1605613, 1701088, 1802240, 1909407, 2022946, 2143237, 2270680,
	2405702, 2548752, 2700309, 2860878, 3030994, 3211227, 3402176,
	3604480, 3818814, 4045892, 4286473, 4541360, 4811404, 5097505,
	5400618, 5721755, 6061989, 6422453, 6804352, 7208960, 7637627,
	8091784, 8572947, 9082720, 9622807, 10195009, 10801236, 11443511,
	12123977, 12844906, 13608704, 14417920, 15275254, 16183568,
	17145893};

// 16-channel mixer levels.  The same levels currently apply to all inputs.

const int32_t mixer_amplitude_fractions[MAX_TGENS + 1] = { //0..16 tone generators playing
/* Fractional amount (times 2^16) to reduce EACH tone generator based on how many are mixing.
     Original table (1.0/.60/.50/.40/.30/.25/...) was very conservative: with 6 notes each
     voice was cut to 25%, which is why polyphonic passages sounded tiny and forced people
     to pin velocity at max.  We now use a 1/sqrt(N) rule: incoherent waveforms add in RMS,
     so scaling each voice by 1/sqrt(N) keeps the summed RMS roughly constant while leaving
     real headroom for the occasional peak.  Output is hard-limited in update() as a
     safety net, so the slightly more aggressive scaling cannot saturate the PWM. */
#define fract16(x) ((int32_t)(x * 65536.0))
	fract16(1.0), // when no generators are playing
	//        1             2             3             4             5             6             7             8
	fract16(1.00), fract16(.71), fract16(.58), fract16(.50), fract16(.45), fract16(.41), fract16(.38), fract16(.35),
	//        9            10            11            12            13           14             15            16
	fract16(.33), fract16(.32), fract16(.30), fract16(.29), fract16(.28), fract16(.27), fract16(.26), fract16(.25)};




//***********  REGULAR AND PERCUSSION INSTRUMENTS  ****************

// To add a regular instrument, you must do ALL FOUR things below
// and keep the instruments in the same order in each.

// (1) add an external reference here to the wave table that you put in synth_Playtune_waves.c

extern const int16_t waveform_aguitar_0033[256];
extern const int16_t waveform_altosax_0001[256];
extern const int16_t waveform_birds_0011[256];
extern const int16_t waveform_cello_0005[256];
extern const int16_t waveform_clarinett_0001[256];
extern const int16_t waveform_clavinet_0021[256];
extern const int16_t waveform_dbass_0015[256];
extern const int16_t waveform_ebass_0037[256];
extern const int16_t waveform_eguitar_0002[256];
extern const int16_t waveform_eorgan_0064[256];
extern const int16_t waveform_epiano_0044[256];
extern const int16_t waveform_flute_0001[256];
extern const int16_t waveform_oboe_0002[256];
extern const int16_t waveform_piano_0013[256];
extern const int16_t waveform_violin_0003[256];

// (2) add an initializer for a new element in this array of structures.
//     It contains a pointer to the wave table, the DAHDSR envelope times in msec,
//     and the fraction of full volume that is the "sustain" volume.

struct instrument_waveform_t
{
	const int16_t *waveforms;								  // pointer to the 256-element waveform array
	const int delay, attack, hold, decay, release;			  // count of of samples for envelope each phase
	const int32_t sustain_level;							  // envelope level for sustain, as a fraction * 2^16
#define ms2cnt(ms) ((int32_t)(ms * AUDIO_SAMPLE_RATE / 1000)) // we define the delays as # of milliseconds
#define lv2fr(lv) ((int32_t)(lv * 65536.0))					  // and the level as a fraction * 2^16
#define DF_DL 0												  // defaults in msec for delay,
#define DF_AT 10											  //   attack,
#define DF_HL 2												  //   hold,
#define DF_DC 30											  //   decay
#define DF_RL 30											  //   release
#define DF_LV 0.60											  // default for sustain amplitude level
} // some audio expert should tweak the envelope for each instrument independently!
instrument_waveforms[] = { // this order must match the enum below
	{waveform_aguitar_0033, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_altosax_0001, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_birds_0011, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_cello_0005, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_clarinett_0001, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_clavinet_0021, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_dbass_0015, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_ebass_0037, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_eguitar_0002, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_eorgan_0064, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_epiano_0044, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_flute_0001, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_oboe_0002, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
	{waveform_piano_0013, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(60), lv2fr(DF_LV)},
	{waveform_violin_0003, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)}};

// (3) add a symbolic index name for your regular instrument at the end of this list

enum instrument_index_t
{ // instrument indexes
	I_AGUITAR,
	I_SAX,
	I_BIRDS,
	I_CELLO,
	I_CLARINET,
	I_CLAVINET,
	I_DBASS,
	I_EBASS,
	I_EGUITAR,
	I_ORGAN,
	I_EPIANO,
	I_FLUTE,
	I_OBOE,
	I_PIANO,
	I_VIOLIN
};

// (4) change whatever entries in this MIDI patch map corresponding to regular instruments
// that you want your new wave sample to play for. (I didn't create enough different
// instruments, so some of these assignments are pretty random!)

const byte instrument_patch_map[128] = { // map from MIDI patch numbers to instrument indexes
	/*1-8: DBASS*/ I_DBASS, I_DBASS, I_EBASS, I_DBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS,
	/*9-16: chromatic percussion*/ I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET,
	/*17-24: Organ*/ I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN,
	/*25-32: guitar*/ I_AGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_AGUITAR,
	/*33-40: bass*/ I_DBASS, I_EBASS, I_EBASS, I_DBASS, I_DBASS, I_DBASS, I_EBASS, I_EBASS,
	/*41-48: strings*/ I_VIOLIN, I_VIOLIN, I_CELLO, I_CELLO, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN,
	/*49-56: ensemble*/ I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN,
	/*57-66: brass*/ I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS,
	/*65-72: reed*/ I_SAX, I_SAX, I_SAX, I_OBOE, I_OBOE, I_SAX, I_SAX, I_OBOE,
	/*73-80: pipe*/ I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE,
	/*81-88: synth lead*/ I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR,
	/*89-96: synth pad*/ I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN,
	/*97-104: synth effects*/ I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS,
	/*105-112: ethnic*/ I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN,
	/*113-120: percussive*/ I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS,
	/*121-128: sound effects*/ I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS};

#if DO_PERCUSSION
// To add a percussion instrument, you must do ALL SIX things below
// and keep the instruments in order!

// (1) add an external reference here to the wave table you put in synth_Playtune_waves.c

extern const int16_t waveform_base_drum_04[];
extern const int16_t waveform_snare_drum_1[];
extern const int16_t waveform_mid_high_tom[];
extern const int16_t waveform_cymbal_2[];
extern const int16_t waveform_hi_bongo[];
extern const int16_t waveform_steel_bell_c6[];

// (2) put the pointer to your wave table at the end of this array, before the NULL

const int16_t *drum_waveforms[] = {
	waveform_base_drum_04, waveform_snare_drum_1, waveform_mid_high_tom,
	waveform_cymbal_2, waveform_hi_bongo, waveform_steel_bell_c6,
	NULL /* stopper so we can iterate over drum indexes */
};

// (3) put the size of it at the end of a table we refer to here that is
// actually located at the bottom of synth_Playtune_waves.c
extern const uint16_t drum_waveform_size[];

// (4) add an element to the end of this array telling what the sampling frequency is

const uint16_t drum_waveform_frequencies[] = {
	4000, 8000, 8000, 8000, 4000, 4000};

// (5) add a symbolic index name for the percussion instrument at the end of this list

enum drum_index_t
{ // drum indexes
	D_BASS,
	D_SNARE,
	D_TOM,
	D_CYMBAL,
	D_BONGO,
	D_BELL
};

// (6) finally, change whatever entries in this patch map correspond to percussion instruments
// that want your new wave sample to play for.  These are note numbers on MIDI channel 10,
// or 9 if you start counting from zero as in the file binary data. (I didn't include too many
// percussion instruments, so some of these assignments are pretty random!)

const byte drum_patch_map[128] = { // map from MIDI percussion instruments to drum indexes
	/*01-16*/ D_BASS, D_SNARE, D_TOM, D_CYMBAL, D_BONGO, D_BELL, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
	/*17-32*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
	/*33-48*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_SNARE, D_SNARE, D_SNARE, D_TOM, D_CYMBAL, D_TOM, D_CYMBAL, D_TOM, D_CYMBAL, D_TOM, D_TOM,
	/*49-64*/ D_CYMBAL, D_TOM, D_CYMBAL, D_CYMBAL, D_BELL, D_SNARE, D_CYMBAL, D_BELL, D_CYMBAL, D_CYMBAL, D_CYMBAL, D_BONGO, D_BONGO, D_BONGO, D_BONGO, D_BONGO,
	/*65-80*/ D_TOM, D_TOM, D_BELL, D_BELL, D_CYMBAL, D_CYMBAL, D_BELL, D_BELL, D_BONGO, D_BONGO, D_BONGO, D_BONGO, D_BONGO, D_TOM, D_TOM, D_BELL,
	/*81-96*/ D_BELL, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
	/*97-112*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
	/*113-128*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS};
#endif // DO_PERCUSSION

//------------------------------------------------------------------------------
//  Random byte generator
//
// This is an 8-bit version of the 2003 George Marsaglia XOR pseudo-random number
// generator. It has a full period of 255 before repeating.
// See http://www.arklyffe.com/main/2010/08/29/xorshift-pseudorandom-number-generator/
//------------------------------------------------------------------------------
static byte seed = 23;
byte random_byte(void)
{
	seed ^= (byte)(seed << 7); // The casts are silly, but are needed to keep the AVR compiler
	seed ^= (byte)(seed >> 5); // from generating "mul 128" for "<< 7"! Shifts are faster.
	seed ^= (byte)(seed << 3);
	return seed;
}

//------------------------------------------------------------------------------
// Start playing a note on a particular tone generator
//------------------------------------------------------------------------------

void tune_playnote(byte tgen, byte note, byte vol)
{
	struct tone_gen_t *tg;

	if (tgen < MAX_TGENS)
	{
		tg = &tone_gen[tgen];
		if (note >= 128)
		{ // percussion instrument
#if DO_PERCUSSION
			int drum_enum = *(drum_patch_map + note - 128);
			tg->waveform_array = drum_waveforms[drum_enum];
			//compute the increment to move from one sample point on the waveform to the next
			tg->tone_incr = (int64_t)drum_waveform_frequencies[drum_enum] * 0x20000 / AUDIO_SAMPLE_RATE;
			tg->tone_phase = 0;												  // start at the beginning
			tg->drum_ending_sample_index = drum_waveform_size[drum_enum] - 1; // remember the end of the waveform
			tg->percussion = true;
			// percussion notes generally seem undermodulated, so we might double the volume we get and clip
			if (BOOST_PERCUSSION)
				vol = vol > 63 ? 127 : vol << 1;
#if DO_ENVELOPE
			tg->env_mult = 0x10000;
			tg->env_incr = 0;
#endif

#if DBUG
			rt_kprintf("tgen= %d\r\n", tgen);
			rt_kprintf(" drum= %d\r\n", drum_enum);
			rt_kprintf(" vol= %d\r\n", vol);
			rt_kprintf(" incr= %d\r\n", tg->tone_incr);
			rt_kprintf(" phase= %d\r\n", tg->tone_phase);
#endif
#else //!DO_PERCUSSION
			return; // ignore percussion note
#endif
		}
		else
		{ // regular instrument
			if (note < MIN_NOTE)
				note = MIN_NOTE;
			if (note > MAX_NOTE)
				note = MAX_NOTE;
			int instrument_index = tg->instrument_index;
			tg->waveform_array = instrument_waveforms[instrument_index].waveforms;
#if DO_ENVELOPE
			tg->env_mult = 0;											  // setup AHDSR envelope
			tg->env_count = instrument_waveforms[instrument_index].delay; // # of samples
			// could be zero, but that will get dealt with at the first sample time.
			tg->env_state = ENV_DELAY;
			tg->env_incr = 0;
#endif
			//compute the increment to move from one sample point on the waveform to the next
			tg->tone_incr = ((uint64_t)(*(freq4096 + (note - MIN_NOTE))) * 0x80000) / (uint64_t)AUDIO_SAMPLE_RATE;
			//start at random place in the wave cycle to minimize phase lock cancellations
			tg->tone_phase = random_byte() << 23;
			tg->percussion = false;
#if DBUG
			rt_kprintf("tgen= %d\r\n", tgen);
			//rt_kprintf(" drum= %d\r\n", drum_enum);
			rt_kprintf(" vol= %d\r\n", vol);
			rt_kprintf(" incr= %d\r\n", tg->tone_incr);
			rt_kprintf(" phase= %d\r\n", tg->tone_phase);
#endif
		}
		/* Perceptual (sqrt) velocity curve: keeps real dynamics across 1..127. */
		tg->volume_frac = velocity_frac[vol & 0x7f]; // 0..0x10000
		tg->playing = true; // go!
	}
}

//------------------------------------------------------------------------------
// Stop playing a note on a particular tone generator
//------------------------------------------------------------------------------

void tune_stopnote(byte tgen)
{
	if (tgen < MAX_TGENS)
	{
		struct tone_gen_t *tg = &tone_gen[tgen];
		if (tg->playing)
		{
#if DBUG
			rt_kprintf("  stop tgen =%d\r\n", tgen);
#endif
#if DO_ENVELOPE
			if (!tg->percussion)
			{
				tg->env_state = ENV_RELEASE; // start release phase of a normal instrument note
				// ramp the amplitude from the sustain level down to 0
				tg->env_count = instrument_waveforms[tg->instrument_index].release;
				tg->env_mult = instrument_waveforms[tg->instrument_index].sustain_level;
				tg->env_incr = -tg->env_mult / tg->env_count; // ramp down to zero
															  // when the count becomes zero, the sample update function will set tg->playing to false
			}
			else
#endif
				tg->playing = false;
		}
	}
}

//------------------------------------------------------------------------------
// Stop playing a score
//------------------------------------------------------------------------------

void tune_stopscore(void)
{
	int i;
	for (i = 0; i < MAX_TGENS; ++i)
		tune_stopnote(i);
	tune_playing = false;
}

//------------------------------------------------------------------------------
//    Play a score
//------------------------------------------------------------------------------

void tune_playscore(const byte *score)
{ // start up the score
	if (tune_playing)
		stop();
	score_start = score;
	volume_present = ASSUME_VOLUME;
	num_tgens_used = MAX_TGENS;
	for (byte tgen = 0; tgen < MAX_TGENS; ++tgen) // set default instrument
		tone_gen[tgen].instrument_index = I_PIANO;

	// look for the optional file header
	memcpy(&file_header, score, sizeof(struct file_hdr_t)); // copy possible header from PROGMEM to RAM
	if (file_header.id1 == 'P' && file_header.id2 == 't')
	{ // validate it
		volume_present = file_header.f1 & HDR_F1_VOLUME_PRESENT;
		num_tgens_used = MAX(1, MIN(MAX_TGENS, file_header.num_tgens));
#if DBUG
		rt_kprintf("header: volume_present= %d\r\n", volume_present);
		rt_kprintf(", #tonegens= %d\r\n", num_tgens_used);
#endif
		score_start += file_header.hdr_length; // skip the whole header
	}
	// We will attentuate amplitudes prior to combining notes based on the
	// worst-case number of notes that might be playing simultaneously.
	amplitude_fraction = mixer_amplitude_fractions[num_tgens_used];
#if DBUG
	rt_kprintf("amplitude fraction is %d\r\n", amplitude_fraction);
#endif
	score_cursor = score_start;
	tune_stepscore(); /* execute initial the commands and return */
	tune_playing = true;
}

void tune_stepscore(void)
{ //*********   continue in the score
	byte cmd, opcode, tgen, note, vol;
	/* Do score commands until a "wait" is found, or the score is stopped.
    This is called initially from tune_playscore, but then is called
    from the slow interrupt routine when waits expire.
  */
	while (1)
	{
		cmd = *(score_cursor++);
		if (cmd < 0x80)
		{ /* wait count in msec. */
			/* wait count is in msec. */
			int scorewait_msec = ((unsigned)cmd << 8) | (*(score_cursor++));
			scorewait_samples = (scorewait_msec * 1000 * (uint64_t)(AUDIO_SAMPLE_RATE + .5)) / 1000000;
#if DBUG
			rt_kprintf("wait samples = %d\r\n", scorewait_samples);
#endif
			break;
		}
		opcode = cmd & 0xf0;
		tgen = cmd & 0x0f;
		if (opcode == CMD_STOPNOTE)
		{ /* stop note */
			tune_stopnote(tgen);
		}
		else if (opcode == CMD_PLAYNOTE)
		{							  /* play note */
			note = *(score_cursor++); // argument evaluation order is undefined in C!
			vol = volume_present ? *(score_cursor++) : 127;
			tune_playnote(tgen, note, vol);
		}
		else if (opcode == CMD_INSTRUMENT)
		{ /* change a tone generator's instrument */
			tone_gen[tgen].instrument_index = *(instrument_patch_map + *(score_cursor++));
		}
		else if (opcode == CMD_RESTART)
		{ /* restart the score */
			score_cursor = score_start;
		}
		else if (opcode == CMD_STOP)
		{ /* stop playing the score */
			stop();
			break;
		}
	}
}

void play(const byte *score, unsigned int num_tgens)
{
	num_tgens_used = num_tgens;
	tune_playscore(score);
}

bool isPlaying(void)
{
	return tune_playing;
}

void stop(void)
{
	tune_stopscore();
}

// for testing...
void tune_setinstrument(byte tgen, byte instrument_index)
{
	tone_gen[tgen].instrument_index = instrument_index;
}

extern void spwm_setoutput(uint32_t val);

/*************************************************************************************************
   Our interrupt-time "update" function, where all the dirty work gets done.
   We are called every 2.9 msec, and must generate a block of 128 2-byte samples
   as quickly as we can.
*************************************************************************************************/

void update(void)
{
	uint32_t sample_date = 0;

	// we use the sample processing interval (22.666 usec) as the timer for score waits
	if (tune_playing && scorewait_samples && --scorewait_samples == 0)
	{
		tune_stepscore(); // end of a score wait, so execute more score commands
	}
#if DYNAMIC_VOLUME
	int num_tgens_playing = 0;
#endif
	int level = 0;
#if DYNAMIC_VOLUME // adjust the mixer input attentuation based on how many generators were last active
	amplitude_fraction = mixer_amplitude_fractions[num_tgens_playing_last];
#endif
	for (byte tgen = 0; tgen < num_tgens_used; ++tgen)
	{ // look at each tone generator
		int our_level = 0;
		struct tone_gen_t *tg = &tone_gen[tgen];
		if (tg->playing)
		{
			uint32_t index1, index2, scale;
			int32_t val1, val2;
#if DYNAMIC_VOLUME
			++num_tgens_playing;
#endif
			if (tg->percussion)
			{ // percussion: play the waveform once
				// tone_phase = +iiiiiiiiiiiiiiffffffffffffffffx, i=index into waveform array, f=fraction
				index1 = tg->tone_phase >> 17; // 14 bits of index, 0..16383 max samples
				index2 = index1 + 1;
				if (index2 >= tg->drum_ending_sample_index)
					tg->playing = false;				// end of percussion waveform; stop playing soon
				scale = (tg->tone_phase >> 1) & 0xFFFF; // 16 bits of fractional distance between samples
			}
			else
			{ // regular instrument: repeat the waveform indefinitely
				// tone_phase = +iiiiiiiiffffffffffffffffxxxxxxx, i=index into waveform array, f=fraction
				index1 = tg->tone_phase >> 23;			// 8 bits of index, 0..255 samples
				index2 = (index1 + 1) & 0xff;			// wrap around at the end
				scale = (tg->tone_phase >> 7) & 0xFFFF; // 16 bits of fractional distance between samples
#if DO_ENVELOPE
				while (tg->env_count == 0)
				{ // change to a state with a non-zero count
					switch (tg->env_state)
					{
					case ENV_IDLE:
						tg->env_count = INT_MAX;
						break;
					case ENV_DELAY:
						tg->env_state = ENV_ATTACK;
						tg->env_count = instrument_waveforms[tg->instrument_index].attack;
						tg->env_incr = 0x10000 / tg->env_count; // ratchet up to maximum volume
						break;
					case ENV_ATTACK:
						tg->env_state = ENV_HOLD;
						tg->env_count = instrument_waveforms[tg->instrument_index].hold;
						tg->env_mult = 0x10000; // hold this volume
						tg->env_incr = 0;
						break;
					case ENV_HOLD:
						tg->env_state = ENV_DECAY;
						tg->env_count = instrument_waveforms[tg->instrument_index].decay;
						tg->env_mult = 0x10000; // start with max volume
						// count down to the sustain volume level
						tg->env_incr = (instrument_waveforms[tg->instrument_index].sustain_level - 0x10000) / tg->env_count;
						break;
					case ENV_DECAY:
						tg->env_state = ENV_SUSTAIN;
						tg->env_count = INT_MAX;
						tg->env_mult = instrument_waveforms[tg->instrument_index].sustain_level;
						tg->env_incr = 0; // maintain the sustain volume level
						break;
					case ENV_SUSTAIN:
						tg->env_count = INT_MAX; // (shouldn't happen; just keep on keeping on)
						break;
					case ENV_RELEASE:
						tg->env_state = ENV_IDLE;
						tg->playing = false; // end of release: stop playing the note
						break;
					}
				}				 // while state count is zero
				--tg->env_count; // count towards the next envelope state
#endif							 // DO_ENVELOPE
			}					 // regular instrument

			// do a linear interpolation between the samples that bracket the waveform point
			val1 = (int16_t) * (tg->waveform_array + index1);
			val1 *= 0xFFFF - scale;
			val2 = (int16_t) * (tg->waveform_array + index2);
			val2 *= scale;
			our_level = (val1 + val2) >> 16;
			tg->tone_phase = (tg->tone_phase + tg->tone_incr) & 0x7fffffff; // advance to the next waveform point
#if DO_ENVELOPE
			our_level = signed_multiply_32x16b(tg->env_mult, our_level); // envelope amplitude attenuation
			tg->env_mult += tg->env_incr;								 // adjust attentuator
#endif
			// Mix all the tone generators together, scaling each waveform by its
			// own MIDI velocity (volume_frac) and the polyphony headroom factor.
			// NOTE: do NOT overwrite volume_frac here -- that was the old hack that
			// pinned every note to full volume and destroyed all dynamics.
			level += signed_multiply_32x16b(tg->volume_frac, signed_multiply_32x16b(amplitude_fraction, our_level));

		} // tone generator is playing
	}	  // next tone generator

	/* --- loudness diagnostics (read with the "audiostat" msh command) ---
	 * Measured on the pre-master mix, so it reflects the score's own dynamics
	 * and is independent of the user volume setting. */
	{
		int32_t abslevel = (level < 0) ? -level : level;
		if (abslevel > audio_peak_level)
			audio_peak_level = abslevel;
		if (level < -32768 || level > 32767)
			++audio_clip_count;
	}

	/* Apply master volume to the SIGNED signal.  Use int64: the helper
	 * signed_multiply_32x16b() truncates its second arg to int16, which would
	 * turn a full-scale 0x10000 gain into zero (silence) by mistake. */
	if (master_volume_q16 != 0x10000)
		level = (int32_t)(((int64_t)level * (int64_t)master_volume_q16) >> 16);

	{
		int32_t abslevel = (level < 0) ? -level : level;
		if (abslevel > audio_peak_scaled)
			audio_peak_scaled = abslevel;
	}

	/* Map signed mix to 0..4095, then hard-limit.  A CCR value greater than
	 * ARR latches the PWM output permanently high, putting DC across the buzzer
	 * (poor tone / wasted current), so it must never reach that state. */
	level = (level + 32768) >> SHIFT_G;
	if (level < 0)
		level = 0;
	else if (level >= (int32_t)MAX_OUTPUT_VALUT)
		level = MAX_OUTPUT_VALUT - 1;
	sample_date = level; //sample_date 0~MAX_OUTPUT_VALUT-1
#if DYNAMIC_VOLUME
	num_tgens_playing_last = num_tgens_playing; // for the next sample, remember how many generators were playing
#endif
	spwm_setoutput(sample_date);
}

void playnote(byte note, byte instr, byte vol, int wait) { // (Uses hidden undocumented stuff!)
  tune_setinstrument(0, instr);
  tune_playnote(0, note, vol);
  tune_playing = true;
  rt_thread_mdelay(wait);
  tune_stopnote(0);
  rt_thread_mdelay(100);
}

void playtest()
{
#if 1 // a playground for various tests
  num_tgens_used = 1; // maximize volume by mixing only one generator
  if (0)  for (byte note = 21; note <= 108; ++note) // play all notes
      playnote(note, 0, 127, 200);
  if (0)  for (byte instr = 0; instr <= 14; ++instr) // play all instruments
      playnote(60, instr, 127, 100);
  if (1)  for (byte instr = 0; instr <= 14; ++instr)
	          for (byte note = 21; note <= 108; ++note)// play all instruments
                 playnote(note, instr, 127, 100);
  if (0) for (byte vol = 0; vol < 127; ++vol) // play all volumes
      playnote(60, 0, vol, 100);
  if (1)  for (byte drum = 128; drum <= 133; ++drum) // play all drums
      playnote(drum, 0, 127, 1000);
  if (0) for (byte tgens = 1; tgens <= 16; ++tgens) { // try all numbers of channels
      num_tgens_used = tgens; // (this affects the mixer input attenuation)
      extern const int32_t mixer_amplitude_fractions[MAX_TGENS + 1];
      amplitude_fraction = mixer_amplitude_fractions[num_tgens_used - 1];
      playnote(60, 0, 127, 100);
    }
  tune_playing = false;
  
#endif
}