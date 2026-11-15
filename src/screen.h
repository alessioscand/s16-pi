/*
 * s16-pi - the screen: what the kernel draws the game, the menus and the
 * console on
 *
 * The kernel uses only this interface. CVideoOut (video.cpp) is the HDMI
 * screen; another output (a CRT on the composite jack) would be another
 * implementation, in its own files, chosen in kernel.h.
 */
#ifndef SCREEN_H
#define SCREEN_H

#include <circle/types.h>

// System 16 visible area: the core hands over 320x224 lines, pitch 320
#define GAME_SRC_PITCH   320
#define GAME_SRC_LINES   224
#define GAME_SRC_X       0
#define GAME_SRC_Y       0
#define GAME_W           320
#define GAME_H           224

#define FILTER_NONE	0
#define FILTER_SCAN_LIGHT  1	// dark rows at 75%
#define FILTER_SCAN_MEDIUM 2	// dark rows at 50%
#define FILTER_SCAN_STRONG 3	// dark rows at 25%
#define FILTER_COMPOSITE   4	// blargg's NTSC filter (tvcore.h), no dark rows
#define FILTER_COUNT	5

#define BEZEL_BLACK	0	// BEZEL OFF: black around the picture
#define BEZEL_GAME	1	// BEZEL INFO, a game loaded: the pattern, title and buttons
#define BEZEL_MENU	2	// BEZEL INFO, no game: the pattern under the menus

// Logical colours (R in the low byte); the screen converts them if the
// framebuffer stores red in the high byte (Pi 4)
#define RGB(r, g, b)     (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

class CScreen
{
public:
	virtual ~CScreen (void) {}

	virtual boolean Initialize (void) = 0;		// console mode

	// Game mode for a filter; bAllowHVS = FALSE forces CPU scaling
	virtual boolean SetGameMode (unsigned nFilter, boolean bAllowHVS) = 0;
	virtual boolean SetConsoleMode (void) = 0;
	virtual boolean IsGameMode (void) const = 0;

	virtual unsigned GetWidth (void) const = 0;	// output resolution
	virtual unsigned GetHeight (void) const = 0;
	virtual unsigned GetScale (void) const = 0;	// total integer scale
	virtual unsigned GetHVSFactor (void) const = 0;	// part done by the HVS (1 = CPU only)
	virtual unsigned GetCPUScale (void) const = 0;	// part drawn by the CPU
	virtual unsigned GetFilter (void) const = 0;
	virtual unsigned GetFBWidth (void) const = 0;	// framebuffer before the HVS
	virtual unsigned GetFBHeight (void) const = 0;
	virtual unsigned GetGameY (void) const = 0;	// first game line in the framebuffer

	// Beam racing: game lines and band text go straight into the buffer being
	// shown (no flip); the UI (DrawGame) still uses the back buffer
	virtual void SetRacing (boolean bOn) = 0;

	// Console text (character cells)
	virtual unsigned GetRows (void) const = 0;
	virtual void Clear (unsigned nBuffer, u32 nColor) = 0;
	virtual void DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
			       u32 nFG, u32 nBG) = 0;
	virtual void ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor) = 0;

	// The visible GAME_W x GAME_H area into the back buffer
	virtual void DrawGame (const u32 *pSource) = 0;
	// one line of it, with the caller's scratch lines (GAME_W * 8 pixels each):
	// called by core 1 as each line is finished
	virtual void DrawGameLine (const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark) = 0;
	// COMPOSITE: one filtered line (nNtscW pixels 0x00RRGGBB) stretched to the
	// game width; pLine = scratch (GAME_W * 8)
	virtual void DrawNtscLine (unsigned y, const u32 *pNtsc, unsigned nNtscW, u32 *pLine) = 0;

	// The border around the picture (OPTIONS > BEZEL). The mode and the info
	// are drawn in both buffers when they change (nId: the game, or anything
	// that tells a new info apart); SetGameMode forgets them
	struct TBezelInfo
	{
		const char *pTitle;			// "GOLDEN AXE"
		const char *pMaker;			// "SEGA 1989"
		const char *pButtons[6];		// buttons A..F, 0 after the last
	};
	virtual void SetBezel (unsigned nMode, const TBezelInfo *pInfo, unsigned nId) = 0;
	// BEZEL_MENU: before a menu screen goes to the screen, its background
	// (the UI's grid colours) becomes the dark pattern, faded near the panels,
	// and the border of the back buffer follows it
	virtual void BezelMenu (u32 *pUIBuf) = 0;

	// Text beside the picture, '\n' starts a new line (a line too long for
	// the column wraps at a space). Left column: lines right-aligned to the
	// picture, the last one level with its bottom (status line, CONNECT A USB
	// GAMEPAD); right column: left-aligned, the first one level with its top
	// (messages). Where the columns are too narrow (a 4:3 display) the left
	// column goes in the band below the picture and the right one in the band
	// above, on one line. No spans = clear.
	struct TTextSpan { const char *pText; u32 nColor; };	// colour: 0xRRGGBB
	virtual void DrawSideText (boolean bRight, const TTextSpan *pSpans, unsigned nSpans) = 0;
	// the same: bTop = the right column (messages), else the left one
	void DrawBandText (boolean bTop, const TTextSpan *pSpans, unsigned nSpans)
	{
		DrawSideText (bTop, pSpans, nSpans);
	}

	// for the log
	virtual unsigned GetPixelOrder (void) const = 0;
	virtual unsigned GetWCPages (void) const = 0;	// framebuffer pages write-combining
	virtual boolean IsRedLow (void) const = 0;

	virtual void ShowBuffer (unsigned nBuffer) = 0;	// show a buffer now
	virtual boolean Flip (void) = 0;		// show the back buffer, swap roles
	virtual boolean WaitForVSync (void) = 0;
};

#endif
