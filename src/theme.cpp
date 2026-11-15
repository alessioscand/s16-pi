/*
 * s16-pi - its look: colours, fonts, logo, credits and the grid (theme.h)
 */
#include "theme.h"
#include "fonts.h"

// the logo, 88x15 (as m92-pi's): # outline (also inside the holes of the 6
// and of the P), o letters
static const char *const s_Logo[] =
{
	"#####################################################........###########################",
	"#ooooooooooooooooooo#ooooooooooo#ooooooooooooooooooo#........#ooooooooooooooooooo#ooooo#",
	"#ooooooooooooooooooo#ooooooooooo#ooooooooooooooooooo#........#ooooooooooooooooooo#ooooo#",
	"#ooooooooooooooooooo#ooooooooooo#ooooooooooooooooooo#........#ooooooooooooooooooo#ooooo#",
	"#ooooo#####################ooooo#ooooo###############........#ooooo#########ooooo#ooooo#",
	"#ooooo###############.....#ooooo#ooooo########################ooooo#########ooooo#ooooo#",
	"#ooooooooooooooooooo#.....#ooooo#ooooooooooooooooooo#oooooooo#ooooooooooooooooooo#ooooo#",
	"#ooooooooooooooooooo#.....#ooooo#ooooooooooooooooooo#oooooooo#ooooooooooooooooooo#ooooo#",
	"#ooooooooooooooooooo#.....#ooooo#ooooooooooooooooooo#oooooooo#ooooooooooooooooooo#ooooo#",
	"###############ooooo#.....#ooooo#ooooo#########ooooo##########ooooo###############ooooo#",
	"###############ooooo#.....#ooooo#ooooo#########ooooo#........#ooooo#.............#ooooo#",
	"#ooooooooooooooooooo#.....#ooooo#ooooooooooooooooooo#........#ooooo#.............#ooooo#",
	"#ooooooooooooooooooo#.....#ooooo#ooooooooooooooooooo#........#ooooo#.............#ooooo#",
	"#ooooooooooooooooooo#.....#ooooo#ooooooooooooooooooo#........#ooooo#.............#ooooo#",
	"#####################.....###########################........#######.............#######",
};

static const char s_LogoKeys[] = "#o";
static const u32 s_LogoColors[] = {0x000000, 0xEEF2F3};	// off-white: the selected game stays the brightest thing

static const char *const s_Credits[][2] =
{
	{"BOARD",  "MAME - A.GILES"},
	{"VDP",    "MAME - D.HAYWOOD"},
	{"68000",  "MUSASHI - K.STENERUD"},
	{"Z80",    "N.ALLEMAND"},
	{"FM",     "YMFM - A.GILES"},
	{"ZIP",    "MINIZ - R.GELDREICH"},
	{"SYSTEM", "CIRCLE - R.STANGE"},
	{"NTSC",   "BLARGG - S.GREEN"},
	{"FONT",   "ZX ORIGINS - D.GUARD"}
};

// petrol panels with an ice-white frame, a crimson band
const TUITheme g_Theme =
{
	"S16-PI",
	0xE6EEF0, 0x2E5866, 0x021E2A,		// frame, outline, panel
	0xC81C37, 0xFFFFFF,			// band, cursor
	0xFFFFFF, 0x46C8F0, 0x83B5C8,		// title, value, dim
	0xE8213F,				// POWER OFF
	g_FontYouSquared, g_FontCyberwire, TRUE,
	s_Logo, sizeof s_Logo / sizeof s_Logo[0], 1, s_LogoKeys, s_LogoColors,
	s_Credits, sizeof s_Credits / sizeof s_Credits[0],
	"SEGA IS A TRADEMARK", "OF ITS OWNER."
};

// the grid: petrol at 30%, a line every 16 pixels
u32 ThemeGrid (int x, int y)
{
	return (x & 15) == 0 || (y & 15) == 0 ? 0x041F27 : 0x011921;
}
