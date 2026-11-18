/*
 * s16-pi - the uPD7751 and its 8243 (see n7751.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "n7751.h"
#include <string.h>

enum { C_FLAG = 0x80, A_FLAG = 0x40, F_FLAG = 0x20, B_FLAG = 0x10 };
enum { EXP_READ, EXP_WRITE, EXP_OR, EXP_AND };

static inline uint8_t Rom (const TN7751 *p, unsigned a)	{ return p->pRom[a & 0x3ff]; }
static inline uint8_t &Ram (TN7751 *p, unsigned a)	{ return p->Ram[a & 0x3f]; }
static inline uint8_t &Reg (TN7751 *p, unsigned n)	{ return p->Ram[((p->nPSW & B_FLAG) ? 24 : 0) + n]; }

static inline uint8_t Fetch (TN7751 *p)
{
	const uint16_t a = p->nPC;
	p->nPC = (uint16_t) (((p->nPC + 1) & 0x7ff) | (p->nPC & 0x800));
	return Rom (p, a);
}

// ---- the 8243 ----------------------------------------------------------------

static void ExpP2 (TN7751 *p, uint8_t d)	{ p->nExpP2 = d & 0x0f; }

static void ExpProg (TN7751 *p, bool bState)
{
	if (p->bProg && !bState)		// high to low: the opcode and port
	{
		p->nExpOp = p->nExpP2;
	}
	else if (!p->bProg && bState)		// low to high: the operation
	{
		const int n = p->nExpOp & 3;
		uint8_t &r = p->ExpPort[n];
		switch (p->nExpOp >> 2)
		{
		case EXP_WRITE:	r = p->nExpP2;		break;
		case EXP_OR:	r |= p->nExpP2;		break;
		case EXP_AND:	r &= p->nExpP2;		break;
		default:	return;
		}
		p->Bus.pExpWrite (p->Bus.pCtx, 4 + n, r);
	}
	p->bProg = bState;
}

uint8_t n7751_exp_p2 (const TN7751 *p)
{
	// a read: the port (its inputs are not connected)
	return (p->nExpOp >> 2) == EXP_READ ? 0x0f : 0x0f;
}

// ---- helpers, as mcs48.cpp ----------------------------------------------------

static void PortP2 (TN7751 *p, uint8_t d)	{ p->nP2 = d; ExpP2 (p, d); }

static void PushPCPSW (TN7751 *p)
{
	const uint8_t sp = p->nPSW & 0x07;
	Ram (p, 8 + 2 * sp) = (uint8_t) p->nPC;
	Ram (p, 9 + 2 * sp) = (uint8_t) (((p->nPC >> 8) & 0x0f) | (p->nPSW & 0xf0));
	p->nPSW = (uint8_t) ((p->nPSW & 0xf0) | ((sp + 1) & 0x07));
}

static void PullPC (TN7751 *p, bool bPSW)
{
	const uint8_t sp = (p->nPSW - 1) & 0x07;
	uint16_t pc = (uint16_t) (Ram (p, 8 + 2 * sp) | Ram (p, 9 + 2 * sp) << 8);
	if (bPSW)	p->nPSW = (uint8_t) (((pc >> 8) & 0xf0) | sp);
	else		p->nPSW = (uint8_t) ((p->nPSW & 0xf0) | sp);
	p->nPC = pc & (p->bInIrq ? 0x7ff : 0xfff);
}

static void Add (TN7751 *p, uint8_t d, bool bCarry)
{
	const unsigned c = bCarry ? (p->nPSW & C_FLAG) >> 7 : 0;
	const unsigned t = p->nA + d + c, t4 = (p->nA & 0x0f) + (d & 0x0f) + c;
	p->nPSW = (uint8_t) ((p->nPSW & ~(C_FLAG | A_FLAG)) | ((t4 << 2) & A_FLAG) | ((t >> 1) & C_FLAG));
	p->nA = (uint8_t) t;
}

static void Jmp (TN7751 *p, uint16_t a)		{ p->nPC = (uint16_t) (a | (p->bInIrq ? 0 : p->nA11)); }
static void Call (TN7751 *p, uint16_t a)	{ PushPCPSW (p); Jmp (p, a); }
static void Jcc (TN7751 *p, bool b)
{
	const uint16_t pch = p->nPC & 0xf00;
	const uint8_t o = Fetch (p);
	if (b) p->nPC = pch | o;
}

static void Expander (TN7751 *p, int nOp, int nPort)
{
	PortP2 (p, (uint8_t) ((p->nP2 & 0xf0) | nOp << 2 | (nPort & 3)));
	ExpProg (p, false);
	if (nOp != EXP_READ)
	{
		PortP2 (p, (uint8_t) ((p->nP2 & 0xf0) | (p->nA & 0x0f)));
	}
	else
	{
		PortP2 (p, (uint8_t) (p->nP2 | 0x0f));
		p->nA = p->Bus.pP2Read (p->Bus.pCtx) & 0x0f;
	}
	ExpProg (p, true);
}

static void Burn (TN7751 *p, int n)
{
	if (p->bTimerOn)
	{
		const uint8_t nOld = p->nTimer;
		p->nPrescaler = (uint8_t) (p->nPrescaler + n);
		p->nTimer = (uint8_t) (p->nTimer + (p->nPrescaler >> 5));
		p->nPrescaler &= 0x1f;
		if (nOld != 0 && p->nTimer == 0)
		{
			p->bTimerFlag = true;
			if (p->bTirqOn) p->bTimerOverflow = true;
		}
	}
	p->nCycles -= n;
}

// ---- the interface ----------------------------------------------------------------

void n7751_init (TN7751 *p, const uint8_t *pRom, const TN7751Bus *pBus)
{
	memset (p, 0, sizeof *p);
	p->pRom = pRom;
	p->Bus = *pBus;
	p->bProg = true;
	n7751_reset (p);
}

void n7751_reset (TN7751 *p)
{
	p->nPC = 0;
	p->nPSW &= C_FLAG | A_FLAG;
	p->bF1 = false;
	p->nA11 = 0;
	p->nP1 = 0xff;
	p->Bus.pP1Write (p->Bus.pCtx, 0xff);
	PortP2 (p, 0xff);
	p->bTirqOn = p->bXirqOn = p->bTimerOn = false;
	p->bTimerFlag = false;
	p->bInIrq = p->bTimerOverflow = p->bIrqPolled = false;
}

void n7751_set_reset (TN7751 *p, bool bHeld)
{
	if (bHeld && !p->bReset) n7751_reset (p);
	p->bReset = bHeld;
}

void n7751_set_irq (TN7751 *p, bool bOn)	{ p->bIrq = bOn; }

static void CheckIrqs (TN7751 *p)
{
	if (p->bInIrq)
	{
		return;
	}
	if (p->bIrq && p->bXirqOn)
	{
		Burn (p, 2);
		p->bInIrq = true;
		if (p->bIrqPolled)		// JNI is taken (as MAME)
		{
			p->nPC = (uint16_t) (((p->nPrevPC + 1) & 0x7ff) | (p->nPrevPC & 0x800));
			Jcc (p, true);
		}
		Call (p, 0x03);
	}
	else if (p->bTimerOverflow && p->bTirqOn)
	{
		Burn (p, 2);
		p->bInIrq = true;
		Call (p, 0x07);
		p->bTimerOverflow = false;
	}
}

void n7751_run (TN7751 *p, int nCycles)
{
	p->nCycles += nCycles;
	if (p->bReset)
	{
		p->nCycles = 0;
		return;
	}
	while (p->nCycles > 0)
	{
		CheckIrqs (p);
		p->bIrqPolled = false;
		p->nPrevPC = p->nPC;
		const uint8_t op = Fetch (p);
		uint8_t t;
		switch (op)
		{
		// arithmetic
		case 0x03: Burn (p, 2); Add (p, Fetch (p), false); break;
		case 0x13: Burn (p, 2); Add (p, Fetch (p), true); break;
		case 0x60: case 0x61: Burn (p, 1); Add (p, Ram (p, Reg (p, op & 1)), false); break;
		case 0x70: case 0x71: Burn (p, 1); Add (p, Ram (p, Reg (p, op & 1)), true); break;
		case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
			Burn (p, 1); Add (p, Reg (p, op & 7), false); break;
		case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
			Burn (p, 1); Add (p, Reg (p, op & 7), true); break;
		case 0x53: Burn (p, 2); p->nA &= Fetch (p); break;
		case 0x50: case 0x51: Burn (p, 1); p->nA &= Ram (p, Reg (p, op & 1)); break;
		case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
			Burn (p, 1); p->nA &= Reg (p, op & 7); break;
		case 0x43: Burn (p, 2); p->nA |= Fetch (p); break;
		case 0x40: case 0x41: Burn (p, 1); p->nA |= Ram (p, Reg (p, op & 1)); break;
		case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
			Burn (p, 1); p->nA |= Reg (p, op & 7); break;
		case 0xd3: Burn (p, 2); p->nA ^= Fetch (p); break;
		case 0xd0: case 0xd1: Burn (p, 1); p->nA ^= Ram (p, Reg (p, op & 1)); break;
		case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
			Burn (p, 1); p->nA ^= Reg (p, op & 7); break;
		case 0x17: Burn (p, 1); p->nA++; break;
		case 0x07: Burn (p, 1); p->nA--; break;
		case 0x10: case 0x11: Burn (p, 1); Ram (p, Reg (p, op & 1))++; break;
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			Burn (p, 1); Reg (p, op & 7)++; break;
		case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
			Burn (p, 1); Reg (p, op & 7)--; break;
		case 0x27: Burn (p, 1); p->nA = 0; break;
		case 0x37: Burn (p, 1); p->nA ^= 0xff; break;
		case 0x57:			// DA A
			Burn (p, 1);
			if ((p->nA & 0x0f) > 0x09 || (p->nPSW & A_FLAG))
			{
				if (p->nA > 0xf9) p->nPSW |= C_FLAG;
				p->nA = (uint8_t) (p->nA + 0x06);
			}
			if ((p->nA & 0xf0) > 0x90 || (p->nPSW & C_FLAG))
			{
				p->nA = (uint8_t) (p->nA + 0x60);
				p->nPSW |= C_FLAG;
			}
			break;
		case 0x47: Burn (p, 1); p->nA = (uint8_t) (p->nA << 4 | p->nA >> 4); break;
		case 0xe7: Burn (p, 1); p->nA = (uint8_t) (p->nA << 1 | p->nA >> 7); break;
		case 0x77: Burn (p, 1); p->nA = (uint8_t) (p->nA >> 1 | p->nA << 7); break;
		case 0xf7: Burn (p, 1); t = p->nA & C_FLAG; p->nA = (uint8_t) (p->nA << 1 | p->nPSW >> 7);
			p->nPSW = (uint8_t) ((p->nPSW & ~C_FLAG) | t); break;
		case 0x67: Burn (p, 1); t = (uint8_t) ((p->nA << 7) & C_FLAG); p->nA = (uint8_t) (p->nA >> 1 | (p->nPSW & C_FLAG));
			p->nPSW = (uint8_t) ((p->nPSW & ~C_FLAG) | t); break;
		// flags
		case 0x97: Burn (p, 1); p->nPSW &= ~C_FLAG; break;
		case 0xa7: Burn (p, 1); p->nPSW ^= C_FLAG; break;
		case 0x85: Burn (p, 1); p->nPSW &= ~F_FLAG; break;
		case 0x95: Burn (p, 1); p->nPSW ^= F_FLAG; break;
		case 0xa5: Burn (p, 1); p->bF1 = false; break;
		case 0xb5: Burn (p, 1); p->bF1 = !p->bF1; break;
		// moves
		case 0x23: Burn (p, 2); p->nA = Fetch (p); break;
		case 0xc7: Burn (p, 1); p->nA = p->nPSW | 0x08; break;
		case 0xd7: Burn (p, 1); p->nPSW = p->nA & ~0x08; break;
		case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
			Burn (p, 1); p->nA = Reg (p, op & 7); break;
		case 0xf0: case 0xf1: Burn (p, 1); p->nA = Ram (p, Reg (p, op & 1)); break;
		case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
			Burn (p, 1); Reg (p, op & 7) = p->nA; break;
		case 0xa0: case 0xa1: Burn (p, 1); Ram (p, Reg (p, op & 1)) = p->nA; break;
		case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
			Burn (p, 2); Reg (p, op & 7) = Fetch (p); break;
		case 0xb0: case 0xb1: Burn (p, 2); t = Fetch (p); Ram (p, Reg (p, op & 1)) = t; break;
		case 0x42: Burn (p, 1); p->nA = p->nTimer; break;
		case 0x62: Burn (p, 1); p->nTimer = p->nA; break;
		case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
			Burn (p, 1); t = p->nA; p->nA = Reg (p, op & 7); Reg (p, op & 7) = t; break;
		case 0x20: case 0x21: Burn (p, 1); t = p->nA; p->nA = Ram (p, Reg (p, op & 1)); Ram (p, Reg (p, op & 1)) = t; break;
		case 0x30: case 0x31:
		{
			Burn (p, 1);
			uint8_t &r = Ram (p, Reg (p, op & 1));
			const uint8_t o = r;
			r = (uint8_t) ((o & 0xf0) | (p->nA & 0x0f));
			p->nA = (uint8_t) ((p->nA & 0xf0) | (o & 0x0f));
			break;
		}
		case 0xa3: Burn (p, 2); p->nA = Rom (p, (p->nPC & 0xf00) | p->nA); break;
		case 0xe3: Burn (p, 2); p->nA = Rom (p, 0x300 | p->nA); break;
		case 0x80: case 0x81: Burn (p, 2); p->nA = 0xff; break;		// MOVX (nothing there)
		case 0x90: case 0x91: Burn (p, 2); break;
		// ports
		case 0x08: Burn (p, 2); p->nA = p->Bus.pBusRead (p->Bus.pCtx); break;	// INS A,BUS
		case 0x02: Burn (p, 2); break;						// OUTL BUS,A
		case 0x88: case 0x98: Burn (p, 2); Fetch (p); break;			// ORL / ANL BUS
		case 0x09: Burn (p, 2); p->nA = p->nP1; break;				// IN A,P1: as written
		case 0x0a: Burn (p, 2); p->nA = p->Bus.pP2Read (p->Bus.pCtx) & p->nP2; break;
		case 0x39: Burn (p, 2); p->nP1 = p->nA; p->Bus.pP1Write (p->Bus.pCtx, p->nP1); break;
		case 0x3a: Burn (p, 2); PortP2 (p, p->nA); break;
		case 0x89: Burn (p, 2); p->nP1 |= Fetch (p); p->Bus.pP1Write (p->Bus.pCtx, p->nP1); break;
		case 0x99: Burn (p, 2); p->nP1 &= Fetch (p); p->Bus.pP1Write (p->Bus.pCtx, p->nP1); break;
		case 0x8a: Burn (p, 2); PortP2 (p, (uint8_t) (p->nP2 | Fetch (p))); break;
		case 0x9a: Burn (p, 2); PortP2 (p, (uint8_t) (p->nP2 & Fetch (p))); break;
		case 0x0c: case 0x0d: case 0x0e: case 0x0f: Burn (p, 2); Expander (p, EXP_READ, op & 3); break;
		case 0x3c: case 0x3d: case 0x3e: case 0x3f: Burn (p, 2); Expander (p, EXP_WRITE, op & 3); break;
		case 0x8c: case 0x8d: case 0x8e: case 0x8f: Burn (p, 2); Expander (p, EXP_OR, op & 3); break;
		case 0x9c: case 0x9d: case 0x9e: case 0x9f: Burn (p, 2); Expander (p, EXP_AND, op & 3); break;
		// jumps
		case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xa4: case 0xc4: case 0xe4:
			Burn (p, 2); t = Fetch (p); Jmp (p, (uint16_t) ((op >> 5) << 8 | t)); break;
		case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xb4: case 0xd4: case 0xf4:
			Burn (p, 2); t = Fetch (p); Call (p, (uint16_t) ((op >> 5) << 8 | t)); break;
		case 0x83: Burn (p, 2); PullPC (p, false); break;
		case 0x93: Burn (p, 2); p->bInIrq = false; PullPC (p, true); break;
		case 0xb3: Burn (p, 2); p->nPC = (uint16_t) ((p->nPC & 0xf00) | Rom (p, (p->nPC & 0xf00) | p->nA)); break;
		case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xb2: case 0xd2: case 0xf2:
			Burn (p, 2); Jcc (p, (p->nA >> (op >> 5)) & 1); break;
		case 0xf6: Burn (p, 2); Jcc (p, (p->nPSW & C_FLAG) != 0); break;
		case 0xe6: Burn (p, 2); Jcc (p, (p->nPSW & C_FLAG) == 0); break;
		case 0xb6: Burn (p, 2); Jcc (p, (p->nPSW & F_FLAG) != 0); break;
		case 0x76: Burn (p, 2); Jcc (p, p->bF1); break;
		case 0x86: Burn (p, 2); p->bIrqPolled = !p->bIrq; Jcc (p, p->bIrq); break;	// JNI: /INT low
		case 0x16: Burn (p, 2); Jcc (p, p->bTimerFlag); p->bTimerFlag = false; break;
		case 0x26: case 0x46: Burn (p, 2); Jcc (p, true); break;	// JNT0, JNT1: both low
		case 0x36: case 0x56: Burn (p, 2); Jcc (p, false); break;	// JT0, JT1
		case 0x96: Burn (p, 2); Jcc (p, p->nA != 0); break;
		case 0xc6: Burn (p, 2); Jcc (p, p->nA == 0); break;
		case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
			Burn (p, 2); Jcc (p, --Reg (p, op & 7) != 0); break;
		// control
		case 0x05: Burn (p, 1); p->bXirqOn = true; break;
		case 0x15: Burn (p, 1); p->bXirqOn = false; break;
		case 0x25: Burn (p, 1); p->bTirqOn = true; break;
		case 0x35: Burn (p, 1); p->bTirqOn = false; p->bTimerOverflow = false; break;
		case 0x45: Burn (p, 1); p->bTimerOn = false; break;	// STRT CNT: T1 is low, never counts
		case 0x55: Burn (p, 1); p->bTimerOn = true; p->nPrescaler = 0; break;
		case 0x65: Burn (p, 1); p->bTimerOn = false; break;
		case 0x75: Burn (p, 1); break;				// ENT0 CLK
		case 0xc5: Burn (p, 1); p->nPSW &= ~B_FLAG; break;
		case 0xd5: Burn (p, 1); p->nPSW |= B_FLAG; break;
		case 0xe5: Burn (p, 1); p->nA11 = 0x000; break;
		case 0xf5: Burn (p, 1); p->nA11 = 0x800; break;
		default:   Burn (p, 1); break;				// NOP and the illegal ones
		}
	}
}
