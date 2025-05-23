
/*-------------------------------------------------------------*/
/*--- Private header file for the library.                  ---*/
/*---                                       bzlib_private.h ---*/
/*-------------------------------------------------------------*/

/* ------------------------------------------------------------------
   This file is part of bzip2/libbzip2, a program and library for
   lossless, block-sorting data compression.

   bzip2/libbzip2 version 1.1.0 of 6 September 2010
   Copyright (C) 1996-2010 Julian Seward <jseward@acm.org>

   Please read the WARNING, DISCLAIMER and PATENTS sections in the
   README file.

   This program is released under the terms of the license contained
   in the file COPYING.
   ------------------------------------------------------------------ */


#ifndef _BZLIB_PRIVATE_H
#define _BZLIB_PRIVATE_H

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "bzlib.h"



/*-- General stuff. --*/

typedef char            Char;
typedef unsigned char   Bool;
typedef unsigned char   UChar;
typedef int             Int32;
typedef unsigned int    UInt32;
typedef short           Int16;
typedef unsigned short  UInt16;

static const unsigned char True =  ((Bool)1);
static const unsigned char False = ((Bool)0);

static const int BUFFER_SIZE = 5000;

#define BZALLOC(nnn) (strm->bzalloc)(strm->opaque,(nnn),1)
#define BZFREE(ppp)  (strm->bzfree)(strm->opaque,(ppp))


/*-- Header bytes. --*/

static int const BZ_HDR_B = 0x42;  /* 'B' */
static int const BZ_HDR_Z = 0x5a;  /* 'Z' */
static int const BZ_HDR_h = 0x68;  /* 'h' */
static int const BZ_HDR_0 = 0x30;  /* '0' */

/*-- Constants for the back end. --*/

static const int BZ_MAX_ALPHA_SIZE = 258;
static const int BZ_MAX_CODE_LEN   = 23;

static const int BZ_RUNA = 0;
static const int BZ_RUNB = 1;

static const int BZ_N_GROUPS  = 6;
static const int BZ_G_SIZE    = 50;
static const int BZ_N_ITERS   = 4;

#define BZ_MAX_SELECTORS (2 + (900000 / BZ_G_SIZE))



/*-- Stuff for randomising repetitive blocks. --*/

extern Int32 BZ2_rNums[512];

#define BZ_RAND_DECLS                          \
   Int32 rNToGo;                               \
   Int32 rTPos                                 \

#define BZ_RAND_INIT_MASK                      \
   s->rNToGo = 0;                              \
   s->rTPos  = 0                               \

#define BZ_RAND_MASK ((s->rNToGo == 1) ? 1 : 0)

#define BZ_RAND_UPD_MASK                       \
   if (s->rNToGo == 0) {                       \
      s->rNToGo = BZ2_rNums[s->rTPos];         \
      s->rTPos += 1;                              \
      if (s->rTPos == 512) s->rTPos = 0;       \
   }                                           \
   s->rNToGo -= 1;



/*-- Stuff for doing CRCs. --*/

extern UInt32 BZ2_crc32Table[256];

static const int BZ_INITIALISE_CRC = 0xffffffffL;

// nach bzlib verschoben
extern void BZ_FINALISE_CRC (unsigned int *crcVar);

// nach bzlib verschoben
extern void BZ_UPDATE_CRC(uint32_t *crcVar, uint8_t cha); // Nur Deklaration!

/*-- States and modes for compression. --*/

typedef enum {
  BZ_MODUS_IDLE = 1,
  BZ_MODUS_RUNNING = 2,
  BZ_MODUS_FLUSHING = 3,
  BZ_MODUS_FINISHING = 4
} BZ_MODUS;

static const int BZ_S_OUTPUT = 1;
static const int BZ_S_INPUT = 2;

static const int BZ_N_RADIX = 2;
static const int BZ_N_QSORT = 12;
static const int BZ_N_SHELL = 18;
#define BZ_N_OVERSHOOT (BZ_N_RADIX + BZ_N_QSORT + BZ_N_SHELL + 2)




/*-- Structure holding all the compression-side stuff. --*/
typedef struct {
  /* pointer back to the struct bz_stream */
  bz_stream* strm;
  
  /* mode this stream is in, and whether inputting */
  /* or outputting data */
  BZ_MODUS modus;
  Bool    statusInputEqualsTrueVsOutputEqualsFalse; // C: 0==falsch
  
  /* remembers avail_in when flush/finish requested */
  UInt32   avail_in_expect;
  
  /* for doing the block sorting */
  UInt32*  arr1;
  UInt32*  arr2;
  UInt32*  ftab;
  Int32    origPtr;
  
  /* aliases for arr1 and arr2 */
  UInt32*  ptr;
  UChar*   block;
  UInt16*  mtfv;
  UChar*   zbits;
  
  /* for deciding when to use the fallback sorting algorithm */
  Int32    workFactor;
  
  /* run-length-encoding of the input */
  UInt32   state_in_ch;
  Int32    state_in_len;
  BZ_RAND_DECLS;
  
  /* input and output limits and current posns */
  Int32    nblock;
  Int32    nblockMAX;
  Int32    numZ;
  Int32    state_out_pos;
  
  /* map of bytes used in block */
  Int32    nInUse;
  Bool     inUse[256];
  UChar    unseqToSeq[256];
  
  /* the buffer for bit stream creation */
  UInt32   bsBuff;
  Int32    bsLive;
  
  /* block and combined CRCs */
  UInt32   blockCRC;
  UInt32   combinedCRC;
  
  /* misc administratium */
  Int32    blockNo;
  Int32    blockSize100k;
  
  /* stuff for coding the MoveToFront values */
  Int32    nMoveToFront;
  Int32    moveToFrontFreq     [BZ_MAX_ALPHA_SIZE];
  UChar    selector            [BZ_MAX_SELECTORS];
  UChar    selectorMoveToFront [BZ_MAX_SELECTORS];
  
  UChar    len     [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  Int32    code    [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  Int32    rfreq   [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  /* second dimension: only 3 needed; 4 makes index calculations faster */
  UInt32   len_pack[BZ_MAX_ALPHA_SIZE][4];
  
}
EState;



/*-- externs for compression. --*/

extern void BZ2_blockSort ( EState* );

extern void BZ2_compressBlock ( EState*, Bool );

extern void BZ2_bsInitWrite ( EState* );

extern void BZ2_hbAssignCodes ( Int32*, UChar*, Int32, Int32, Int32 );

extern void BZ2_hbMakeCodeLengths ( UChar*, Int32*, Int32, Int32 );



/*-- states for decompression. --*/

static const int  BZ_X_IDLE       = 1;
static const int  BZ_X_OUTPUT     = 2;

static const int  BZ_X_MAGIC_1    = 10;
static const int  BZ_X_MAGIC_2    = 11;
static const int  BZ_X_MAGIC_3    = 12;
static const int  BZ_X_MAGIC_4    = 13;
static const int  BZ_X_BLKHDR_1   = 14;
static const int  BZ_X_BLKHDR_2   = 15;
static const int  BZ_X_BLKHDR_3   = 16;
static const int  BZ_X_BLKHDR_4   = 17;
static const int  BZ_X_BLKHDR_5   = 18;
static const int  BZ_X_BLKHDR_6   = 19;
static const int  BZ_X_BCRC_1     = 20;
static const int  BZ_X_BCRC_2     = 21;
static const int  BZ_X_BCRC_3     = 22;
static const int  BZ_X_BCRC_4     = 23;
static const int  BZ_X_RANDBIT    = 24;
static const int  BZ_X_ORIGPTR_1  = 25;
static const int  BZ_X_ORIGPTR_2  = 26;
static const int  BZ_X_ORIGPTR_3  = 27;
static const int  BZ_X_MAPPING_1  = 28;
static const int  BZ_X_MAPPING_2  = 29;
static const int  BZ_X_SELECTOR_1 = 30;
static const int  BZ_X_SELECTOR_2 = 31;
static const int  BZ_X_SELECTOR_3 = 32;
static const int  BZ_X_CODING_1   = 33;
static const int  BZ_X_CODING_2   = 34;
static const int  BZ_X_CODING_3   = 35;
static const int  BZ_X_MTF_1      = 36;
static const int  BZ_X_MTF_2      = 37;
static const int  BZ_X_MTF_3      = 38;
static const int  BZ_X_MTF_4      = 39;
static const int  BZ_X_MTF_5      = 40;
static const int  BZ_X_MTF_6      = 41;
static const int  BZ_X_ENDHDR_2   = 42;
static const int  BZ_X_ENDHDR_3   = 43;
static const int  BZ_X_ENDHDR_4   = 44;
static const int  BZ_X_ENDHDR_5   = 45;
static const int  BZ_X_ENDHDR_6   = 46;
static const int  BZ_X_CCRC_1     = 47;
static const int  BZ_X_CCRC_2     = 48;
static const int  BZ_X_CCRC_3     = 49;
static const int  BZ_X_CCRC_4     = 50;


/*-- Constants for the fast MoveToFront decoder. --*/

static const int MOVE_TO_FRONT_A_SIZE = 4096;
static const int MOVE_TO_FRONT_L_SIZE = 16;


/*-- Structure holding all the decompression-side stuff. --*/

typedef struct {
  /* pointer back to the struct bz_stream */
  bz_stream* strm;
  
  /* state indicator for this stream */
  Int32    state;
  
  /* for doing the final run-length decoding */
  UChar    state_out_ch;
  Int32    state_out_len;
  Bool     blockRandomised;
  BZ_RAND_DECLS;
  
  /* the buffer for bit stream reading */
  UInt32   bsBuff;
  Int32    bsLive;
  
  /* misc administratium */
  Int32    blockSize100k;
  Bool     smallDecompress;
  Int32    currBlockNo;
  
  /* for undoing the Burrows-Wheeler transform */
  Int32    origPtr;
  UInt32   tPos;
  Int32    k0;
  Int32    unzftab[256];
  Int32    nblock_used;
  Int32    cftab[257];
  Int32    cftabCopy[257];
  
  /* for undoing the Burrows-Wheeler transform (FAST) */
  UInt32   *tt;
  
  /* for undoing the Burrows-Wheeler transform (SMALL) */
  UInt16   *ll16;
  UChar    *ll4;
  
  /* stored and calculated CRCs */
  UInt32   storedBlockCRC;
  UInt32   storedCombinedCRC;
  UInt32   calculatedBlockCRC;
  UInt32   calculatedCombinedCRC;
  
  /* map of bytes used in block */
  Int32    nInUse;
  Bool     inUse[256];
  Bool     inUse16[16];
  UChar    seqToUnseq[256];
  
  /* for decoding the MoveToFront values */
  UChar    moveToFront_a   [MOVE_TO_FRONT_A_SIZE];
  Int32    moveToFrontBase[256 / MOVE_TO_FRONT_L_SIZE];
  UChar    selector   [BZ_MAX_SELECTORS];
  UChar    selectorMoveToFront [BZ_MAX_SELECTORS];
  UChar    len  [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  
  Int32    limit  [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  Int32    base   [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  Int32    perm   [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
  Int32    minLens[BZ_N_GROUPS];
  
  /* save area for scalars in the main decompress code */
  Int32    save_i;
  Int32    save_j;
  Int32    save_t;
  Int32    save_alphaSize;
  Int32    save_nGroups;
  Int32    save_nSelectors;
  Int32    save_EOB;
  Int32    save_groupNo;
  Int32    save_groupPos;
  Int32    save_nextSym;
  Int32    save_nblockMAX;
  Int32    save_nblock;
  Int32    save_es;
  Int32    save_N;
  Int32    save_curr;
  Int32    save_zt;
  Int32    save_zn;
  Int32    save_zvec;
  Int32    save_zj;
  Int32    save_gSel;
  Int32    save_gMinlen;
  Int32*   save_gLimit;
  Int32*   save_gBase;
  Int32*   save_gPerm;
  
}
DState;



/*-- Macros for decompression. --*/

#define BZ_GET_FAST(cccc)                     \
    /* c_tPos is unsigned, hence test < 0 is pointless. */ \
    if (s->tPos >= (UInt32)100000 * (UInt32)s->blockSize100k) return True; \
    s->tPos = s->tt[s->tPos];                 \
    cccc = (UChar)(s->tPos & 0xff);           \
    s->tPos >>= 8;

#define BZ_GET_FAST_C(cccc)                   \
    /* c_tPos is unsigned, hence test < 0 is pointless. */ \
    if (c_tPos >= (UInt32)100000 * (UInt32)ro_blockSize100k) return True; \
    c_tPos = c_tt[c_tPos];                    \
    cccc = (UChar)(c_tPos & 0xff);            \
    c_tPos >>= 8;

#define SET_LL4(i,n)                                          \
   { if (((i) & 0x1) == 0)                                    \
        s->ll4[(i) >> 1] = (s->ll4[(i) >> 1] & 0xf0) | (n); else    \
        s->ll4[(i) >> 1] = (s->ll4[(i) >> 1] & 0x0f) | ((n) << 4);  \
   }

#define GET_LL4(i)                             \
   ((((UInt32)(s->ll4[(i) >> 1])) >> (((i) << 2) & 0x4)) & 0xF)

#define SET_LL(i,n)                          \
   { s->ll16[i] = (UInt16)(n & 0x0000ffff);  \
     SET_LL4(i, n >> 16);                    \
   }

#define GET_LL(i) \
   (((UInt32)s->ll16[i]) | (GET_LL4(i) << 16))

#define BZ_GET_SMALL(cccc)                            \
    /* c_tPos is unsigned, hence test < 0 is pointless. */ \
    if (s->tPos >= (UInt32)100000 * (UInt32)s->blockSize100k) return True; \
    cccc = BZ2_indexIntoF ( s->tPos, s->cftab );    \
    s->tPos = GET_LL(s->tPos);


/*-- externs for decompression. --*/

extern Int32 BZ2_indexIntoF ( Int32, Int32* );

extern Int32 BZ2_decompress ( DState* );

extern void BZ2_hbCreateDecodeTables ( Int32*, Int32*, Int32*, UChar*, Int32,  Int32, Int32 );


#endif


/*-------------------------------------------------------------*/
/*--- end                                   bzlib_private.h ---*/
/*-------------------------------------------------------------*/
