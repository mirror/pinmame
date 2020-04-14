// license:BSD-3-Clause

#include "driver.h"
#include "filter.h"
#include <math.h>
#ifdef __MINGW32__
 #include <windef.h>
#endif

#define SAMPLE_RATE (4*48000) // 4x oversampling of standard output rate

#define SHIFTMASK 0x07 // = mc3417 // at least Xenon and Flash Gordon
//#define SHIFTMASK 0x0F // = mc3418 // features a more advanced syllabic filter (fancier lowpass filter for the step adaption) than the mc3417!

#define	FILTER_MAX				1.0954 // 0 dbmo sine wave peak value volts from MC3417 datasheet
#ifdef PINMAME
 // from exidy440
 //#define INTEGRATOR_LEAK_TC       0.001
 #define leak   0.939413062813475786119710824622305084524680890549441822009 //=pow(1.0/M_E, 1.0/(INTEGRATOR_LEAK_TC * 16000.0));
 //#define FILTER_DECAY_TC         ((18e3 + 3.3e3) * 0.33e-6)
 #define decay  0.99114768031730635396012114691053
 //#define FILTER_CHARGE_TC        (18e3 * 0.33e-6)
 #define charge 0.9895332758787504236814964839343

 // old values
 //#define leak   0.96875
 //#define decay  0.9990234375
 //#define charge 0.9990234375

 #define ENABLE_LOWPASS_ESTIMATE 0 // don't use it for now, it sounds too muffled
 #define SAMPLE_GAIN			0.92 // ~0..1
#else
 //#define	INTEGRATOR_LEAK_TC		0.001
 #define leak   0.939413062813475786119710824622305084524680890549441822009 //=pow(1.0/M_E, 1.0/(INTEGRATOR_LEAK_TC * 16000.0));
 //#define	FILTER_DECAY_TC			0.004
 #define decay  0.984496437005408405986988829697020369707861003180350567476 //=pow(1.0/M_E, 1.0/(FILTER_DECAY_TC * 16000.0));
 //#define	FILTER_CHARGE_TC		0.004
 #define charge 0.984496437005408405986988829697020369707861003180350567476 //=pow(1.0/M_E, 1.0/(FILTER_CHARGE_TC * 16000.0));

 #define FILTER_MIN				0.0416 // idle voltage (0/1 alternating input on each clock) from MC3417 datasheet
 #define SAMPLE_GAIN			(10000.0/32768.) // ~0..1
#endif

#if ENABLE_LOWPASS_ESTIMATE
#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif
#endif

struct mc3417_data
{
	INT8 	channel;
	UINT8	last_clock;
	UINT8	databit;
	UINT8	shiftreg;

	double	curr_value;
	double	next_value;

	double 	filter;
	double	integrator;
	double  gain;

#ifdef PINMAME // add low pass filtering like chip spec suggests, and like real machines also had (=extreme filtering networks, f.e. Flash Gordon has 4x Sallen-Key Active Low-pass at the end (cutoff ~3Khz))
	int     last_sound;

#define SALLEN_KEY // specs used from Xenon & Flash Gordon schematics
#ifdef SALLEN_KEY
	// filter stages
	filter2_context f[4];
#else // generic low-pass
	filter* filter_f;           /* filter used, ==0 if none */
	filter_state* filter_state; /* state of the filter */
#endif
#if ENABLE_LOWPASS_ESTIMATE
	UINT32  length_estimate;    // for estimating the clock rate/'default' sample playback rate of the machine
	UINT32  length_estimate_runs;
#endif
#endif
};


static struct mc3417_data mc3417[MAX_MC3417];

static void mc3417_update(int num, INT16 *buffer, int length);


int mc3417_sh_start(const struct MachineSound *msound)
{
	const struct mc3417_interface *intf = msound->sound_interface;
	int i;

	/* loop over MC3417 chips */
	for (i = 0; i < intf->num; i++)
	{
		struct mc3417_data *chip = &mc3417[i];
		char name[40];
		int ii,fi;

		/* reset the channel */
		memset(chip, 0, sizeof(*chip));

		/* create the stream */
		sprintf(name, "MC3417 #%d", i);
		chip->channel = stream_init_float(name, intf->volume[i], SAMPLE_RATE, i, mc3417_update, 1);
		/* bail on fail */
		if (chip->channel == -1)
			return 1;
		chip->gain = SAMPLE_GAIN;

#ifdef PINMAME
		chip->last_sound = 0;
#if ENABLE_LOWPASS_ESTIMATE
		chip->filter_f = 0;
		chip->length_estimate = 0;
		chip->length_estimate_runs = 0;
#else
#ifdef SALLEN_KEY
		fi = 0;
		// Xenon & Flash Gordon:
		for (ii = 0; ii < 2; ++ii)
		  //filter_sallen_key_lp_setup(36000, 36000, 2200e-12, 1000e-12, &chip->f[fi++], SAMPLE_RATE);
		  filter_setup(2.23151838311376E-03,4.46303676622753E-03,2.23151838311376E-03,-1.86781481370649,.876740887238946, &chip->f[fi++]);
		for (ii = 0; ii < 2; ++ii)
		  //filter_sallen_key_lp_setup(56000, 56000, 1000e-12,  470e-12, &chip->f[fi++], SAMPLE_RATE);
		  filter_setup(4.20071066000835E-03,8.40142132001669E-03,4.20071066000835E-03,-1.81355854102765,.830361383667684, &chip->f[fi++]);
#else
		chip->filter_f = filter_lp_fir_alloc(0.05, FILTER_ORDER_MAX);
		chip->filter_state = filter_state_alloc();
		filter_state_reset(chip->filter_f, chip->filter_state);
#endif
#endif
#endif
	}

	/* success */
	return 0;
}


static void mc3417_update(int num, INT16 *buffer, int length)
{
	struct mc3417_data *chip = &mc3417[num];
	float* __restrict buffer_f = (float*)buffer;

	/* zero-length? bail */
	if (length == 0)
		return;

#ifdef PINMAME
#if ENABLE_LOWPASS_ESTIMATE
	// 'detect' clock rate and guesstimate low pass filtering from this
	// not perfect, as the clock rate varies depending on the sample played
 #define LOWPASS_ESTIMATE_CYCLES 666
	if (chip->last_sound == 0 // is update coming from clock?
		&& length < SAMPLE_RATE/12000 // and no outlier?
		&& chip->length_estimate_runs < LOWPASS_ESTIMATE_CYCLES) // and we are still tracking the clock?
	{
		chip->length_estimate += length;
		chip->length_estimate_runs++;
	}
	else if (chip->length_estimate_runs == LOWPASS_ESTIMATE_CYCLES) // enough tracking of the clock -> enable filter and estimate for which cut frequency
	{
		double freq_scale;
		
		chip->length_estimate /= LOWPASS_ESTIMATE_CYCLES;

		freq_scale = ((INT32)chip->length_estimate - SAMPLE_RATE/48000) / (double)(SAMPLE_RATE/12000-1 - SAMPLE_RATE/48000); // estimate to end up within 0..1 (with all tested machines)
		freq_scale = MAX(MIN(freq_scale, 1.), 0.); // make sure to be in 0..1
		freq_scale = 1.-sqrt(sqrt(freq_scale));    // penalty for low clock rates -> even more of the lower frequencies removed then

		if (freq_scale < 0.45) // assume that high clock rates/most modern machines (that would end up at ~12000Hz filtering, see below) do not need to be filtered at all (improves clarity at the price of some noise)
		{
			chip->filter_f = filter_lp_fir_alloc((2000 + 22000*freq_scale)/SAMPLE_RATE, FILTER_ORDER_MAX); // magic, Xenon uses about 22 kHz
			chip->filter_state = filter_state_alloc(); //!! leaks
			filter_state_reset(chip->filter_f, chip->filter_state);
		}

		chip->length_estimate_runs++;
	}
#endif
#endif

	/* track how many samples we've updated without a clock, e.g. if its too many, then chip got no data = silence */
	if (length > SAMPLE_RATE/2048  //!! magic // PINMAME: be less conservative/more precise
		&& chip->last_sound != 0)  // clock did not update next_value since the last update -> fade to silence (resolves clicks and simulates real DAC kinda)
	{
		float tmp = chip->curr_value;
		int i;
		for (i = 0; i < length; i++, tmp *= 0.95f)
			buffer_f[i] = tmp;

		chip->next_value = tmp; // update next_value with the faded value

		chip->integrator = 0.; // PINMAME: Reset all chip state
		chip->filter = 0.;
		chip->shiftreg = 0;
	}
	else
	{
		/* compute the interpolation slope */
		// as the clock drives the update (99% of the time), we can interpolate only within the current update phase
		// for the remaining cases where the output drives the update, length is rather small (1 or very low 2 digit range): then the last sample will simply be repeated
		double data = chip->curr_value;
		const double slope = (chip->next_value - data) / (double)length;
		int i;

#ifdef PINMAME
#if ENABLE_LOWPASS_ESTIMATE
		if (chip->filter_f)
			for (i = 0; i < length; i++, data += slope)
			{
				filter_insert(chip->filter_f, chip->filter_state, (float)data);
				buffer_f[i] = filter_compute(chip->filter_f, chip->filter_state);
			}
		else
			for (i = 0; i < length; i++, data += slope)
				buffer_f[i] = (float)data;
#else // ENABLE_LOWPASS_ESTIMATE
		for (i = 0; i < length; i++, data += slope)
		{
#ifdef SALLEN_KEY
			// run the sample through the staged filter
			double v = data;
			int iii;
			for(iii = 0; iii < 4; iii++) //!! opt.!?
				v = filter2_step_with(&chip->f[iii], v);
			buffer_f[i] = (float)v;
#else
			filter_insert(chip->filter_f, chip->filter_state, (float)data);
			buffer_f[i] = filter_compute(chip->filter_f, chip->filter_state);
#endif
		}
#endif
#else // PINMAME
		for (i = 0; i < length; i++, data += slope)
			buffer_f[i] = (float)data;
#endif
	}

	chip->curr_value = chip->next_value;

	chip->last_sound = 1;
}


void mc3417_clock_w(int num, int state)
{
	struct mc3417_data *chip = &mc3417[num];
	int clock = state & 1, diffclock;

	/* update the clock */
	diffclock = clock ^ chip->last_clock;
	chip->last_clock = clock;

	/* speech clock changing (active on falling edge) */
	if (diffclock && !clock)
	{
		double temp;

		chip->shiftreg = ((chip->shiftreg << 1) | chip->databit) & SHIFTMASK;

		/* move the estimator up or down a step based on the bit */
		if (chip->databit)
			chip->integrator += chip->filter;
		else
			chip->integrator -= chip->filter;

		/* simulate leakage */
		chip->integrator *= leak;

		/* if we got all 0's or all 1's in the last n bits, bump the step up by charging the filter */
		if (chip->shiftreg == 0 || chip->shiftreg == SHIFTMASK)
		{
			chip->filter = (1.-charge) * FILTER_MAX + chip->filter * charge;
#ifndef PINMAME // cannot happen
			if (chip->filter > FILTER_MAX)
				chip->filter = FILTER_MAX;
#endif
		}
		/* simulate decay */
		else
		{
			chip->filter *= decay;
#ifndef PINMAME //!! should not be needed from chip spec, as it will either alternate 0/1 databits or output 'real' silence
			if (chip->filter < FILTER_MIN)
				chip->filter = FILTER_MIN;
#endif
		}

		/* compute the sample as a 16-bit word */
		temp = chip->integrator * chip->gain;

#ifdef PINMAME
#if 1
		/* compress the sample range, sounds better */
		if (temp < 0.)
			temp = temp / (-temp + 1.0) + temp*0.15;
		else
			temp = temp / ( temp + 1.0) + temp*0.15;

		if (chip->last_sound == 0) // missed a soundupdate: lerp data in here directly
			temp = (temp + chip->next_value)*0.5;

		if(temp <= -1.)
			chip->next_value = -1.;
		else if(temp >= 1.)
			chip->next_value = 1.;
		else
			chip->next_value = temp;
#else
		/* Cut off extreme peaks produced by bad speech data */
		if (temp < -80000./32768.) temp = -80000./32768.;
		else if (temp > 80000./32768.) temp = 80000./32768.;
		/* Just wrap to prevent clipping */
		if (temp < -1.) chip->next_value = (-2. - temp);
		else if (temp > 1.) chip->next_value = (2. - temp);
		else chip->next_value = temp;
#endif
#else
		/* compress the sample range to fit better in a 16-bit word */
		if (temp < 0.)
			chip->next_value = (temp / (-temp + 1.0));
		else
			chip->next_value = (temp / ( temp + 1.0));
#endif
		/* clear the update count */
		chip->last_sound = 0;

		/* update the output buffer before changing the registers */
		stream_update(chip->channel, 0);
	}
}

void mc3417_set_gain(int num, double gain)
{
	mc3417[num].gain = gain;
}


void mc3417_digit_w(int num, int data)
{
	mc3417[num].databit = data & 1;
}


void mc3417_clock_clear_w(int num, int data)
{
	mc3417_clock_w(num, 0);
}


void mc3417_clock_set_w(int num, int data)
{
	mc3417_clock_w(num, 1);
}


void mc3417_digit_clock_clear_w(int num, int data)
{
	mc3417[num].databit = data & 1;
	mc3417_clock_w(num, 0);
}


WRITE_HANDLER( mc3417_0_digit_w )		{ mc3417_digit_w(0,data); }
WRITE_HANDLER( mc3417_0_clock_w )		{ mc3417_clock_w(0,data); }
WRITE_HANDLER( mc3417_0_clock_clear_w )	{ mc3417_clock_clear_w(0,data); }
WRITE_HANDLER( mc3417_0_clock_set_w )	{ mc3417_clock_set_w(0,data); }
WRITE_HANDLER( mc3417_0_digit_clock_clear_w ) { mc3417_digit_clock_clear_w(0,data); }

WRITE_HANDLER( mc3417_1_digit_w )		{ mc3417_digit_w(1,data); }
WRITE_HANDLER( mc3417_1_clock_w )		{ mc3417_clock_w(1,data); }
WRITE_HANDLER( mc3417_1_clock_clear_w )	{ mc3417_clock_clear_w(1,data); }
WRITE_HANDLER( mc3417_1_clock_set_w )	{ mc3417_clock_set_w(1,data); }
WRITE_HANDLER( mc3417_1_digit_clock_clear_w ) { mc3417_digit_clock_clear_w(1,data); }
