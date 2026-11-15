/*
 * s16-pi - hiscores (core/s16_hiscore.inc, from MAME's hiscore.dat) in
 * saves/<game>.hi, and the battery-kept work RAM of every game in
 * saves/<game>.nv (Laser Ghost's gun adjustment too: without it the game
 * asks for the adjustment, which the game loop then does for the player).
 * The work itself is common/hiscore.cpp; here are the core and the log.
 */
#include "kernel.h"
#include "mcline.h"
#include "s16_api.h"
#include "hiscore.h"

static const THiCore s_HiCore = {s16_hi_size, s16_hi_ready, s16_hi_get, s16_hi_put};
static const TNvCore s_NvCore = {s16_nvram_size, s16_nvram};
static CHiscore s_Hiscore;
static CBatteryRam s_Battery;

void CKernel::HiStart (void)
{
	char Path[160];
	GamePath (Path, sizeof Path, "hi");
	s_Hiscore.Start (&s_HiCore, Path);
}

void CKernel::HiStop (void)
{
	s_Hiscore.Stop ();
}

// after each frame: the saved table goes in once the game has set its own up
void CKernel::HiTick (void)
{
	switch (s_Hiscore.Tick ())
	{
	case CardLoaded:	Log ("  hiscores: loaded from the card", COLOR_DIM);	break;
	case CardBlank:		Log ("  hiscores: none saved yet", COLOR_DIM);		break;
	default:		break;
	}
}

void CKernel::HiSave (void)
{
	McSync ();
	switch (s_Hiscore.Save ())
	{
	case CardSaved:		Log ("  hiscores: saved", COLOR_DIM);		break;
	case CardFailed:	Log ("  hiscores: NOT saved", COLOR_ERR);	break;
	default:		break;
	}
}

void CKernel::NvStart (void)
{
	char Path[160];
	GamePath (Path, sizeof Path, "nv");
	m_bGunAdjust = FALSE;
	switch (s_Battery.Start (&s_NvCore, Path))
	{
	case CardLoaded:
		Log ("  battery RAM: loaded from the card", COLOR_DIM);
		break;
	case CardBlank:
		m_bGunAdjust = s16_guns () > 0;
		Log (m_bGunAdjust ? "  battery RAM: none saved yet, the guns will be adjusted"
				  : "  battery RAM: none saved yet", COLOR_DIM);
		break;
	default:
		break;
	}
}

void CKernel::NvSave (void)
{
	McSync ();
	switch (s_Battery.Save ())
	{
	case CardSaved:		Log ("  battery RAM: saved", COLOR_DIM);	break;
	case CardFailed:	Log ("  battery RAM: NOT saved", COLOR_ERR);	break;
	default:		break;
	}
}

// CLEAR MEMORY: as taking the battery out; NvStart then says whether the
// guns need adjusting
void CKernel::NvClear (void)
{
	McSync ();
	s_Battery.Clear ();
	NvStart ();
}
