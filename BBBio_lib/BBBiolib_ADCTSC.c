/*
 * This library is a crude ADC library for Beaglebone black .
 *
 * support "Single Channel Single Step" ADC sample control  ,  not support Interrupt yet ,
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sched.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "BBBiolib.h"
#include "BBBiolib_ADCTSC.h"


#include <sys/time.h>
//-----------------------------------------------------------------------------------------------
/* Argument define */

/* Beaglebone black ADC have 7 AIN (0~6) */
#define ADCTSC_AIN_COUNT	7

/* Device register mamory map */
#define ADCTSC_MMAP_ADDR	0x44E0D000
#define ADCTSC_MMAP_LEN	0x2000

/* Device register offset */
#define ADCTSC_REVISION	0x0
#define ADCTSC_SYSCONFIG	0x10
#define ADCTSC_IRQSTATUS_RAW	0x24
#define ADCTSC_IRQSTATUS	0x28
#define ADCTSC_IRQENABLE_SET	0x2C
#define ADCTSC_IRQENABLE_CLR	0x30
#define ADCTSC_IRQWAKEUP	0x34
#define ADCTSC_DMAENABLE_SET	0x38
#define ADCTSC_DMAENABLE_CLR	0x3C
#define ADCTSC_CTRL	0x40
#define ADCTSC_ADCSTAT	0x44
#define ADCTSC_ADCRANGE	0x48
#define ADCTSC_ADC_CLKDIV	0x4C
#define ADCTSC_ADC_MISC	0x50
#define ADCTSC_STEPENABLE	0x54
#define ADCTSC_IDLECONFIG	0x58
#define ADCTSC_TS_CHARGE_STEPCONFIG	0x5C
#define ADCTSC_TS_CHARGE_DELAY	0x60
#define ADCTSC_STEPCONFIG1	0x64
#define ADCTSC_STEPDELAY1	0x68
#define ADCTSC_STEPCONFIG2	0x6C
#define ADCTSC_STEPDELAY2	0x70
#define ADCTSC_STEPCONFIG3	0x74
#define ADCTSC_STEPDELAY3	0x78
#define ADCTSC_STEPCONFIG4	0x7C
#define ADCTSC_STEPDELAY4	0x80
#define ADCTSC_STEPCONFIG5	0x84
#define ADCTSC_STEPDELAY5	0x88
#define ADCTSC_STEPCONFIG6	0x8C
#define ADCTSC_STEPDELAY6	0x90
#define ADCTSC_STEPCONFIG7	0x94
#define ADCTSC_STEPDELAY7	0x98
#define ADCTSC_STEPCONFIG8	0x9C
#define ADCTSC_STEPDELAY8	0xA0
#define ADCTSC_STEPCONFIG9	0xA4
#define ADCTSC_STEPDELAY9	0xA8
#define ADCTSC_STEPCONFIG10	0xAC
#define ADCTSC_STEPDELAY10	0xB0
#define ADCTSC_STEPCONFIG11	0xB4
#define ADCTSC_STEPDELAY11	0xB8
#define ADCTSC_STEPCONFIG12	0xBC
#define ADCTSC_STEPDELAY12	0xC0
#define ADCTSC_STEPCONFIG13	0xC4
#define ADCTSC_STEPDELAY13	0xC8
#define ADCTSC_STEPCONFIG14	0xCC
#define ADCTSC_STEPDELAY14	0xD0
#define ADCTSC_STEPCONFIG15	0xD4
#define ADCTSC_STEPDELAY15	0xD8
#define ADCTSC_STEPCONFIG16	0xDC
#define ADCTSC_STEPDELAY16	0xE0
#define ADCTSC_FIFO0COUNT	0xE4
#define ADCTSC_FIFO0THRESHOLD	0xE8
#define ADCTSC_DMA0REQ	0xEC
#define ADCTSC_FIFO1COUNT	0xF0
#define ADCTSC_FIFO1THRESHOLD	0xF4
#define ADCTSC_DMA1REQ	0xF8
#define ADCTSC_FIFO0DATA	0x100
#define ADCTSC_FIFO1DATA	0x200

/* ADCRANGE operator code */
#define ADCRANGE_MAX_RANGE	0xFFF
#define ADCRANGE_MIN_RANGE	0x000

/* CTRL operator code */
#define CTRL_ENABLE	0x1
#define CTRL_STEP_ID_TAG	0x2


/* step config */
#define BBBIO_ADC_STEP_MODE_SW_ONE_SHOOT	0x0
#define BBBIO_ADC_STEP_MODE_SW_CONTINUOUS	0x1
#define BBBIO_ADC_STEP_MODE_HW_ONE_SHOOT	0x2
#define BBBIO_ADC_STEP_MODE_HW_CONTINUOUS	0x3

#define BBBIO_ADC_STEP_AVG_NO	0x0
#define BBBIO_ADC_STEP_AVG_2	0x1
#define BBBIO_ADC_STEP_AVG_4	0x2
#define BBBIO_ADC_STEP_AVG_8	0x3
#define BBBIO_ADC_STEP_AVG_16	0x4

/* ----------------------------------------------------------------------------------------------- */
/* struct definition */
struct ADCTSC_FIFO_struct
{
	unsigned int *reg_count ;
	unsigned int *reg_data ;
	struct ADCTSC_FIFO_struct *next ;
};

struct ADCTSC_channel_struct
{
	unsigned int mode ;
	unsigned int enable ;
	unsigned int FIFO ;
	/*  13 + O + S cycle per sample , O is open delay , S is sample delay
	 *	Open delay  minmum : 0
	 *	Sample delay  minmum : 1
	 */
	unsigned int delay ;	/* bit 0~17 open delay , bit 24~31 sample delay  */

	/* channel buffer */
	unsigned int *buffer ;
	unsigned int buffer_size ;
	unsigned int buffer_count ;
	unsigned int *buffer_save_ptr ;
	unsigned int *buffer_fetch_ptr ;
};

struct ADCTSC_struct
{
	unsigned int H_range;
	unsigned int L_range;
	unsigned int ClockDiv;	/* Clock divider , Default ADC clock :24MHz */
	struct ADCTSC_channel_struct channel[8];
	struct ADCTSC_FIFO_struct FIFO[2] ;
};

/* ----------------------------------------------------------------------------------------------- */
/* Global Variable */
extern int memh;
extern volatile unsigned int *cm_wkup_addr;
volatile unsigned int *adctsc_ptr = NULL;


int sample_buffer[441000] ={0};
struct ADCTSC_struct ADCTSC ;
/* ----------------------------------------------------------------------------------------------- */
/* ADCTSC set range
 *
 * set ADC step sample max range and min range , Max range : 4095 (1.8v) , Min range : 0 (0v)
 *
 * 	@param L_range : Min range .
 *      @param H_range : Max range .
 *
 *	@return : 0 for error , 1 for success .
 *
 * 	@Note : Max Voltage in ADC_TSC is 1.8v .
 */
static int BBBIO_ADCTSC_set_range(int L_range, int H_range)
{
	unsigned int *reg = NULL;

	if((L_range > 4095) || (L_range < 0) || (H_range > 4095) || (H_range < 0) || (H_range < L_range)) {
#ifdef BBBIO_LIB_DBG
		printf("BBBIO_ADCTSC_set_range : ADC range error : [L:%d L ,H:%d] , (0 <= range <= 4095)\n", L_range, H_range);
#endif
		return 0 ;
	}

	reg = (void *)adctsc_ptr + ADCTSC_ADCRANGE;
	*reg |= (L_range | H_range << 16) ;
	return 1 ;
}

/* ----------------------------------------------------------------------------------------------- */
/* ADCTSC Channel status controller
 *
 *	#define BBBIO_ADCTSC_channel_enable(A) BBBIO_ADCTSC_channel_status(A,1)
 *	#define BBBIO_ADCTSC_channel_disable(A)	BBBIO_ADCTSC_channel_status(A,0)
*/
void BBBIO_ADCTSC_channel_status(int chn_ID ,int enable)
{
	unsigned int *reg = NULL;

	if((chn_ID < 0) || (chn_ID > 6)) {
#ifdef BBBIO_LIB_DBG
		printf("BBBIO_ADCTSC_Channel_status : Channel ID error [%d]\n", chn_ID);
#endif
	}
	else {
		/* step enable */
		if(enable) {
			ADCTSC.channel[chn_ID].enable = 1;
			reg = (void *)adctsc_ptr + ADCTSC_STEPENABLE;
			*reg |= 0x0001 << (chn_ID+1);
		}
		else {
			ADCTSC.channel[chn_ID].enable = 0;
			reg = (void *)adctsc_ptr + ADCTSC_STEPENABLE;
			*reg &= ~(0x0001 << (chn_ID+1));
		}

		/* Reset buffer counter*/
		ADCTSC.channel[chn_ID].buffer_count = 0;
		ADCTSC.channel[chn_ID].buffer_save_ptr = ADCTSC.channel[chn_ID].buffer;
	}
}

/* ----------------------------------------------------------------------------------------------- */
void BBBIO_ADCTSC_module_ctrl(unsigned int clkdiv, int L_range, int H_range)
{
	unsigned int *reg = NULL;

	if((clkdiv < 1) || (clkdiv > 65535)) {
#ifdef BBBIO_LIB_DBG
		printf("BBBIO_ADCTSC_module_ctrl : Clock Divider error [%d] ,set to div 1\n", clkdiv);
#endif
		clkdiv = 1;
	}

	reg = (void *)adctsc_ptr + ADCTSC_ADC_CLKDIV;
	*reg = clkdiv ;

	/* Default ADC range*/
	BBBIO_ADCTSC_set_range(L_range, H_range);
}
/* ----------------------------------------------------------------------------------------------- */
void BBBIO_ADCTSC_channel_ctrl(unsigned int chn_ID, int mode, int sample_avg, unsigned int *buf, unsigned int buf_size)
{
	unsigned int *reg = NULL;

	/* assian buffer */
	if(buf != NULL && buf_size > 0) {
		ADCTSC.channel[chn_ID].buffer = buf;
		ADCTSC.channel[chn_ID].buffer_size = buf_size;
		ADCTSC.channel[chn_ID].buffer_save_ptr = buf;
		ADCTSC.channel[chn_ID].buffer_count = 0;
	}

	/* Disable channel step*/
	BBBIO_ADCTSC_channel_disable(chn_ID);

	/* cancel step config register protection*/
	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg |= 0x4 ;

	/* set step config */
	reg = (void *)adctsc_ptr + (ADCTSC_STEPCONFIG1 + chn_ID * 0x8);
	printf("%d \t%X\t",chn_ID, *reg);
	*reg &= ~(0x1F) ;
	printf("%X\t",*reg);
	*reg |= mode | (sample_avg << 2) | ((chn_ID % 2) << 26);
	printf("%X\n",*reg);

	/* resume step config register protection*/
	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg &= ~0x4 ;
}


/* ----------------------------------------------------------------------------------------------- */
/* ADCTSC fetch data
 *
 *	fetch a word from ADC . this function is blocking function .
 *
 *
 */
unsigned int BBBIO_ADCTSC_work(unsigned int fetch_size)
{
	unsigned int *reg_count = NULL;
	unsigned int *reg_data = NULL;
	unsigned int *reg_ctrl = NULL;
	unsigned int buf_data = 0;
	int buf_count = 0;
	int chn_ID =0;
	struct ADCTSC_channel_struct *chn_ptr =NULL;
	struct ADCTSC_FIFO_struct *FIFO_ptr = ADCTSC.FIFO;
	int i ;

	/* Start sample */
	for(chn_ID = 0 ; chn_ID < ADCTSC_AIN_COUNT ; chn_ID++) {
		if(ADCTSC.channel[chn_ID].enable)
			BBBIO_ADCTSC_channel_enable(chn_ID);
	}

	/* Enable module and tag channel ID in FIFO data*/
	reg_ctrl = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg_ctrl |= (CTRL_ENABLE | CTRL_STEP_ID_TAG);

	/* waiting FIFO buffer fetch a data*/
	while(fetch_size>0) {
		reg_count = FIFO_ptr->reg_count;
		reg_data = FIFO_ptr->reg_data;

		buf_count = *reg_count;
		if(buf_count > 0) fetch_size -- ;
		for(i = 0 ; i < buf_count ; i++) {
			buf_data = *reg_data;
			chn_ID = (buf_data >> 16) & 0xF;
			chn_ptr = &ADCTSC.channel[chn_ID];
			 *chn_ptr->buffer_save_ptr = buf_data & 0xFFF;
			// *chn_ptr->buffer_save_ptr = buf_data;
			chn_ptr->buffer_save_ptr++;
			chn_ptr->buffer_count ++;
		}
		/* switch to next FIFO */
		FIFO_ptr = FIFO_ptr->next;
	}


	/* all sample finish */
        for(chn_ID = 0 ; chn_ID < ADCTSC_AIN_COUNT ; chn_ID++) {
		if(ADCTSC.channel[chn_ID].enable)
			BBBIO_ADCTSC_channel_disable(chn_ID);
        }
	reg_ctrl = (void *)adctsc_ptr + ADCTSC_CTRL;
        *reg_ctrl &= ~(CTRL_ENABLE | CTRL_STEP_ID_TAG);

	return 0 ;
}


/* ----------------------------------------------------------------------------------------------- */
/* ADCTSC init
 *
 * Handle mmap for ADCTSC , and initial ADCTSC .
 *
 *	@return : 0 for error , 1 for success .
 *
 *	@Note :  iolib_init() will run this function automatically
 */
int BBBIO_ADCTSC_Init()
{
	unsigned int *reg = NULL;
	unsigned int FIFO_count = 0;
	unsigned int FIFO_data = 0;
	int i ;

	if (memh == 0) {
#ifdef BBBIO_LIB_DBG
		printf("BBBIO_ADCTSC_Init : memory not mapped?\n");
#endif
		return 0;
	}

	adctsc_ptr = mmap(0, ADCTSC_MMAP_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, memh, ADCTSC_MMAP_ADDR);
	if(adctsc_ptr == MAP_FAILED) {
#ifdef BBBIO_LIB_DBG
		printf("BBBIO_ADCTSC_Init: ADCTSC mmap failure!\n");
#endif
		return 0;
	}

	/* Enable module Clock  */
	reg = (void *)cm_wkup_addr + BBBIO_CM_WKUP_ADC_TSC_CLKCTRL;
	*reg = 0x2 ;

	/* Pre-disable module work */
	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg &= ~0x1 ;

	/* Default ADC module configure*/
	BBBIO_ADCTSC_module_ctrl(1, ADCRANGE_MIN_RANGE, ADCRANGE_MAX_RANGE);

        /* Default channel configure */
	BBBIO_ADCTSC_channel_ctrl(0, 0, 0, NULL, 0);
	BBBIO_ADCTSC_channel_ctrl(1, 0, 0, NULL, 0);
	BBBIO_ADCTSC_channel_ctrl(2, 0, 0, NULL, 0);
	BBBIO_ADCTSC_channel_ctrl(3, 0, 0, NULL, 0);
	BBBIO_ADCTSC_channel_ctrl(4, 0, 0, NULL, 0);
	BBBIO_ADCTSC_channel_ctrl(5, 0, 0, NULL, 0);
	BBBIO_ADCTSC_channel_ctrl(6, 0, 0, NULL, 0);

	/* Clear FIFO  */
	FIFO_count = *((unsigned int*)((void *)adctsc_ptr + ADCTSC_FIFO0COUNT));
	for(i = 0 ; i < FIFO_count ; i++) {
		FIFO_data = *((unsigned int*)((void *)adctsc_ptr + ADCTSC_FIFO0DATA));
	}

	FIFO_count = *((unsigned int*)((void *)adctsc_ptr + ADCTSC_FIFO1COUNT));
        for(i = 0 ; i < FIFO_count ; i++) {
		FIFO_data = *((unsigned int*)((void *)adctsc_ptr + ADCTSC_FIFO1DATA));
        }

	/* init work struct */
	ADCTSC.FIFO[0].reg_count = (void *)adctsc_ptr + ADCTSC_FIFO0COUNT;
	ADCTSC.FIFO[0].reg_data = (void *)adctsc_ptr + ADCTSC_FIFO0DATA;
	ADCTSC.FIFO[0].next = &ADCTSC.FIFO[1];
	ADCTSC.FIFO[1].reg_count = (void *)adctsc_ptr + ADCTSC_FIFO1COUNT;
	ADCTSC.FIFO[1].reg_data = (void *)adctsc_ptr + ADCTSC_FIFO1DATA;
	ADCTSC.FIFO[1].next = &ADCTSC.FIFO[0];

	return 1;
}

/* ----------------------------------------------------------------------------------------------- */
void BBBIO_ADCTSC_step_work()
{
	unsigned int *reg = NULL;
	unsigned int sample_count = 0;
	unsigned int sample ;
	unsigned int ADC_buf_count =0;
	int i;

	printf("------------- step work --------------\n");

	reg = (void *)adctsc_ptr + ADCTSC_STEPENABLE;
	*reg = 0;	// disable step 1
	printf("STEPENABLE : %X\n", *reg);

	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg |= 0x4 ;	/* cancel step config register protection*/

	reg = (void *)adctsc_ptr + ADCTSC_STEPCONFIG1;
	*reg |= 0x1;
	printf(">STEPCONFIG1 : %X\n", *reg);

	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg &= ~0x4 ;   /* resume step config register protection*/


        reg = (void *)adctsc_ptr + ADCTSC_STEPENABLE;
	*reg |=0x0001 << 1;     // enable step 1
        printf("STEPENABLE : %X\n", *reg);


	/* start sample */

	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg |= 0x3 ;
	printf("CTRL : %X\n",*reg);

	struct timeval t_start,t_end;
	gettimeofday(&t_start, NULL);

	reg = (void *)adctsc_ptr + ADCTSC_FIFO0COUNT;
	ADC_buf_count = *reg;
	if(ADC_buf_count >0)
		printf("FIFO0COUNT : %d\n",ADC_buf_count);

	sample_count =0;
	while(ADC_buf_count > 0 || sample_count < 44100) {
		if(ADC_buf_count >0) {
			reg = (void *)adctsc_ptr + ADCTSC_FIFO0DATA;
			sample = *reg;
			sample_buffer[sample_count] = sample;
			sample_count ++ ;
		}
		reg = (void *)adctsc_ptr + ADCTSC_FIFO0COUNT;
		ADC_buf_count = *reg;
//		usleep(5000);
	}
	printf("<%d>\n",sample_count);
	gettimeofday(&t_end, NULL);
	for(i = 0 ; i < 5 ; i++) {
		sample = sample_buffer[i];
		printf("[%d \t%d \t%d]\n", sample, sample & 0x0FFF, ( sample & 0xF0000)>>16 );
	}
	float nTime = (t_end.tv_sec -t_start.tv_sec)*1000000.0 +(t_end.tv_usec -t_start.tv_usec);
	printf("%f\n", nTime);

	reg = (void *)adctsc_ptr + ADCTSC_CTRL;
	*reg &= ~0x3;
	printf("CTRL : %X\n",*reg);

}
