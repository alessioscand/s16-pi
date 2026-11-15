/*
 * s16-pi - HDMI output (see video.h)
 */
#include "fontdata.h"
#include "video.h"
#include "fbmap.h"
#include <circle/bcmpropertytags.h>
#include <circle/font.h>
#include <circle/util.h>

#define PROPTAG_RELEASE_BUFFER	0x00048001
#define PROPTAG_GET_PIXEL_ORDER	0x00040006	// 0 = BGR, 1 = RGB

#define MAX_LINE	(GAME_W * 8)		// longest scaled line we support

// the border: the UI's grid (ui.h) at 30%, a line every 16 UI pixels, lined
// up with the menus' own grid; the lines fade out near the picture (72 screen
// pixels) and, under the menus, near the panels and the text (18 UI pixels)
#define BG_BASE		0x05566Fu		// UI_BG
#define BG_LINE		0x0E6882u		// UI_GRID
#define BG_DIM		30			// %
#define BG_CELL		16
#define FADE_SCREEN	72
#define FADE_UI		18
#define CANVAS_MAX	(800 * 450)		// cells: up to 4K at 4x... and 1080p at 2x

// the text: Area51 at 2 screen pixels per font pixel
#define TEXT_H		7			// font pixels: capitals and digits
#define TEXT_PITCH	12			// between lines
#define INFO_WHITE	0xECECECu
#define INFO_GREY	0x808A8Fu
#define SIDE_LINES	8
#define SIDE_CHARS	48

CVideoOut::CVideoOut (void)
:	m_pFrameBuffer (0),
	m_nDispW (0),
	m_nDispH (0),
	m_nWidth (0),
	m_nHeight (0),
	m_nPitchPx (0),
	m_pBase (0),
	m_nBack (1),
	m_bGameMode (FALSE),
	m_bRacing (FALSE),
	m_nTextScale (1),
	m_nScale (1),
	m_nK (1),
	m_nM (1),
	m_nFilter (FILTER_NONE),
	m_nGameX (0),
	m_nGameY (0),
	m_nU (1),
	m_bSide (FALSE),
	m_bBands (FALSE),
	m_nGap (0),
	m_nColTop (0),
	m_nColBot (0),
	m_nBezel (BEZEL_BLACK),
	m_nBezelId (0),
	m_bBezelDrawn (FALSE),
	m_nBgBase (0xFF000000u),
	m_nBgLine (0xFF000000u),
	m_pDist (0),
	m_nCW (0),
	m_nCH (0),
	m_nOX (0),
	m_nOY (0),
	m_pLine (0),
	m_pDark (0),

	m_nPixelOrder (1),
	m_nWCPages (0),
	m_bRLow (TRUE)
{
	memset (&m_Info, 0, sizeof m_Info);
	memset (m_Text, 0, sizeof m_Text);
	m_nMenuSum[0] = m_nMenuSum[1] = 0;
}

CVideoOut::~CVideoOut (void)
{
	delete m_pFrameBuffer;
	delete [] m_pLine;
	delete [] m_pDark;
	delete [] m_pDist;
}

boolean CVideoOut::Allocate (unsigned nWidth, unsigned nHeight)
{
	if (m_pFrameBuffer != 0)
	{
		CBcmPropertyTags Tags;
		TPropertyTagSimple Release;
		Tags.GetTag (PROPTAG_RELEASE_BUFFER, &Release, sizeof Release);	// may be unsupported
		delete m_pFrameBuffer;
	}

	m_pFrameBuffer = new CBcmFrameBuffer (nWidth, nHeight, 32, nWidth, nHeight * 2, 0, TRUE);
	if (   m_pFrameBuffer == 0
	    || !m_pFrameBuffer->Initialize ())
	{
		return FALSE;
	}

	m_nWidth   = m_pFrameBuffer->GetWidth ();
	m_nHeight  = m_pFrameBuffer->GetHeight ();
	m_nPitchPx = m_pFrameBuffer->GetPitch () / 4;
	m_pBase    = (u32 *) (uintptr) m_pFrameBuffer->GetBuffer ();
	if (   m_pBase == 0
	    || m_nWidth != nWidth || m_nHeight != nHeight
	    || m_pFrameBuffer->GetVirtHeight () < 2 * m_nHeight)
	{
		return FALSE;
	}

	// Byte order of the pixels: QEMU reports RGB (1, red in the low byte),
	// the Pi 4 firmware (framebuffer_swap=1 by default) reports BGR (0, red
	// in the high byte, the same layout as the core's lines)
	CBcmPropertyTags Tags;
	TPropertyTagSimple Order;
	if (Tags.GetTag (PROPTAG_GET_PIXEL_ORDER, &Order, sizeof Order))
	{
		m_nPixelOrder = Order.nValue;
	}
	m_bRLow = m_nPixelOrder != 0;

	// write-combining: stores merged into bursts instead of one bus write each
	m_nWCPages = FbWriteCombine ((uintptr) m_pBase, (size_t) m_nPitchPx * 4 * m_pFrameBuffer->GetVirtHeight ());

	Clear (0, RGB (0, 0, 0));
	Clear (1, RGB (0, 0, 0));
	ShowBuffer (0);
	return TRUE;
}

boolean CVideoOut::Initialize (void)
{
	// current HDMI mode (chosen by the firmware from config.txt / EDID)
	CBcmPropertyTags Tags;
	TPropertyTagDisplayDimensions Dim;
	if (   Tags.GetTag (PROPTAG_GET_DISPLAY_DIMENSIONS, &Dim, sizeof Dim)
	    && Dim.nWidth >= 640 && Dim.nWidth <= 4096
	    && Dim.nHeight >= 480 && Dim.nHeight <= 2160)
	{
		m_nDispW = Dim.nWidth;
		m_nDispH = Dim.nHeight;
	}
	else
	{
		m_nDispW = 640;
		m_nDispH = 480;
	}

	m_pLine = new u32[MAX_LINE];
	m_pDark = new u32[MAX_LINE];
	m_pDist = new u8[CANVAS_MAX];
	if (m_pLine == 0 || m_pDark == 0 || m_pDist == 0)
	{
		return FALSE;
	}

	return SetConsoleMode ();
}

boolean CVideoOut::SetConsoleMode (void)
{
	m_bGameMode = FALSE;
	m_bSide = FALSE;
	m_bBezelDrawn = FALSE;
	memset (m_Text, 0, sizeof m_Text);
	m_nTextScale = m_nDispH >= 900 ? 2 : 1;
	return Allocate (m_nDispW, m_nDispH);
}

boolean CVideoOut::SetGameMode (unsigned nFilter, boolean bAllowHVS)
{
	m_nFilter = nFilter < FILTER_COUNT ? nFilter : FILTER_NONE;


	// total integer scale S
	unsigned sx = m_nDispW / GAME_W, sy = m_nDispH / GAME_H;
	m_nScale = sx < sy ? sx : sy;
	if (m_nScale < 1) m_nScale = 1;
	if (m_nScale > 8) m_nScale = 8;

	// HVS factor k: largest divisor of S that divides the display exactly;
	// with a filter the CPU must draw at least 2x (m >= 2); at most 2, so
	// that the text beside the picture can have 2 screen pixels per font pixel
	m_nK = 1;
	if (bAllowHVS)
	{
		for (unsigned k = m_nScale < 2 ? m_nScale : 2; k >= 2; k--)
		{
			if (   m_nScale % k == 0
			    && m_nDispW % k == 0 && m_nDispH % k == 0
			    && (m_nFilter == FILTER_NONE || m_nScale / k >= 2))
			{
				m_nK = k;
				break;
			}
		}
	}
	m_nM = m_nScale / m_nK;

	unsigned nFbW = m_nDispW / m_nK, nFbH = m_nDispH / m_nK;
	if (!Allocate (nFbW, nFbH))
	{
		// fall back to CPU scaling at the full resolution
		m_nK = 1;
		m_nM = m_nScale;
		if (!Allocate (m_nDispW, m_nDispH))
		{
			return FALSE;
		}
	}

	unsigned nGameW = GAME_W * m_nM, nGameH = GAME_H * m_nM;
	m_nGameX = (m_nWidth - nGameW) / 2;
	m_nGameY = (m_nHeight - nGameH) / 2;

	// text beside the picture: Area51, 2 screen pixels per font pixel
	m_nU = m_nK >= 2 ? 1 : 2;
	const unsigned U = m_nU, nTextH = TEXT_H * U;
	// the title and the buttons above and below the picture: only in bands
	// at least 3 lines of text tall (1080p, not 720p); the text in the columns
	// then keeps from the picture the distance the title has from it, else
	// 24 screen pixels
	m_bBands = m_nGameY * m_nK >= 3 * TEXT_H * 2;
	m_nGap = m_bBands ? (m_nGameY - nTextH) / 2 : (24 + m_nK - 1) / m_nK;
	m_bSide = m_nGameX >= 2 * m_nGap + 40 * U;
	if (!m_bSide)
	{
		m_bBands = FALSE;	// the bands hold the status line and the messages
	}
	m_nColTop = m_nGameY > m_nGap ? m_nGameY : m_nGap;
	m_nColBot = m_nGameY + nGameH;
	if (m_nColBot > m_nHeight - m_nGap) m_nColBot = m_nHeight - m_nGap;

	// the border's colours and fade
	auto Dim = [this] (u32 c)
	{
		u32 r = ((c >> 16) & 0xFF) * BG_DIM / 100, g = ((c >> 8) & 0xFF) * BG_DIM / 100,
		    b = (c & 0xFF) * BG_DIM / 100;
		return Native (RGB (r, g, b));
	};
	m_nBgBase = Dim (BG_BASE);
	m_nBgLine = Dim (BG_LINE);
	for (unsigned i = 0; i < 256; i++)		// smoothstep: i = distance / reach * 255
	{
		unsigned t = i;
		m_Fade[i] = (u8) (t * t * (3 * 255 - 2 * t) / (255 * 255));
	}
	m_bBezelDrawn = FALSE;			// Allocate cleared both buffers
	memset (m_Text, 0, sizeof m_Text);
	m_nMenuSum[0] = m_nMenuSum[1] = 0;

	// BEZEL_MENU canvas: the screen in UI pixels, the menu's top left corner
	// on a whole cell
	m_nOX = (m_nGameX + m_nM - 1) / m_nM;
	m_nOY = (m_nGameY + m_nM - 1) / m_nM;
	m_nCW = m_nOX + GAME_W + (m_nWidth - m_nGameX - nGameW + m_nM - 1) / m_nM;
	m_nCH = m_nOY + GAME_H + (m_nHeight - m_nGameY - nGameH + m_nM - 1) / m_nM;
	if (m_nCW * m_nCH > CANVAS_MAX)
	{
		m_nCW = m_nCH = 0;			// no fade near the panels
	}

	m_bGameMode = TRUE;
	return TRUE;
}

unsigned CVideoOut::GetColumns (void) const
{
	return m_nWidth / (m_Font.GetCharWidth () * m_nTextScale);
}

unsigned CVideoOut::GetRows (void) const
{
	return m_nHeight / (m_Font.GetCharHeight () * m_nTextScale);
}

void CVideoOut::Clear (unsigned nBuffer, u32 nColor)
{
	nColor = Native (nColor);
	for (unsigned y = 0; y < m_nHeight; y++)
	{
		u32 *p = Row (nBuffer, y);
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = nColor;
		}
	}
}

void CVideoOut::ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor)
{
	nColor = Native (nColor);
	unsigned nCharH = m_Font.GetCharHeight () * m_nTextScale;
	unsigned y0 = nFirstRow * nCharH;
	unsigned y1 = (nFirstRow + nRows) * nCharH;
	if (y1 > m_nHeight)
	{
		y1 = m_nHeight;
	}
	for (unsigned y = y0; y < y1; y++)
	{
		u32 *p = Row (nBuffer, y);
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = nColor;
		}
	}
}

void CVideoOut::DrawChar (unsigned nBuffer, unsigned nX, unsigned nY, char ch, u32 nFG, u32 nBG,
			  unsigned nTextScale, const CCharGenerator &Font)
{
	nFG = Native (nFG);
	nBG = Native (nBG);
	unsigned nCharW = Font.GetCharWidth ();
	unsigned nCharH = Font.GetCharHeight ();
	if (   nX + nCharW * nTextScale > m_nWidth
	    || nY + nCharH * nTextScale > m_nHeight)
	{
		return;
	}

	for (unsigned cy = 0; cy < nCharH; cy++)
	{
		CCharGenerator::TPixelLine Line = Font.GetPixelLine (ch, cy);
		for (unsigned sy = 0; sy < nTextScale; sy++)
		{
			u32 *p = Row (nBuffer, nY + cy * nTextScale + sy) + nX;
			for (unsigned cx = 0; cx < nCharW; cx++)
			{
				u32 nColor = Font.GetPixel (cx, Line) ? nFG : nBG;
				for (unsigned sx = 0; sx < nTextScale; sx++)
				{
					*p++ = nColor;
				}
			}
		}
	}
}

void CVideoOut::DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
			  u32 nFG, u32 nBG)
{
	unsigned nCharW = m_Font.GetCharWidth () * m_nTextScale;
	unsigned nCharH = m_Font.GetCharHeight () * m_nTextScale;
	for (unsigned i = 0; pText[i] != '\0' && nCol + i < GetColumns (); i++)
	{
		DrawChar (nBuffer, (nCol + i) * nCharW, nRow * nCharH, pText[i], nFG, nBG,
			  m_nTextScale, m_Font);
	}
}

void CVideoOut::DrawGame (const u32 *pSource)
{
	for (unsigned y = 0; y < GAME_H; y++)
	{
		DrawGameLineTo (m_nBack, pSource, y, m_pLine, m_pDark);
	}
}

void CVideoOut::DrawGameLineTo (unsigned nBuffer, const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark)
{
	const unsigned M = m_nM;
	const unsigned nGameW = GAME_W * M;

	// scanlines: bottom rows of each group darker (2 of 4 at 4x, 1 of 2 or 3)
	// (COMPOSITE: none; the game lines come through DrawNtscLine, this draws
	// the menus over the paused game)
	const unsigned nDarkRows = m_nFilter != FILTER_NONE && m_nFilter != FILTER_COMPOSITE
				   ? (M >= 4 ? M / 2 : (M >= 2 ? 1 : 0)) : 0;

	// expand one source line horizontally into cached memory, converting
	// the core's XRGB8888 (R high) to the framebuffer order if needed
	const u32 *pSrc = pSource + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
	u32 *pDst = pLine;
	if (m_bRLow)
	{
		for (unsigned x = 0; x < GAME_W; x++)
		{
			u32 p = pSrc[x];
			u32 c = 0xFF000000u | (p & 0x0000FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
			for (unsigned s = 0; s < M; s++)
			{
				*pDst++ = c;
			}
		}
	}
	else
	{
		for (unsigned x = 0; x < GAME_W; x++)
		{
			u32 c = 0xFF000000u | pSrc[x];
			for (unsigned s = 0; s < M; s++)
			{
				*pDst++ = c;
			}
		}
	}

	unsigned nDstY = m_nGameY + y * M;

	if (nDarkRows)
	{
		// shifts and masks only (75% = 50% + 25%)
		const u32 *pL = pLine;
		u32 *pD = pDark;
		switch (m_nFilter)
		{
		case FILTER_SCAN_LIGHT:
			for (unsigned x = 0; x < nGameW; x++)
			{
				u32 c = pL[x];
				pD[x] = 0xFF000000u | (((c >> 1) & 0x007F7F7Fu) + ((c >> 2) & 0x003F3F3Fu));
			}
			break;
		case FILTER_SCAN_STRONG:
			for (unsigned x = 0; x < nGameW; x++)
			{
				pD[x] = 0xFF000000u | ((pL[x] >> 2) & 0x003F3F3Fu);
			}
			break;
		default:
			for (unsigned x = 0; x < nGameW; x++)
			{
				pD[x] = 0xFF000000u | ((pL[x] >> 1) & 0x007F7F7Fu);
			}
			break;
		}
	}
	for (unsigned s = 0; s < M; s++)
	{
		const u32 *pSrcLine = s >= M - nDarkRows ? pDark : pLine;
		memcpy (Row (nBuffer, nDstY + s) + m_nGameX, pSrcLine, nGameW * 4);
	}
	FbFlush ();			// the line is in memory before the HVS reads it
}

void CVideoOut::DrawNtscLine (unsigned y, const u32 *pNtsc, unsigned nNtscW, u32 *pLine)
{
	const unsigned M = m_nM;
	const unsigned nGameW = GAME_W * M;
	const unsigned nBuffer = SharedLoad (m_bRacing) ? m_nBack ^ 1 : m_nBack;	// core 2

	// linear interpolation, 16.16: output pixel x samples the filtered line at
	// (x + 0.5) * nNtscW / nGameW - 0.5
	const u32 nStep = (nNtscW << 16) / nGameW;
	s32 nPos = (s32) (nStep / 2) - 0x8000;
	for (unsigned x = 0; x < nGameW; x++, nPos += nStep)
	{
		unsigned i = nPos < 0 ? 0 : (unsigned) nPos >> 16;
		u32 f = nPos < 0 ? 0 : ((unsigned) nPos >> 8) & 0xFF;	// weight of the next pixel
		u32 p = pNtsc[i];
		if (f && i + 1 < nNtscW)
		{
			u32 q = pNtsc[i + 1];
			u32 rb = (((p & 0xFF00FFu) * (256 - f) + (q & 0xFF00FFu) * f) >> 8) & 0xFF00FFu;
			u32 g = (((p & 0x00FF00u) * (256 - f) + (q & 0x00FF00u) * f) >> 8) & 0x00FF00u;
			p = rb | g;
		}
		pLine[x] = m_bRLow ? 0xFF000000u | (p & 0x0000FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16)
				   : 0xFF000000u | p;
	}

	unsigned nDstY = m_nGameY + y * M;
	for (unsigned s = 0; s < M; s++)
	{
		memcpy (Row (nBuffer, nDstY + s) + m_nGameX, pLine, nGameW * 4);
	}
	FbFlush ();			// the line is in memory before the HVS reads it
}

// ---------------------------------------------------------------- the border

static unsigned ISqrt (unsigned n)
{
	unsigned r = 0, b = 1u << 14;		// n < 2^28
	while (b > n) b >>= 2;
	while (b)
	{
		if (n >= r + b)
		{
			n -= r + b;
			r = (r >> 1) + b;
		}
		else
		{
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

// a pixel of the border: the grid line colour faded towards the base by
// v / 255 (255 = the line as it is)
static u32 MixLine (u32 nBase, u32 nLine, unsigned v)
{
	u32 c = 0xFF000000u;
	for (unsigned s = 0; s < 24; s += 8)
	{
		u32 a = (nBase >> s) & 0xFF, b = (nLine >> s) & 0xFF;
		c |= ((a * (255 - v) + b * v) / 255) << s;
	}
	return c;
}

u32 CVideoOut::BgPixel (unsigned x, unsigned y) const
{
	if (m_nBezel == BEZEL_BLACK)
	{
		return 0xFF000000u;
	}
	const int M = (int) m_nM, nCell = BG_CELL * M;
	const int dx = (int) x - (int) m_nGameX, dy = (int) y - (int) m_nGameY;
	int mx = dx % nCell, my = dy % nCell;
	if (mx < 0) mx += nCell;
	if (my < 0) my += nCell;
	if (mx >= M && my >= M)
	{
		return m_nBgBase;			// inside a cell: the fade does not show
	}

	unsigned v = 255;
	if (m_nBezel == BEZEL_MENU)
	{
		if (m_nCW)
		{
			int cx = (dx >= 0 ? dx / M : -((-dx + M - 1) / M)) + (int) m_nOX;
			int cy = (dy >= 0 ? dy / M : -((-dy + M - 1) / M)) + (int) m_nOY;
			if (cx < 0) cx = 0;
			if (cy < 0) cy = 0;
			if (cx >= (int) m_nCW) cx = (int) m_nCW - 1;
			if (cy >= (int) m_nCH) cy = (int) m_nCH - 1;
			v = m_Fade[m_pDist[cy * (int) m_nCW + cx] * 255 / (3 * FADE_UI)];
		}
	}
	else
	{
		// distance from the picture, in framebuffer pixels
		const int nW = GAME_W * M, nH = GAME_H * M;
		const unsigned ex = dx < 0 ? (unsigned) -dx : (dx >= nW ? (unsigned) (dx - nW + 1) : 0);
		const unsigned ey = dy < 0 ? (unsigned) -dy : (dy >= nH ? (unsigned) (dy - nH + 1) : 0);
		const unsigned R = FADE_SCREEN / m_nK;
		if (ex < R && ey < R)
		{
			unsigned d2 = ex * ex + ey * ey;
			if (d2 < R * R)
			{
				v = m_Fade[ISqrt (d2) * 255 / R];
			}
		}
	}
	return v >= 255 ? m_nBgLine : MixLine (m_nBgBase, m_nBgLine, v);
}

// the border inside a rectangle (never the picture)
void CVideoOut::BgRect (unsigned nBuf, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
	if (x1 > m_nWidth) x1 = m_nWidth;
	if (y1 > m_nHeight) y1 = m_nHeight;
	const unsigned gx0 = m_nGameX, gx1 = m_nGameX + GAME_W * m_nM;
	const unsigned gy0 = m_nGameY, gy1 = m_nGameY + GAME_H * m_nM;
	for (unsigned y = y0; y < y1; y++)
	{
		u32 *p = Row (nBuf, y);
		const boolean bGameRow = y >= gy0 && y < gy1;
		for (unsigned x = x0; x < x1; x++)
		{
			if (bGameRow && x >= gx0 && x < gx1)
			{
				x = gx1 - 1;
				continue;
			}
			p[x] = BgPixel (x, y);
		}
	}
}

void CVideoOut::FillBorder (unsigned nBuf)
{
	BgRect (nBuf, 0, 0, m_nWidth, m_nHeight);
	m_Text[nBuf][0].x1 = m_Text[nBuf][1].x1 = 0;
	if (m_nBezel == BEZEL_GAME && m_bBands)
	{
		DrawInfoLines (nBuf);
	}
	FbFlush ();
}

void CVideoOut::SetBezel (unsigned nMode, const TBezelInfo *pInfo, unsigned nId)
{
	if (!m_bGameMode || (m_bBezelDrawn && nMode == m_nBezel && nId == m_nBezelId))
	{
		return;
	}
	m_nBezel = nMode;
	m_nBezelId = nId;
	if (pInfo)
	{
		m_Info = *pInfo;
	}
	else
	{
		memset (&m_Info, 0, sizeof m_Info);
	}
	if (m_nCW)
	{
		memset (m_pDist, 3 * FADE_UI, m_nCW * m_nCH);	// no menu yet: no fade
	}
	m_nMenuSum[0] = m_nMenuSum[1] = 0;
	FillBorder (0);
	FillBorder (1);
	m_bBezelDrawn = TRUE;
}

void CVideoOut::BezelMenu (u32 *pUIBuf)
{
	if (!m_bGameMode || m_nBezel != BEZEL_MENU || m_nCW == 0)
	{
		return;
	}
	const unsigned W = m_nCW, H = m_nCH, R3 = 3 * FADE_UI;
	u8 *D = m_pDist;

	// the pattern (UI colours: R high)
	auto DimRH = [] (u32 c)
	{
		return ((((c >> 16) & 0xFF) * BG_DIM / 100) << 16)
		       | ((((c >> 8) & 0xFF) * BG_DIM / 100) << 8) | ((c & 0xFF) * BG_DIM / 100);
	};
	const u32 nBase = 0xFF000000u | DimRH (BG_BASE), nLine = 0xFF000000u | DimRH (BG_LINE);
	// background: the grid's two colours, or the pattern (a box drawn over
	// a screen that already went through here)
	auto IsBg = [nBase, nLine] (u32 c)
	{
		c &= 0xFFFFFFu;
		return c == BG_BASE || c == BG_LINE || c == (nBase & 0xFFFFFFu) || c == (nLine & 0xFFFFFFu);
	};

	// 0 on the menu's own pixels
	memset (D, R3, W * H);
	for (unsigned y = 0; y < GAME_H; y++)
	{
		const u32 *p = pUIBuf + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
		u8 *d = D + (m_nOY + y) * W + m_nOX;
		for (unsigned x = 0; x < GAME_W; x++)
		{
			if (!IsBg (p[x]))
			{
				d[x] = 0;
			}
		}
	}

	// chamfer distance, two passes (3 = a side, 4 = a diagonal), capped at R3
	for (unsigned y = 0; y < H; y++)
	{
		u8 *d = D + y * W;
		for (unsigned x = 0; x < W; x++)
		{
			unsigned v = d[x];
			if (v == 0) continue;
			if (x > 0 && d[x - 1] + 3u < v) v = d[x - 1] + 3u;
			if (y > 0)
			{
				const u8 *u = d - W;
				if (u[x] + 3u < v) v = u[x] + 3u;
				if (x > 0 && u[x - 1] + 4u < v) v = u[x - 1] + 4u;
				if (x + 1 < W && u[x + 1] + 4u < v) v = u[x + 1] + 4u;
			}
			d[x] = (u8) v;
		}
	}
	for (int y = (int) H - 1; y >= 0; y--)
	{
		u8 *d = D + y * W;
		for (int x = (int) W - 1; x >= 0; x--)
		{
			unsigned v = d[x];
			if (v == 0) continue;
			if (x + 1 < (int) W && d[x + 1] + 3u < v) v = d[x + 1] + 3u;
			if (y + 1 < (int) H)
			{
				const u8 *u = d + W;
				if (u[x] + 3u < v) v = u[x] + 3u;
				if (x > 0 && u[x - 1] + 4u < v) v = u[x - 1] + 4u;
				if (x + 1 < (int) W && u[x + 1] + 4u < v) v = u[x + 1] + 4u;
			}
			d[x] = (u8) v;
		}
	}

	// the menu's background becomes the border's pattern
	for (unsigned y = 0; y < GAME_H; y++)
	{
		u32 *p = pUIBuf + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
		const u8 *d = D + (m_nOY + y) * W + m_nOX;
		const boolean bLineRow = y % BG_CELL == 0;
		for (unsigned x = 0; x < GAME_W; x++)
		{
			if (!IsBg (p[x]))
			{
				continue;
			}
			if (!bLineRow && x % BG_CELL != 0)
			{
				p[x] = nBase;
				continue;
			}
			unsigned v = m_Fade[d[x] * 255 / R3];
			p[x] = v >= 255 ? nLine : MixLine (nBase, nLine, v);
		}
	}

	// the border of the back buffer, if what it shows near the menu changed
	u32 nSum = 2166136261u;
	const unsigned nReach = FADE_UI + 1;
	const unsigned y0 = m_nOY > nReach ? m_nOY - nReach : 0, y1 = m_nOY + GAME_H + nReach;
	const unsigned x0 = m_nOX > nReach ? m_nOX - nReach : 0, x1 = m_nOX + GAME_W + nReach;
	for (unsigned y = y0; y < y1 && y < H; y++)
	{
		for (unsigned x = x0; x < x1 && x < W; x++)
		{
			if (y >= m_nOY && y < m_nOY + GAME_H && x >= m_nOX && x < m_nOX + GAME_W)
			{
				x = m_nOX + GAME_W - 1;
				continue;
			}
			nSum = (nSum ^ D[y * W + x]) * 16777619u;
		}
	}
	nSum |= 1;
	if (m_nMenuSum[m_nBack] != nSum)
	{
		FillBorder (m_nBack);
		m_nMenuSum[m_nBack] = nSum;
	}
}

// ---------------------------------------------------------------- the text

unsigned CVideoOut::InfoWidth (const char *pText) const
{
	unsigned n = 0;
	for (const char *s = pText; *s; s++)
	{
		unsigned ch = (unsigned char) *s;
		n += g_InfoWidth[(ch >= 32 && ch < 128 ? ch : '?') - 32];
	}
	return n * m_nU;
}

unsigned CVideoOut::DrawInfoText (unsigned nBuf, unsigned x, unsigned y, const char *pText, u32 nColor)
{
	const unsigned U = m_nU;
	const u32 nInk = Native (RGB ((nColor >> 16) & 0xFF, (nColor >> 8) & 0xFF, nColor & 0xFF));
	for (const char *s = pText; *s; s++)
	{
		unsigned ch = (unsigned char) *s;
		if (ch < 32 || ch >= 128) ch = '?';
		const unsigned nAdv = g_InfoWidth[ch - 32] * U;
		if (x + nAdv + U > m_nWidth || y + 9 * U > m_nHeight)
		{
			break;
		}
		for (int pass = 0; pass < 2; pass++)		// the shadow, then the glyph
		{
			const u32 c = pass == 0 ? 0xFF000000u : nInk;
			const unsigned o = pass == 0 ? U : 0;
			for (unsigned r = 0; r < 8; r++)
			{
				const unsigned char nRow = g_InfoFont[ch - 32][r];
				for (unsigned k = 0; nRow && k < 8; k++)
				{
					if (!(nRow & (0x80 >> k)))
					{
						continue;
					}
					for (unsigned sy = 0; sy < U; sy++)
					{
						u32 *p = Row (nBuf, y + r * U + sy + o) + x + k * U + o;
						for (unsigned sx = 0; sx < U; sx++)
						{
							p[sx] = c;
						}
					}
				}
			}
		}
		x += nAdv;
	}
	return x;
}

// BEZEL_GAME at 1080p: title, maker and year above the picture; the buttons
// and SELECT below it
void CVideoOut::DrawInfoLines (unsigned nBuf)
{
	const unsigned U = m_nU, nGameW = GAME_W * m_nM, nGameH = GAME_H * m_nM;
	const unsigned nTop = (m_nGameY - TEXT_H * U) / 2;
	const unsigned nBelow = m_nHeight - m_nGameY - nGameH;
	const unsigned nBottom = m_nGameY + nGameH + (nBelow - TEXT_H * U) / 2;

	unsigned x = m_nGameX;
	if (m_Info.pTitle)
	{
		x = DrawInfoText (nBuf, x, nTop, m_Info.pTitle, INFO_WHITE);
	}
	if (m_Info.pMaker && m_Info.pMaker[0])
	{
		x = DrawInfoText (nBuf, x, nTop, "  ", INFO_GREY);
		DrawInfoText (nBuf, x, nTop, m_Info.pMaker, INFO_GREY);
	}

	// "A attack   B jump   C magic"; the same name on the buttons next to
	// each other once: "A B C rotate"
	x = m_nGameX;
	for (unsigned i = 0; i < 6 && m_Info.pButtons[i]; )
	{
		unsigned j = i + 1;
		while (j < 6 && m_Info.pButtons[j] && strcmp (m_Info.pButtons[j], m_Info.pButtons[i]) == 0)
		{
			j++;
		}
		if (i > 0)
		{
			x = DrawInfoText (nBuf, x, nBottom, "   ", INFO_GREY);
		}
		for (unsigned b = i; b < j; b++)
		{
			char Letter[3] = {(char) ('A' + b), ' ', '\0'};
			x = DrawInfoText (nBuf, x, nBottom, Letter, INFO_WHITE);
		}
		char Name[32];
		unsigned n = 0;
		for (const char *s = m_Info.pButtons[i]; *s && n < sizeof Name - 1; s++)
		{
			Name[n++] = *s >= 'A' && *s <= 'Z' ? (char) (*s + 32) : *s;
		}
		Name[n] = '\0';
		x = DrawInfoText (nBuf, x, nBottom, Name, INFO_GREY);
		i = j;
	}
	unsigned nSelW = InfoWidth ("SELECT coin");
	unsigned xs = m_nGameX + nGameW - nSelW;
	if (xs > x)
	{
		xs = DrawInfoText (nBuf, xs, nBottom, "SELECT", INFO_WHITE);
		DrawInfoText (nBuf, xs, nBottom, " coin", INFO_GREY);
	}
}

void CVideoOut::DrawSideText (boolean bRight, const TTextSpan *pSpans, unsigned nSpans)
{
	if (!m_bGameMode)
	{
		return;
	}
	const unsigned U = m_nU;
	const unsigned nBuf = SharedLoad (m_bRacing) ? m_nBack ^ 1 : m_nBack;
	TRect &Old = m_Text[nBuf][bRight ? 1 : 0];
	if (Old.x1)
	{
		BgRect (nBuf, Old.x0, Old.y0, Old.x1, Old.y1);
		Old.x1 = 0;
	}
	if (nSpans == 0)
	{
		FbFlush ();
		return;
	}

	// the lines, character by character with their colours
	static char Ch[2][SIDE_LINES][SIDE_CHARS + 1];
	static u32 Col[2][SIDE_LINES][SIDE_CHARS];
	char (*L)[SIDE_CHARS + 1] = Ch[bRight ? 1 : 0];
	u32 (*C)[SIDE_CHARS] = Col[bRight ? 1 : 0];
	unsigned nLen[SIDE_LINES] = {0};
	unsigned nLines = 1;
	for (unsigned i = 0; i < nSpans; i++)
	{
		for (const char *s = pSpans[i].pText; *s; s++)
		{
			unsigned l = nLines - 1;
			if (*s == '\n' && m_bSide)
			{
				if (nLines < SIDE_LINES) nLines++;
				continue;
			}
			if (*s == '\n')			// the bands: one line
			{
				for (unsigned k = 0; k < 2 && nLen[l] < SIDE_CHARS; k++)
				{
					L[l][nLen[l]] = ' ';
					C[l][nLen[l]++] = pSpans[i].nColor;
				}
				continue;
			}
			if (nLen[l] < SIDE_CHARS)
			{
				L[l][nLen[l]] = *s;
				C[l][nLen[l]++] = pSpans[i].nColor;
			}
		}
	}
	for (unsigned l = 0; l < nLines; l++)
	{
		L[l][nLen[l]] = '\0';
	}

	// too wide for the column: wrap at the last space that fits
	const unsigned nGameW = GAME_W * m_nM, nGameH = GAME_H * m_nM;
	const unsigned nMaxW = m_bSide ? m_nGameX - 2 * m_nGap : m_nWidth - 2 * m_nGap;
	for (unsigned l = 0; l < nLines && nLines < SIDE_LINES; l++)
	{
		if (InfoWidth (L[l]) <= nMaxW)
		{
			continue;
		}
		int nCut = -1;
		for (int k = (int) nLen[l] - 1; k > 0; k--)
		{
			if (L[l][k] == ' ')
			{
				char c = L[l][k];
				L[l][k] = '\0';
				boolean bFits = InfoWidth (L[l]) <= nMaxW;
				L[l][k] = c;
				if (bFits)
				{
					nCut = k;
					break;
				}
			}
		}
		if (nCut < 0 || !m_bSide)
		{
			continue;			// one long word: cut by the screen edge
		}
		for (unsigned m = nLines; m > l + 1; m--)
		{
			memcpy (L[m], L[m - 1], sizeof L[0]);
			memcpy (C[m], C[m - 1], sizeof C[0]);
			nLen[m] = nLen[m - 1];
		}
		nLines++;
		unsigned nRest = nLen[l] - (unsigned) nCut - 1;
		memmove (L[l + 1], &L[l][nCut + 1], nRest + 1);
		memmove (C[l + 1], &C[l][nCut + 1], nRest * sizeof (u32));
		nLen[l + 1] = nRest;
		L[l][nCut] = '\0';
		nLen[l] = (unsigned) nCut;
	}

	// where
	const unsigned nPitch = TEXT_PITCH * U;
	const unsigned nTextH = nLines * nPitch - (TEXT_PITCH - TEXT_H) * U;
	unsigned y;
	if (m_bSide)
	{
		y = bRight ? m_nColTop : (m_nColBot > nTextH ? m_nColBot - nTextH : 0);
	}
	else if (bRight)
	{
		y = m_nGameY > TEXT_H * U ? (m_nGameY - TEXT_H * U) / 2 : 0;
	}
	else
	{
		unsigned nBelow = m_nHeight - m_nGameY - nGameH;
		y = m_nGameY + nGameH + (nBelow > TEXT_H * U ? (nBelow - TEXT_H * U) / 2 : 0);
	}

	TRect New = {m_nWidth, y, 0, y + nLines * nPitch};
	for (unsigned l = 0; l < nLines; l++, y += nPitch)
	{
		const unsigned w = InfoWidth (L[l]);
		unsigned x;
		if (!m_bSide)		x = w < m_nWidth ? (m_nWidth - w) / 2 : 0;
		else if (bRight)	x = m_nGameX + nGameW + m_nGap;
		else			x = m_nGameX - m_nGap > w ? m_nGameX - m_nGap - w : 0;
		if (x < New.x0) New.x0 = x;
		unsigned x1 = x;
		for (unsigned k = 0; k < nLen[l]; k++)
		{
			char One[2] = {L[l][k], '\0'};
			x1 = DrawInfoText (nBuf, x1, y, One, C[l][k]);
		}
		if (x1 + U > New.x1) New.x1 = x1 + U;
	}
	if (New.x1 > New.x0)
	{
		Old = New;
	}
	FbFlush ();
}

void CVideoOut::ShowBuffer (unsigned nBuffer)
{
	m_pFrameBuffer->SetVirtualOffset (0, nBuffer * m_nHeight);
	m_nBack = nBuffer ^ 1;
}

boolean CVideoOut::Flip (void)
{
	boolean bOK = m_pFrameBuffer->SetVirtualOffset (0, m_nBack * m_nHeight);
	m_nBack ^= 1;
	return bOK;
}

boolean CVideoOut::WaitForVSync (void)
{
	return m_pFrameBuffer->WaitForVerticalSync ();
}
