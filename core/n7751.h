/*
 * s16-pi - the uPD7751 (an 8048 with 1 KB of ROM and 64 bytes of RAM) and the
 * 8243 port expander beside it: System 16A's sample player.
 *
 * Instructions, timer and interrupts as MAME's mcs48.cpp and i8243.cpp (Dan
 * Boris, Mirko Buffoni, Aaron Giles), re-implemented here.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N7751_H
#define N7751_H

#include <stdint.h>

struct TN7751Bus
{
	void *pCtx;
	uint8_t (*pBusRead) (void *pCtx);			// INS A,BUS
	uint8_t (*pP2Read) (void *pCtx);			// (P20-P23 are the 8243's)
	void (*pP1Write) (void *pCtx, uint8_t nData);
	void (*pExpWrite) (void *pCtx, int nPort, uint8_t nData);	// 8243 ports 4-7
};

struct TN7751
{
	const uint8_t *pRom;
	TN7751Bus Bus;

	uint16_t nPC, nPrevPC, nA11;
	uint8_t nA, nPSW, nTimer, nPrescaler, nP1, nP2;
	uint8_t Ram[64];
	bool bF1, bIrq, bIrqPolled, bInIrq, bTimerOverflow, bTimerFlag, bTirqOn, bXirqOn, bTimerOn;
	bool bReset;				// held in reset
	int nCycles;				// owed (negative: ran ahead)
	// 8243
	uint8_t nExpOp, ExpPort[4], nExpP2;
	bool bProg;
};

void n7751_init (TN7751 *p, const uint8_t *pRom, const TN7751Bus *pBus);
void n7751_reset (TN7751 *p);
void n7751_set_reset (TN7751 *p, bool bHeld);
void n7751_set_irq (TN7751 *p, bool bOn);
void n7751_run (TN7751 *p, int nCycles);	// machine cycles (15 clocks each)
uint8_t n7751_exp_p2 (const TN7751 *p);		// what the 8243 drives on P20-P23

#endif
