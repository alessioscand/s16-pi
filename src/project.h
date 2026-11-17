/*
 * s16-pi - what the shared files in common/ need to know about the project
 */
#ifndef PROJECT_H
#define PROJECT_H

#define PROJECT_NAME	"s16-pi"
#define PROJECT_VERSION	"1.0.0"

/* The emulated picture: the core hands over 320x224 lines, pitch 320 */
#define PROJECT_WIDTH	320
#define PROJECT_HEIGHT	224
#define PROJECT_SRC_PITCH	320		/* the game's buffer */
#define PROJECT_SRC_LINES	224
#define PROJECT_SRC_X	0			/* the picture in it */
#define PROJECT_SRC_Y	0

/* TV filter (common/tvcore, common/composite): one line in (320 pixels
   0x00RRGGBB), the filtered line out (COMP_OUT_W) */
#define PROJECT_TV_IN_BYTES	(320 * 4)
#define PROJECT_TV_OUT_W	749

/* Audio queue (common/audio.cpp): ~1 video frame (~17 ms) instead of 2,
   less audio lag; the sound runs a slice at a time in each line */
#define PROJECT_AUDIO_TARGET	800
#define PROJECT_AUDIO_BAND	200

/* Players: one pad each (common/input) */
#define PROJECT_PLAYERS	4

/* Buttons besides the directions, START and SELECT: A B C D (the games use
   up to three) */
#define PROJECT_BUTTONS	4

/* Each emulated frame starts here: the first line after the picture */
#define PROJECT_PHASE_LINE	224

/* Error codes = number of green LED blinks when the screen is unusable */
#define PROJECT_ERR_FB    2   /* framebuffer / HDMI */
#define PROJECT_ERR_SD    3   /* SD card or FAT filesystem */
#define PROJECT_ERR_GAME  5   /* no System 16 or 18 game (MAME .zip) in the s16pi folder */
#define PROJECT_ERR_CORE  6   /* emulator core error */
#define PROJECT_ERR_MEM   7   /* out of memory */

#ifdef __cplusplus
extern "C" {
#endif

void project_log (const char *pFormat, ...);
void project_fatal (int nCode, const char *pFormat, ...);

#ifdef __cplusplus
}
#endif

#endif
