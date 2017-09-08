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

/* ABGR Color */
#define COLOR_WHITE   (0xFFFFFFFF)
#define COLOR_GRAY    (0xFF7F7F7F)
#define COLOR_BLACK   (0xFF000000)
#define COLOR_RED     (0xFF0000FF)
#define COLOR_GREEN   (0xFF00FF00)
#define COLOR_BLUE    (0xFFFF0000)
#define COLOR_CYAN    (0xFFFFFF00)
#define COLOR_MAGENTA (0xFFFF00FF)
#define COLOR_YELLOW  (0xFF00FFFF)

#define MIC_FREQ 48000
#define MIC_GRAIN 768
/* Audio in buffer has to be at least MIC_GRAIN */
#define AUDIO_IN_BUFF MIC_GRAIN

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

#define SPECTRUM_WIDTH 4

#define TICK_DELAY 80000

/* draw magnitudes */
void draw_spectrum(FFT *pspectrum, uint32_t color, uint8_t magn_or_dB, uint8_t zoomY){
	uint16_t i;
	int32_t val;
	/* draw only rectangles of size SPECTRUM_WIDTH, from left to
	   right, until the screen is full */
	for (i = 0; i < SCREEN_WIDTH / SPECTRUM_WIDTH; i++){
		if (!magn_or_dB){
			val = (int32_t)(pspectrum[i].mag);
			vita2d_draw_rectangle(i * SPECTRUM_WIDTH, SCREEN_HEIGHT - 3, SPECTRUM_WIDTH, -val * zoomY, color);
		}else{
			val = (int32_t)(pspectrum[i].dB);
			vita2d_draw_rectangle(i * SPECTRUM_WIDTH, SCREEN_HEIGHT/2, SPECTRUM_WIDTH, -val * zoomY, color);
		}
	}
	return;
}

/* draw cursor function */
void drawCursor(uint16_t x, uint16_t y, uint16_t cursorSize, uint32_t color){
	vita2d_draw_line(x, y - cursorSize, x, y + cursorSize, color);
	vita2d_draw_line(x - cursorSize, y, x + cursorSize, y, color);
}

/* show menu function */
void showMenu(vita2d_pgf *pgf, uint8_t sens, uint8_t magn_or_dB, float freq){
	uint16_t x, y;
	uint16_t textWidth;
	x = SCREEN_WIDTH - 420;
	y = 10;
	vita2d_draw_rectangle(x, y, 400, 185, COLOR_YELLOW);
	x += 10;
	y += 20;
	textWidth = vita2d_pgf_text_width(pgf, 1.0f, "512 point Radix-2 FFT");
	vita2d_pgf_draw_text(pgf, (x - 10) + (400 - textWidth)/2, y, COLOR_BLACK, 1.0f,
				"512 point Radix-2 FFT");
	y += 25;
	textWidth = vita2d_pgf_text_width(pgf, 1.0f, "by pyroesp");
	vita2d_pgf_draw_text(pgf, (x - 10) + (400 - textWidth)/2, y, COLOR_BLACK, 1.0f, 
				"by pyroesp");
	y += 25;
	vita2d_pgf_draw_textf(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Microphone sensitivity (up/down): %d", sens);
	y += 20;
	vita2d_pgf_draw_textf(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Cursor frequency: %0.2f", freq);
	y += 20;
	vita2d_pgf_draw_textf(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Press cross to change display : %s", magn_or_dB?"dB":"Magn");
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Show/hide this menu : L_TRIGGER");
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Use left stick to move the cursor");
	y += 20;
	vita2d_pgf_draw_text(pgf, x, y, COLOR_BLACK, 1.0f,
				" - Use select to exit");

	return;
}

int main (int argc, char *argv[]){
	uint8_t exit = 0;
	uint8_t menu = 1;
	uint8_t magn_or_dB = 0;
	uint16_t i;

	/* Audio in vars */
	int16_t *audioIn = NULL;
	int32_t port = 0;
	uint8_t sens = 1;

	int16_t cursorX = SPECTRUM_WIDTH/2 * 50;
	int16_t cursorY = SCREEN_HEIGHT/2;
	uint16_t cursorSize = 5;
	
	uint8_t zoomY = 1;

	/* Blocks per stage array */
	uint16_t blocks[FFT_STAGES];
	/* Butterflies per block per stage */
	uint16_t butterflies[FFT_STAGES];
	/* Bit Reversed LUT */
	uint16_t bit_reversed[FFT_POINT];
	/* Window function */
	float window[FFT_POINT] = {0};
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
	fft_Window(FFT_WIN_BLACKMAN, window);
	/* Set spectrum buffer to 0, just in case */
	memset(spectrum, 0, sizeof(FFT) * FFT_POINT);

	/* vita2d stuff */
	vita2d_init();
	vita2d_set_clear_color(COLOR_GRAY);
	pgf = vita2d_load_default_pgf();

	/* Read tick */
	sceRtcGetCurrentTick(&old);
	
	/* Enable analog sampling */
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

	/* loop while not exiting */
	while(!exit){
		sceRtcGetCurrentTick(&current);

		/* Read microphone */
		sceAudioInInput(port, (void*)audioIn);
		/* Normalize audio samples and convert to float */
		for (i = 0; i < FFT_POINT; ++i){
			x[i] = (float)audioIn[i]*(float)sens;
		}

		/* Convert x buffer from polar to complex */
		fft_DataToComplex(x, window, data_complex, bit_reversed);
		/* Compute FFT algorithm */
		fft_Compute(data_complex, W, blocks, butterflies);
		/* Convert data_complex buffer from complex to polar and normalize output */
		/* fft_ComplexToMagnPhase(data_complex, spectrum, 1); */

		/* Convert complex data to magnitude and dB */
		fft_ComplexTodB(data_complex, spectrum);
		
		vita2d_start_drawing();
		vita2d_clear_screen();	

		draw_spectrum(spectrum, COLOR_CYAN, magn_or_dB, zoomY);
		
		/* 
			Get the frequency pointed at by the cursor 
			From the FFT_POINT amplitudes, only FFT_POINT_2 are usable (=512/2=256)
			We only display SCREEN_WIDTH / SPECTRUM_WIDTH amplitudes (=960/4=240)
			cursorX / SPECTRUM_WIDTH should give a value from 0 to SCREEN_WIDTH/SPECTRUM_WIDTH
			MIC_FREQ / FFT_POINT gives the frequency step (48000Hz/512=93.75Hz)
		*/
		
		if (menu){
			showMenu(pgf, sens, magn_or_dB, ((float)MIC_FREQ / (float)FFT_POINT) * (float)(cursorX / SPECTRUM_WIDTH));
			drawCursor(cursorX, cursorY, cursorSize, COLOR_YELLOW);
		}

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
		else if (ctrl.buttons & SCE_CTRL_CROSS && !(oldCtrl.buttons & SCE_CTRL_CROSS)){
			magn_or_dB = !magn_or_dB;
			if (magn_or_dB)
				zoomY = 5;
			else
				zoomY = 1;
		}
		
		if (current.tick > (old.tick + TICK_DELAY)){
			if (ctrl.ly > 240)
				cursorY += 8;
			else if (ctrl.ly > 150)
				cursorY++;
			else if (ctrl.ly < 10)
				cursorY -= 8;
			else if (ctrl.ly < 105)
				cursorY--;
			
			if (ctrl.lx > 240)
				cursorX += SPECTRUM_WIDTH * 3;
			else if (ctrl.lx > 200)
				cursorX += SPECTRUM_WIDTH;	
			else if (ctrl.lx < 10)
				cursorX -= SPECTRUM_WIDTH * 3;	
			else if (ctrl.lx < 55)
				cursorX -= SPECTRUM_WIDTH;

			memcpy(&old, &current, sizeof(SceRtcTick));
		}

		if (0 >= sens)
			sens = 1;
		if (cursorSize > cursorY)
			cursorY = cursorSize;
		else if (SCREEN_HEIGHT - cursorSize < cursorY)
			cursorY = SCREEN_HEIGHT - cursorSize;
		if (0 > cursorX)
			cursorX = SPECTRUM_WIDTH/2;
		else if (SCREEN_WIDTH-SPECTRUM_WIDTH/2 < cursorX)
			cursorX = SCREEN_WIDTH - SPECTRUM_WIDTH/2;

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
