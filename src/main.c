#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/audioin.h>
#include <psp2/ctrl.h>
#include <psp2/rtc.h>

#include <vita2d.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "fft.h"

#define MIC_FREQ 48000
#define MIC_GRAIN 768
#define AUDIO_IN_BUFF 1024

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

/* ABGR Color */
#define COLOR_WHITE   (0xFFFFFFFF)
#define COLOR_BLACK   (0xFF000000)
#define COLOR_RED     (0xFF0000FF)
#define COLOR_GREEN   (0xFF00FF00)
#define COLOR_BLUE    (0xFFFF0000)
#define COLOR_CYAN    (0xFFFFFF00)
#define COLOR_MAGENTA (0xFFFF00FF)
#define COLOR_YELLOW  (0xFF00FFFF)

#define SPECTRUM_WIDTH 4

#define TICK_DELAY 80000

/* draw magnitudes */
void draw_spectrum(FFT *pspectrum){
	uint16_t i;
	uint16_t mag;
	/* draw only rectangles of size SPECTRUM_WIDTH, from left to
	   right, until the screen is full */
	for (i = 0; i < SCREEN_WIDTH / SPECTRUM_WIDTH; i++){
		mag = (int)(pspectrum[i].mag);
		vita2d_draw_rectangle(i * SPECTRUM_WIDTH, SCREEN_HEIGHT - 1, 
					SPECTRUM_WIDTH, -mag, COLOR_WHITE);
	}
	return;
}

/* draw arrow function */
void drawArrow(uint16_t x, uint16_t y, uint32_t color){
	vita2d_draw_line(x, y, x, y+20, color);
	vita2d_draw_line(x-3, y+20-7, x, y+20, color);
	vita2d_draw_line(x+3, y+20-7, x, y+20, color);
}

/* show menu function */
void showMenu(vita2d_pgf *pgf, uint8_t sens){
	uint16_t x, y;
	x = SCREEN_WIDTH - 400;
	y = 10;
	vita2d_draw_rectangle(x, y, 380, 165, COLOR_YELLOW);
	x += 10;
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				"          512 point Radix-2 FFT");
	y += 25;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				"           = Proof of Concept =");
	y += 25;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f, 
				"                  by pyroesp");
	y += 25;
	vita2d_pgf_draw_textf(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Microphone sensitivity : %d", sens);
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Show/hide this menu : L_TRIGGER");
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Use left & right to move the arrow");
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Use select to exit");

	return;
}

int main (int argc, char *argv[]){
	uint8_t exit = 0;
	uint8_t menu = 1;
	uint16_t i;

	/* Audio in vars */
	int16_t *audioIn = NULL;
	int32_t port = 0;
	uint8_t sens = 1;

	int16_t arrowX = SPECTRUM_WIDTH/2;
	int16_t arrowY = 30;
	

	/* Blocks per stage array */
	uint16_t blocks[FFT_STAGES];
	/* Butterflies per block per stage */
	uint16_t butterflies[FFT_STAGES];
	/* Bit Reversed LUT */
	uint16_t bit_reversed[FFT_POINT];
	/* Audio samples */
	float x[FFT_POINT] = {0};
	/* Complex data */
	Complex data_complex[FFT_POINT];
	/* Twiddle factor complex array */
	Complex W[FFT_POINT_2];
	/* FFT amp & phase buffer*/
	FFT spectrum[FFT_POINT];
	
	vita2d_pgf *pgf;
	SceCtrlData ctrl, oldCtrl;
	SceRtcTick current, old;
	
	/* Open microphone port */
	port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_RAW, MIC_GRAIN,
				  MIC_FREQ, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
	/* malloc audio in buffer */
	audioIn = (int16_t*)malloc(sizeof(int16_t) * AUDIO_IN_BUFF);
	if (0 > port || !audioIn){
		sceKernelDelayThread(5000000);
		sceKernelExitProcess(0);
		return 0;
	}
	/* only MIC_GRAIN values are going to be read in the buffer
	   so the rest of the values in the buffer should be set to 0
	   aka zero padding */
	memset(audioIn, 0, sizeof(int16_t) * AUDIO_IN_BUFF);

	/* Pre-setup for FFT library */
	fft_BlockPerStage(blocks);
	fft_ButterfliesPerBlocks(butterflies);
	fft_BitReversedLUT(bit_reversed);
	fft_TwiddleFactor(W);
	/* Set spectrum buffer to 0, just in case */
	memset(spectrum, 0, sizeof(FFT) * FFT_POINT);

	/* vita2d stuff */
	vita2d_init();
	vita2d_set_clear_color(COLOR_BLACK);
	pgf = vita2d_load_default_pgf();

	sceRtcGetCurrentTick(&old);

	/* loop while not exiting */
	while(!exit){
		sceRtcGetCurrentTick(&current);

		sceAudioInInput(port, (void*)audioIn);
		/* Normalize audio samples and convert to float */
		for (i = 0; i < FFT_POINT; ++i){
			x[i] = ((float)audioIn[i]*(float)sens)/(float)FFT_POINT;
		}

		/* Convert x buffer from polar to complex */
		fft_DataToComplex(x, data_complex, bit_reversed);
		/* Compute FFT algorithm */
		fft_Compute(data_complex, W, blocks, butterflies);
		/* Convert data_complex buffer from complex to polar */
		fft_ComplexToMagnPhase(data_complex, spectrum);

		vita2d_start_drawing();
		vita2d_clear_screen();	

		draw_spectrum(spectrum);
		
		/* Get the frequency pointed at by the arrow 
		   From the FFT_POINT amplitudes, only FFT_POINT_2 are usable 
		   We only display SCREEN_WIDTH / SPECTRUM_WIDTH amplitudes 
		   arrowX / SPECTRUM_WIDTH should give a value from 0 to SCREEN_WIDTH/SPECTRUM_WIDTH
		   MIC_FREQ / FFT_POINT gives the frequency step */
		vita2d_pgf_draw_textf(pgf, 10, 20, COLOR_RED, 1.0f, "freq = %0.2f",((float)MIC_FREQ / (float)FFT_POINT) * (float)(arrowX / SPECTRUM_WIDTH) );
		drawArrow(arrowX, arrowY, COLOR_RED);

		if (menu)
			showMenu(pgf, sens);

		/* vita2d end drawing */
		vita2d_end_drawing();
		vita2d_swap_buffers();
		
		/* vita controls */
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		if (ctrl.buttons & SCE_CTRL_SELECT)
			exit = 1;
		else if (ctrl.buttons & SCE_CTRL_UP && !(oldCtrl.buttons & SCE_CTRL_UP))
			sens++;
		else if (ctrl.buttons & SCE_CTRL_DOWN && !(oldCtrl.buttons & SCE_CTRL_DOWN))
			sens--;
		else if (ctrl.buttons & SCE_CTRL_LTRIGGER && !(oldCtrl.buttons & SCE_CTRL_LTRIGGER))
			menu = !menu;
		
		if (current.tick > (old.tick + TICK_DELAY)){
			if (ctrl.buttons & SCE_CTRL_LEFT)
				arrowX -= SPECTRUM_WIDTH;	
			else if (ctrl.buttons & SCE_CTRL_RIGHT)
				arrowX += SPECTRUM_WIDTH;

			memcpy(&old, &current, sizeof(SceRtcTick));
		}

		if (0 >= sens)
			sens = 1;
		if (0 > arrowX)
			arrowX = SPECTRUM_WIDTH/2;
		else if (SCREEN_WIDTH-SPECTRUM_WIDTH/2 < arrowX)
			arrowX = SCREEN_WIDTH - SPECTRUM_WIDTH/2;

		memcpy(&oldCtrl, &ctrl, sizeof(SceCtrlData));
		sceKernelDelayThread(10000);
	}

	/* exit */
	free(audioIn);
	vita2d_fini();
	vita2d_free_pgf(pgf);
	sceAudioInReleasePort(port);
	sceKernelExitProcess(0);
	return 0;
}
