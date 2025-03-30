
/*-------------------------------------------------------------*/
/*--- Compression machinery (not incl block sorting)        ---*/
/*---                                            compress.c ---*/
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


/* CHANGES
    0.9.0    -- original version.
    0.9.0a/b -- no changes in this file.
    0.9.0c   -- changed setting of nGroups in sendMTFValues()
                so as to do a bit better on small files
*/

#include "bzlib_private.h"


/*---------------------------------------------------*/
/*--- Bit stream I/O                              ---*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
void BZ2_bsInitWrite ( EState* s ) {
  s->bsLive = 0;
  s->bsBuff = 0;
}


/*---------------------------------------------------*/
static void bsFinishWrite ( EState* s ) {
  while (s->bsLive > 0) {
    s->zbits[s->numZ] = (UChar)(s->bsBuff >> 24);
    s->numZ += 1;
    s->bsBuff <<= 8;
    s->bsLive -= 8;
  }
}


/*---------------------------------------------------*/
static inline void bsW ( EState* s, Int32 n, UInt32 v ) {
  while (s->bsLive >= 8) {
    s->zbits[s->numZ]
    = (UChar)(s->bsBuff >> 24);
    s->numZ += 1;
    s->bsBuff <<= 8;
    s->bsLive -= 8;
  }
  s->bsBuff |= (v << (32 - s->bsLive - n));
  s->bsLive += n;
}


/*---------------------------------------------------*/
static void bsPutUInt32 ( EState* s, UInt32 u ) {
   bsW ( s, 8, (u >> 24) & 0xffL );
   bsW ( s, 8, (u >> 16) & 0xffL );
   bsW ( s, 8, (u >>  8) & 0xffL );
   bsW ( s, 8,  u        & 0xffL );
}


/*---------------------------------------------------*/
static void bsPutUChar ( EState* s, UChar c ) {
   bsW( s, 8, (UInt32)c );
}


/*---------------------------------------------------*/
/*--- The back end proper                         ---*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
static void makeMaps_e ( EState* s ) {
  s->nInUse = 0;
  for (Int32 i = 0; i < 256; i++) {
    if (s->inUse[i]) {
      s->unseqToSeq[i] = s->nInUse;
      s->nInUse += 1;
    }
  }
}


/*---------------------------------------------------*/
static void generateMoveToFrontValues ( EState* s ) {
  UChar   yy[256];
  Int32   zPend = 0;
  Int32   wr = 0;
  Int32   EOB;
  
  /*
   After sorting (eg, here),
   s->arr1 [ 0 .. s->nblock-1 ] holds sorted order,
   and
   ((UChar*)s->arr2) [ 0 .. s->nblock-1 ]
   holds the original block data.
   
   The first thing to do is generate the MTF values,
   and put them in
   ((UInt16*)s->arr1) [ 0 .. s->nblock-1 ].
   Because there are strictly fewer or equal MTF values
   than block values, ptr values in this area are overwritten
   with MTF values only when they are no longer needed.
   
   The final compressed bitstream is generated into the
   area starting at
   (UChar*) (&((UChar*)s->arr2)[s->nblock])
   
   These storage aliases are set up in bzCompressInit(),
   except for the last one, which is arranged in
   compressBlock().
   */
  UInt32* ptr   = s->ptr;
  UChar* block  = s->block;
  UInt16* mtfv  = s->mtfv;
  
  makeMaps_e ( s );
  EOB = s->nInUse+1;
  
  for (Int32 i = 0; i <= EOB; i++) {
    s->moveToFrontFreq[i] = 0;
  }
  
  for (Int32 i = 0; i < s->nInUse; i++) {
    yy[i] = (UChar) i;
  }
  
  for (Int32 i = 0; i < s->nblock; i++) {
    Int32 precedingIndexOfBurrowWheelerTransformation = ptr[i]-1;
    if (precedingIndexOfBurrowWheelerTransformation < 0) {
      precedingIndexOfBurrowWheelerTransformation += s->nblock;
    }
    UChar ll_i = s->unseqToSeq[block[precedingIndexOfBurrowWheelerTransformation]];
    
    if (yy[0] == ll_i) {
      zPend += 1;
    } else {
      
      if (zPend > 0) {
        zPend -= 1;
        while (True) {
          if (zPend & 1) {
            mtfv[wr] = BZ_RUNB;
            wr += 1;
            s->moveToFrontFreq[BZ_RUNB] += 1;
          } else {
            mtfv[wr] = BZ_RUNA;
            wr += 1;
            s->moveToFrontFreq[BZ_RUNA] += 1;
          }
          if (zPend < 2) {
            break;
          }
          zPend = (zPend - 2) / 2;
        }
        zPend = 0;
      }
      {
        UChar  rtmp;
        UChar* ryy_j;
        UChar  rll_i;
        rtmp  = yy[1];
        yy[1] = yy[0];
        ryy_j = &(yy[1]);
        rll_i = ll_i;
        while ( rll_i != rtmp ) {
          UChar rtmp2;
          ryy_j += 1;
          rtmp2  = rtmp;
          rtmp   = *ryy_j;
          *ryy_j = rtmp2;
        };
        yy[0] = rtmp;
        precedingIndexOfBurrowWheelerTransformation = (int) (ryy_j - &(yy[0]));
        mtfv[wr] = precedingIndexOfBurrowWheelerTransformation+1;
        wr += 1;
        s->moveToFrontFreq[precedingIndexOfBurrowWheelerTransformation+1] += 1;
      }
      
    }
  }
  
  if (zPend > 0) {
    zPend -= 1;
    while (True) {
      if (zPend & 1) {
        mtfv[wr] = BZ_RUNB;
        wr += 1;
        s->moveToFrontFreq[BZ_RUNB] += 1;
      }
      else {
        mtfv[wr] = BZ_RUNA;
        wr += 1;
        s->moveToFrontFreq[BZ_RUNA] += 1;
      }
      if (zPend < 2) {
        break;
      }
      zPend = (zPend - 2) / 2;
    }
    zPend = 0;
  }
  
  mtfv[wr] = EOB;
  wr += 1;
  s->moveToFrontFreq[EOB] += 1;
  
  s->nMoveToFront = wr;
}


/*---------------------------------------------------*/
static const int BZ_LESSER_ICOST  = 0;
static const int BZ_GREATER_ICOST = 15;

static void sendMoveToFrontValues ( EState* s ) {
  Int32 j;
  Int32 gs;
  Int32 ge;
  Int32 totc;
  Int32 bt;
  Int32 bc;
  Int32 nSelectors = 0;
  Int32 alphaSize;
  Int32 minLen;
  Int32 maxLen;
  Int32 selCtr;
  Int32 nGroups;
  Int32 nBytes;
  
  /*--
   UChar  len [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
   is a global since the decoder also needs it.
   
   Int32  code[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
   Int32  rfreq[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
   are also globals only used in this proc.
   Made global to keep stack frame size small.
   --*/
  
  
  UInt16 cost[BZ_N_GROUPS];
  Int32  fave[BZ_N_GROUPS];
  
  UInt16* mtfv = s->mtfv;
  
  alphaSize = s->nInUse+2;
  for (Int32 t = 0; t < BZ_N_GROUPS; t++) {
    for (Int32 v = 0; v < alphaSize; v++) {
      s->len[t][v] = BZ_GREATER_ICOST;
    }
  }
  
  /*--- Decide how many coding tables to use ---*/
  if (s->nMoveToFront < 200)  {
    nGroups = 2;
  }
  else {
    if (s->nMoveToFront < 600)  {
      nGroups = 3;
    }
    else {
      if (s->nMoveToFront < 1200) {
        nGroups = 4;
      }
      else {
        if (s->nMoveToFront < 2400) {
          nGroups = 5;
        }
        else {
          nGroups = 6;
        }
      }
    }
  }

  /*--- Generate an initial set of coding tables ---*/
  {
    Int32 nPart;
    Int32 remF;
    Int32 tFreq;
    Int32 aFreq;
    
    nPart = nGroups;
    remF  = s->nMoveToFront;
    gs = 0;
    while (nPart > 0) {
      tFreq = remF / nPart;
      ge = gs-1;
      aFreq = 0;
      while (aFreq < tFreq && ge < alphaSize-1) {
        ge += 1;
        aFreq += s->moveToFrontFreq[ge];
      }
      
      if (ge > gs
          && nPart != nGroups && nPart != 1
          && ((nGroups-nPart) % 2 == 1)) {
        aFreq -= s->moveToFrontFreq[ge];
        ge -= 1;
      }
      
      for (Int32 v = 0; v < alphaSize; v++) {
        if (v >= gs && v <= ge) {
          s->len[nPart-1][v] = BZ_LESSER_ICOST;
        }
        else {
          s->len[nPart-1][v] = BZ_GREATER_ICOST;
        }
      }
      
      nPart -= 1;
      gs = ge+1;
      remF -= aFreq;
    }
  }
  
  /*---
   Iterate up to BZ_N_ITERS times to improve the tables.
   ---*/
  for (Int32 iter = 0; iter < BZ_N_ITERS; iter++) {
    
    for (Int32 t = 0; t < nGroups; t++) {
      fave[t] = 0;
    }
    
    for (Int32 t = 0; t < nGroups; t++) {
      for (Int32 v = 0; v < alphaSize; v++) {
        s->rfreq[t][v] = 0;
      }
    }
    
    /*---
     Set up an auxiliary length table which is used to fast-track
     the common case (nGroups == 6).
     ---*/
    if (nGroups == 6) {
      for (Int32 v = 0; v < alphaSize; v++) {
        s->len_pack[v][0] = (s->len[1][v] << 16) | s->len[0][v];
        s->len_pack[v][1] = (s->len[3][v] << 16) | s->len[2][v];
        s->len_pack[v][2] = (s->len[5][v] << 16) | s->len[4][v];
      }
    }
    
    nSelectors = 0;
    totc = 0;
    gs = 0;
    while (True) {
      
      /*--- Set group start & end marks. --*/
      if (gs >= s->nMoveToFront) {
        break;
      }
      ge = gs + BZ_G_SIZE - 1;
      if (ge >= s->nMoveToFront) {
        ge = s->nMoveToFront-1;
      }
      
      /*--
       Calculate the cost of this group as coded
       by each of the coding tables.
       --*/
      for (Int32 t = 0; t < nGroups; t++) {
        cost[t] = 0;
      }

      for (Int32 i = gs; i <= ge; i++) {
        UInt16 icv = mtfv[i];
        for (Int32 t = 0; t < nGroups; t++) {
          cost[t] += s->len[t][icv];
        }
      }
      
      /*--
       Find the coding table which is best for this group,
       and record its identity in the selector table.
       --*/
      bc = 999999999; bt = -1;
      for (Int32 t = 0; t < nGroups; t++) {
        if (cost[t] < bc) {
          bc = cost[t];
          bt = t;
        };
      }
      totc += bc;
      fave[bt] += 1;
      s->selector[nSelectors] = bt;
      nSelectors += 1;
      
      for (Int32 i = gs; i <= ge; i++) {
        s->rfreq[bt][ mtfv[i] ] += 1;
      }
      
      gs = ge+1;
    }
    
    /*--
     Recompute the tables based on the accumulated frequencies.
     --*/
    /* maxLen was changed from 20 to 17 in bzip2-1.0.3.  See
     comment in huffman.c for details. */
    for (Int32 t = 0; t < nGroups; t++) {
      BZ2_hbMakeCodeLengths ( &(s->len[t][0]), &(s->rfreq[t][0]), alphaSize, 17 /*20*/ );
    }
  }
  
    
  /*--- Compute MTF values for the selectors. ---*/
  {
    UChar pos[BZ_N_GROUPS]; UChar ll_i; UChar tmp2; UChar tmp;
    for (Int32 i = 0; i < nGroups; i++) {
      pos[i] = i;
    }
    for (Int32 i = 0; i < nSelectors; i++) {
      ll_i = s->selector[i];
      j = 0;
      tmp = pos[j];
      while ( ll_i != tmp ) {
        j += 1;
        tmp2 = tmp;
        tmp = pos[j];
        pos[j] = tmp2;
      }
      pos[0] = tmp;
      s->selectorMoveToFront[i] = j;
    }
  }
  
  /*--- Assign actual codes for the tables. --*/
  for (Int32 t = 0; t < nGroups; t++) {
    minLen = 32;
    maxLen = 0;
    for (Int32 i = 0; i < alphaSize; i++) {
      if (s->len[t][i] > maxLen) {
        maxLen = s->len[t][i];
      }
      if (s->len[t][i] < minLen) {
        minLen = s->len[t][i];
      }
    }
    BZ2_hbAssignCodes ( &(s->code[t][0]), &(s->len[t][0]), minLen, maxLen, alphaSize );
  }
  
  /*--- Transmit the mapping table. ---*/
  {
    Bool inUse16[16];
    for (Int32 i = 0; i < 16; i++) {
      inUse16[i] = False;
      for (j = 0; j < 16; j++) {
        if (s->inUse[i * 16 + j]) {
          inUse16[i] = True;
        }
      }
    }
    
    nBytes = s->numZ;
    for (Int32 i = 0; i < 16; i++) {
      if (inUse16[i]) {
        bsW(s,1,1);
      }
      else {
        bsW(s,1,0);
      }
    }
    
    for (Int32 i = 0; i < 16; i++) {
      if (inUse16[i]) {
        for (j = 0; j < 16; j++) {
          if (s->inUse[i * 16 + j]) {
            bsW(s,1,1);
          }
          else {
            bsW(s,1,0);
          }
        }
      }
    }
  }
  
  /*--- Now the selectors. ---*/
  nBytes = s->numZ;
  bsW ( s, 3, nGroups );
  bsW ( s, 15, nSelectors );
  for (Int32 i = 0; i < nSelectors; i++) {
    for (j = 0; j < s->selectorMoveToFront[i]; j++) {
      bsW(s,1,1);
    }
    bsW(s,1,0);
  }
  
  /*--- Now the coding tables. ---*/
  nBytes = s->numZ;
  
  for (Int32 t = 0; t < nGroups; t++) {
    Int32 curr = s->len[t][0];
    bsW ( s, 5, curr );
    for (Int32 i = 0; i < alphaSize; i++) {
      while (curr < s->len[t][i]) {
        bsW(s,2,2);
        curr += 1; /* 10 */
      };
      while (curr > s->len[t][i]) {
        bsW(s,2,3);
        curr -= 1; /* 11 */
      };
      bsW ( s, 1, 0 );
    }
  }
  
  /*--- And finally, the block data proper ---*/
  nBytes = s->numZ;
  selCtr = 0;
  gs = 0;
  while (True) {
    if (gs >= s->nMoveToFront) break;
    ge = gs + BZ_G_SIZE - 1;
    if (ge >= s->nMoveToFront) {
      ge = s->nMoveToFront-1;
    }
    
    for (Int32 i = gs; i <= ge; i++) {
      bsW ( s, s->len  [s->selector[selCtr]] [mtfv[i]], s->code [s->selector[selCtr]] [mtfv[i]] );
    }
    
    gs = ge+1;
    selCtr += 1;
  }
}


/*---------------------------------------------------*/
void BZ2_compressBlock ( EState* status, Bool is_last_block ) {
  
  if (status->nblock > 0) {
    BZ_FINALISE_CRC ( &status->blockCRC );
    status->combinedCRC = (status->combinedCRC << 1) | (status->combinedCRC >> 31);
    status->combinedCRC ^= status->blockCRC;
    if (status->blockNo > 1) {
      status->numZ = 0;
    }
    
    BZ2_blockSort ( status );
  }
  
  status->zbits = (UChar*) (&((UChar*)status->arr2)[status->nblock]);
  
  /*-- If this is the first block, create the stream header. --*/
  if (status->blockNo == 1) {
    BZ2_bsInitWrite ( status );
    bsPutUChar ( status, BZ_HDR_B );
    bsPutUChar ( status, BZ_HDR_Z );
    bsPutUChar ( status, BZ_HDR_h );
    bsPutUChar ( status, (UChar)(BZ_HDR_0 + status->blockSize100k) );
  }
  
  if (status->nblock > 0) {
    bsPutUChar ( status, 0x31 );
    bsPutUChar ( status, 0x41 );
    bsPutUChar ( status, 0x59 );
    bsPutUChar ( status, 0x26 );
    bsPutUChar ( status, 0x53 );
    bsPutUChar ( status, 0x59 );
    
    /*-- Now the block's CRC, so it is in a known place. --*/
    bsPutUInt32 ( status, status->blockCRC );
    
    /*--
     Now a single bit indicating (non-)randomisation.
     As of version 0.9.5, we use a better sorting algorithm
     which makes randomisation unnecessary.  So always set
     the randomised bit to 'no'.  Of course, the decoder
     still needs to be able to handle randomised blocks
     so as to maintain backwards compatibility with
     older versions of bzip2.
     --*/
    bsW (status, 1, 0);
    
    bsW ( status, 24, status->origPtr );
    generateMoveToFrontValues ( status );
    sendMoveToFrontValues ( status );
  }
  
  /*-- If this is the last block, add the stream trailer. --*/
  if (is_last_block) {
    bsPutUChar ( status, 0x17 );
    bsPutUChar ( status, 0x72 );
    bsPutUChar ( status, 0x45 );
    bsPutUChar ( status, 0x38 );
    bsPutUChar ( status, 0x50 );
    bsPutUChar ( status, 0x90 );
    bsPutUInt32 ( status, status->combinedCRC );
    bsFinishWrite ( status );
  }
}

/*-------------------------------------------------------------*/
/*--- end                                        compress.c ---*/
/*-------------------------------------------------------------*/
