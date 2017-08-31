# vitaFFT


This is a Fast Fourier Transform PoC for PSVita using my libFFT for the FFT algorithm.
The audio samples are read throught the internal microphone of the PSVita.

The FFT is setup as a 512 point FFT, see the libfft to know how to increase or decrease the points.

## TODO:

- Fix amplitude value so it isn't longer than the height of the screen
- Determine the unit of the amplitude (dB, ...) - FIXED in libfft

## Dependencies

Install vitasdk through vdpm.

* ctrl
* audio in
* vita2d
* display
* rtc
* gxm
* sysmodule
* pgf
* pvf
* common dialog
* freetype
* png
* jpeg
* z
* m
* c

## Build software

Build the software with "cmake . && make".

## License:

CC Attribution-ShareAlike 4.0 International, see LICENSE.md

### Thanks to :

	- everyone who worked on the vitasdk and vitasdk samples

	- xerpi for his vita2d library
