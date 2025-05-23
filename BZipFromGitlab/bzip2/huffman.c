
/*-------------------------------------------------------------*/
/*--- Huffman coding low-level stuff                        ---*/
/*---                                             huffman.c ---*/
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


#include "bzlib_private.h"

/*---------------------------------------------------*/
void BZ2_hbMakeCodeLengths ( UChar *len, Int32 *freq, Int32 alphaSize, Int32 maxLen ) {
  /*--
   Nodes and heap entries run from 1.  Entry 0
   for both the heap and nodes is a sentinel.
   --*/
  Int32 nNodes, nHeap, n1, n2, i, j, k;
  Bool  tooLong;
  
  Int32 heap   [ BZ_MAX_ALPHA_SIZE + 2 ];
  Int32 weight [ BZ_MAX_ALPHA_SIZE * 2 ];
  Int32 parent [ BZ_MAX_ALPHA_SIZE * 2 ];
  
  for (i = 0; i < alphaSize; i++) {
    weight[i+1] = (freq[i] == 0 ? 1 : freq[i]) << 8;
  }
  
  while (True) {
    
    nNodes = alphaSize;
    nHeap = 0;
    
    heap[0] = 0;
    weight[0] = 0;
    parent[0] = -2;
    
    for (i = 1; i <= alphaSize; i++) {
      parent[i] = -1;
      nHeap += 1;
      heap[nHeap] = i;
      {
        Int32 zz = nHeap;
        Int32 tmp = heap[zz];
        while (weight[tmp] < weight[heap[zz >> 1]]) {
          heap[zz] = heap[zz >> 1];
          zz >>= 1;
        }
        heap[zz] = tmp;
      }
    }
        
    while (nHeap > 1) {
      n1 = heap[1]; heap[1] = heap[nHeap]; nHeap -= 1;
      {
        Int32 zz, yy, tmp;
        zz = 1; tmp = heap[zz];
        while (True) {
          yy = zz << 1;
          if (yy > nHeap) break;
          if (yy < nHeap &&
              weight[heap[yy+1]] < weight[heap[yy]])
            yy += 1;
          if (weight[tmp] < weight[heap[yy]]) break;
          heap[zz] = heap[yy];
          zz = yy;
        }
        heap[zz] = tmp;
      }
      
      n2 = heap[1]; heap[1] = heap[nHeap]; nHeap -= 1; {
        Int32 zz, yy, tmp;
        zz = 1; tmp = heap[zz];
        while (True) {
          yy = zz << 1;
          if (yy > nHeap) break;
          if (yy < nHeap &&
              weight[heap[yy+1]] < weight[heap[yy]])
            yy += 1;
          if (weight[tmp] < weight[heap[yy]]) break;
          heap[zz] = heap[yy];
          zz = yy;
        }
        heap[zz] = tmp;
      }
      
      nNodes += 1;
      parent[n1] = parent[n2] = nNodes;
      weight[nNodes] = (((weight[n1]) & 0xffffff00)+((weight[n2])) & 0xffffff00) | (1 + ((((weight[n1]) & 0x000000ff)) > (((weight[n2]) & 0x000000ff)) ? (((weight[n1]) & 0x000000ff)) : (((weight[n2]) & 0x000000ff))) );
      
      parent[nNodes] = -1;
      nHeap += 1;
      heap[nHeap] = nNodes;
      {
        Int32 zz, tmp;
        zz = nHeap; tmp = heap[zz];
        while (weight[tmp] < weight[heap[zz >> 1]]) {
          heap[zz] = heap[zz >> 1];
          zz >>= 1;
        }
        heap[zz] = tmp;
      }
      
    }
    
    tooLong = False;
    for (i = 1; i <= alphaSize; i++) {
      j = 0;
      k = i;
      while (parent[k] >= 0) {
        k = parent[k];
        j += 1;
      }
      len[i-1] = j;
      if (j > maxLen) tooLong = True;
    }
    
    if (! tooLong) {break;}
    
    /* 17 Oct 04: keep-going condition for the following loop used
     to be 'i < alphaSize', which missed the last element,
     theoretically leading to the possibility of the compressor
     looping.  However, this count-scaling step is only needed if
     one of the generated Huffman code words is longer than
     maxLen, which up to and including version 1.0.2 was 20 bits,
     which is extremely unlikely.  In version 1.0.3 maxLen was
     changed to 17 bits, which has minimal effect on compression
     ratio, but does mean this scaling step is used from time to
     time, enough to verify that it works.
     
     This means that bzip2-1.0.3 and later will only produce
     Huffman codes with a maximum length of 17 bits.  However, in
     order to preserve backwards compatibility with bitstreams
     produced by versions pre-1.0.3, the decompressor must still
     handle lengths of up to 20. */
    
    for (i = 1; i <= alphaSize; i++) {
      j = weight[i] >> 8;
      j = 1 + (j / 2);
      weight[i] = j << 8;
    }
  }
}


/** (KI-generated)
 *
 * @brief Weist jedem Symbol in einem Alphabet Huffman-Codes basierend auf seinen Codelängen zu.
 *
 * Diese Funktion iteriert durch die gegebenen Codelängen (`length`) von der minimalen
 * Länge (`minLen`) bis zur maximalen Länge (`maxLen`). Für jede Länge werden alle
 * Symbole im Alphabet (`alphaSize`) überprüft. Wenn die Codelänge eines Symbols
 * der aktuellen Länge entspricht, wird ihm der aktuelle Code (`vec`) zugewiesen.
 * Der Code wird dann für das nächste Symbol der gleichen Länge inkrementiert.
 * Nachdem alle Symbole einer bestimmten Länge verarbeitet wurden, wird der Code
 * um ein Bit nach links verschoben (`vec <<= 1`), um die Präfixeigenschaft von
 * Huffman-Codes sicherzustellen.
 *
 * @param code      Ein Array von Integern, in dem die zugewiesenen Huffman-Codes gespeichert werden.
 * Es muss mindestens `alphaSize` Elemente groß sein.
 * @param length    Ein Array von Unsigned Chars, das die Codelänge für jedes Symbol im
 * Alphabet enthält. Es muss mindestens `alphaSize` Elemente groß sein.
 * @param minLen    Die minimale Codelänge im Alphabet.
 * @param maxLen    Die maximale Codelänge im Alphabet.
 * @param alphaSize Die Größe des Alphabets (die Anzahl der Symbole).
 *
 * @note Die Funktion geht davon aus, dass die Eingabe `length` gültige Codelängen
 * für ein Huffman-Code-System repräsentiert.
 */
void BZ2_hbAssignCodes ( Int32 *code, UChar *length, Int32 minLen, Int32 maxLen, Int32 alphaSize ) {
  Int32 n, vec, i;
  
  vec = 0;
  for (n = minLen; n <= maxLen; n++) {
    for (i = 0; i < alphaSize; i++) {
      if (length[i] == n) {
        code[i] = vec;
        vec += 1;
      }
    }
    vec <<= 1;
  }
}


/*---------------------------------------------------*/
void BZ2_hbCreateDecodeTables ( Int32 *limit, Int32 *base, Int32 *perm, UChar *length, Int32 minLen, Int32 maxLen, Int32 alphaSize ) {
  Int32 pp;
  Int32 i;
  Int32 j;
  Int32 vec;
  
  pp = 0;
  for (i = minLen; i <= maxLen; i++) {
    for (j = 0; j < alphaSize; j++) {
      if (length[j] == i) {
        perm[pp] = j;
        pp += 1;
      }
    }
  }
  
  for (i = 0; i < BZ_MAX_CODE_LEN; i++) {
    base[i] = 0;
  }
  for (i = 0; i < alphaSize; i++) {
    base[length[i]+1] += 1;
  }
  
  for (i = 1; i < BZ_MAX_CODE_LEN; i++) {
    base[i] += base[i-1];
  }
  
  for (i = 0; i < BZ_MAX_CODE_LEN; i++) {
    limit[i] = 0;
  }
  vec = 0;
  
  for (i = minLen; i <= maxLen; i++) {
    vec += (base[i+1] - base[i]);
    limit[i] = vec-1;
    vec <<= 1;
  }
  for (i = minLen + 1; i <= maxLen; i++){
    base[i] = ((limit[i-1] + 1) << 1) - base[i];
  }
}


/*-------------------------------------------------------------*/
/*--- end                                         huffman.c ---*/
/*-------------------------------------------------------------*/
