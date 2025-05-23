
/*-------------------------------------------------------------*/
/*--- Library top-level functions.                          ---*/
/*---                                               bzlib.c ---*/
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
   0.9.0c   -- made zero-length BZ_FLUSH work correctly in bzCompress().
     fixed bzWrite/bzRead to ignore zero-length requests.
     fixed bzread to correctly handle read requests after EOF.
     wrong parameter order in call to bzDecompressInit in
     bzBuffToBuffDecompress.  Fixed.
*/

#include "bzlib_private.h"


/*---------------------------------------------------*/
/*--- Compression stuff                           ---*/
/*---------------------------------------------------*/


/*---------------------------------------------------*/
static int bz_config_ok ( void ) {
  if (sizeof(int)   != 4) {
    return 0;
  }
  if (sizeof(short) != 2) {
    return 0;
  }
  if (sizeof(char)  != 1) {
    return 0;
  }
  return 1;
}


/*---------------------------------------------------*/
static void* default_bzalloc ( void* opaque, Int32 items, Int32 size ) {
   void* v = malloc ( items * size );
   return v;
}

static void default_bzfree ( void* opaque, void* addr ) {
  if (addr != NULL) {
    free ( addr );
  }
}


/** (KI generiert)
 * @brief Bereitet die Kompressionsstruktur für die Verarbeitung eines neuen Datenblocks vor.
 *
 * Diese Funktion initialisiert verschiedene Felder der `EState`-Struktur, um den Zustand
 * für die Komprimierung eines neuen Eingabeblocks vorzubereiten. Dazu gehören das
 * Zurücksetzen der Blockgröße, der Anzahl der komprimierten Bytes, der Ausgabeposition
 * und der Block-CRC. Außerdem wird das `inUse`-Array zurückgesetzt, um die Information
 * über die im vorherigen Block verwendeten Bytes zu löschen, und die Blocknummer wird
 * inkrementiert.
 *
 * @param s Ein Zeiger auf die `EState`-Struktur, die für die Kompression verwendet wird.
 * Diese Struktur enthält alle relevanten Zustandsinformationen für den
 * bzip2-Kompressionsprozess.
 *
 * @details
 * Die Funktion führt folgende Schritte aus:
 *
 * - Setzt `s->nblock` (Größe des aktuellen Eingabeblocks) auf 0.
 *
 * - Setzt `s->numZ` (Anzahl der komprimierten Bytes für den aktuellen Block) auf 0.
 *
 * - Setzt `s->state_out_pos` (aktuelle Position im Ausgabepuffer des Blocks) auf 0.
 *
 * - Initialisiert `s->blockCRC` (CRC-Prüfsumme des aktuellen Blocks) mit `BZ_INITIALISE_CRC`.
 *
 * - Setzt alle Elemente des `s->inUse`-Arrays (Markierung verwendeter Bytes) auf `False`.
 *
 * - Inkrementiert `s->blockNo` (fortlaufende Blocknummer).
 *
 * Diese Initialisierung ist entscheidend, um sicherzustellen, dass die Komprimierung jedes
 * neuen Datenblocks mit einem sauberen Zustand beginnt und keine Restinformationen
 * aus vorherigen Blöcken die aktuelle Verarbeitung beeinflussen.
 *
 * @note Diese Funktion sollte aufgerufen werden, bevor mit dem Füllen der `block`-
 * oder anderer relevanter Felder der `EState`-Struktur für einen neuen
 * Kompressionsblock begonnen wird.
 */
static void prepare_new_block ( EState* s ) {
  Int32 i;
  s->nblock = 0;
  s->numZ = 0;
  s->state_out_pos = 0;
  s->blockCRC = BZ_INITIALISE_CRC;
  for (i = 0; i < 256; i++) {
    s->inUse[i] = False;
  }
  s->blockNo += 1;
}


/*---------------------------------------------------*/
static void init_RL ( EState* s ) {
   s->state_in_ch  = 256;
   s->state_in_len = 0;
}

static Bool isempty_RL ( EState* s ) {
  if (s->state_in_ch < 256 && s->state_in_len > 0) {
    return False;
  }
  else {
    return True;
  }
}

/*---------------------------------------------------*/
int BZ2_bzCompressInit ( bz_stream* strm, int blockSize100k, int workFactor ) {
  Int32   n;
  EState* s;
  
  if (!bz_config_ok()) {
    return BZ_CONFIG_ERROR;
  }
  
  if (strm == NULL || blockSize100k < 1 || blockSize100k > 9 || workFactor < 0 || workFactor > 250)
    return BZ_PARAM_ERROR;
  
  if (workFactor == 0) {
    workFactor = 30;
  }
  if (strm->bzalloc == NULL) {
    strm->bzalloc = default_bzalloc;
  }
  if (strm->bzfree == NULL) {
    strm->bzfree = default_bzfree;
  }
  
  s = BZALLOC( sizeof(EState) );
  if (s == NULL) {
    return BZ_MEM_ERROR;
  }
  s->strm = strm;
  
  s->arr1 = NULL;
  s->arr2 = NULL;
  s->ftab = NULL;
  
  n       = 100000 * blockSize100k;
  s->arr1 = BZALLOC( n                  * sizeof(UInt32) );
  s->arr2 = BZALLOC( (n+BZ_N_OVERSHOOT) * sizeof(UInt32) );
  s->ftab = BZALLOC( 65537              * sizeof(UInt32) );
  
  if (s->arr1 == NULL || s->arr2 == NULL || s->ftab == NULL) {
    if (s->arr1 != NULL) {
      BZFREE(s->arr1);
    }
    if (s->arr2 != NULL) {
      BZFREE(s->arr2);
    }
    if (s->ftab != NULL) {
      BZFREE(s->ftab);
    }
    if (s       != NULL) {
      BZFREE(s);
    }
    return BZ_MEM_ERROR;
  }
  
  s->blockNo           = 0;
  s->statusInputEqualsTrueVsOutputEqualsFalse             = True;
  s->modus              = BZ_MODUS_RUNNING;
  s->combinedCRC       = 0;
  s->blockSize100k     = blockSize100k;
  s->nblockMAX         = 100000 * blockSize100k - 19;
  s->workFactor        = workFactor;
  
  s->block             = (UChar*)s->arr2;
  s->mtfv              = (UInt16*)s->arr1;
  s->zbits             = NULL;
  s->ptr               = (UInt32*)s->arr1;
  
  strm->state          = s;
  strm->total_in_lo32  = 0;
  strm->total_in_hi32  = 0;
  strm->total_out_lo32 = 0;
  strm->total_out_hi32 = 0;
  init_RL ( s );
  prepare_new_block ( s );
  return BZ_OK;
}

/*---------------------------------------------------*/
static void add_pair_to_block ( EState* s ) {
  Int32 i;
  UChar ch = (UChar)(s->state_in_ch);
  for (i = 0; i < s->state_in_len; i++) {
    BZ_UPDATE_CRC(&s->blockCRC, ch); // Funktionsaufruf (Pointer)
  }
  s->inUse[s->state_in_ch] = True;
  switch (s->state_in_len) {
    case 1:
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      break;
    case 2:
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      break;
    case 3:
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      break;
    default:
      s->inUse[s->state_in_len-4] = True;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = (UChar)ch;
      s->nblock += 1;
      s->block[s->nblock] = ((UChar)(s->state_in_len-4));
      s->nblock += 1;
      break;
  }
}

/*---------------------------------------------------*/
static void flush_RL ( EState* s ) {
  if (s->state_in_ch < 256) {
    add_pair_to_block ( s );
  }
  init_RL ( s );
}

/*---------------------------------------------------*/
#define ADD_CHAR_TO_BLOCK(zs,zchh0)               \
{                                                 \
   UInt32 zchh = (UInt32)(zchh0);                 \
   /*-- fast track the common case --*/           \
   if (zchh != zs->state_in_ch &&                 \
       zs->state_in_len == 1) {                   \
      UChar ch = (UChar)(zs->state_in_ch);        \
      BZ_UPDATE_CRC(&zs->blockCRC, ch);           \
      zs->inUse[zs->state_in_ch] = True;          \
      zs->block[zs->nblock] = (UChar)ch;          \
      zs->nblock += 1;                               \
      zs->state_in_ch = zchh;                     \
   }                                              \
   else                                           \
   /*-- general, uncommon cases --*/              \
   if (zchh != zs->state_in_ch ||                 \
      zs->state_in_len == 255) {                  \
      if (zs->state_in_ch < 256)                  \
         add_pair_to_block ( zs );                \
      zs->state_in_ch = zchh;                     \
      zs->state_in_len = 1;                       \
   } else {                                       \
      zs->state_in_len += 1;                         \
   }                                              \
}


/*---------------------------------------------------*/
static Bool copy_input_until_stop ( EState* s ) {
  Bool progress_in = False;
  
  if (s->modus == BZ_MODUS_RUNNING) {
    /*-- fast track the common case --*/
    while (True) {
      /*-- block full? --*/
      if (s->nblock >= s->nblockMAX) {
        break;
      }
      /*-- no input? --*/
      if (s->strm->avail_in == 0) {
        break;
      }
      progress_in = True;
      ADD_CHAR_TO_BLOCK ( s, (UInt32)(*((UChar*)(s->strm->next_in))) );
      s->strm->next_in += 1;
      s->strm->avail_in -= 1;
      s->strm->total_in_lo32 += 1;
      if (s->strm->total_in_lo32 == 0) {
        s->strm->total_in_hi32 += 1;
      }
    }
  }
  else {
    /*-- general, uncommon case --*/
    while (True) {
      /*-- block full? --*/
      if (s->nblock >= s->nblockMAX) {
        break;
      }
      /*-- no input? --*/
      if (s->strm->avail_in == 0) {
        break;
      }
      /*-- flush/finish end? --*/
      if (s->avail_in_expect == 0) {
        break;
      }
      progress_in = True;
      ADD_CHAR_TO_BLOCK ( s, (UInt32)(*((UChar*)(s->strm->next_in))) );
      s->strm->next_in += 1;
      s->strm->avail_in -= 1;
      s->strm->total_in_lo32 += 1;
      if (s->strm->total_in_lo32 == 0) {
        s->strm->total_in_hi32 += 1;
      }
      s->avail_in_expect -= 1;
    }
  }
  return progress_in;
}

/*---------------------------------------------------*/
static Bool copy_output_until_stop ( EState* s ) {
  Bool progress_out = False;
  
  while (True) {
    
    /*-- no output space? --*/
    if (s->strm->avail_out == 0) {
      break;
    }
    
    /*-- block done? --*/
    if (s->state_out_pos >= s->numZ) {
      break;
    }
    
    progress_out = True;
    *(s->strm->next_out) = s->zbits[s->state_out_pos];
    s->state_out_pos += 1;
    s->strm->avail_out -= 1;
    s->strm->next_out += 1;
    s->strm->total_out_lo32 += 1;
    if (s->strm->total_out_lo32 == 0) {
      s->strm->total_out_hi32 += 1;
    }
  }
  
  return progress_out;
}


/*---------------------------------------------------*/
// Diese Funktion wird nur von "int BZ2_bzCompress ( bz_stream *strm, int action )" aufgerufen
static inline Bool handle_compress ( bz_stream* strm ) {
  Bool progress_in  = False;
  Bool progress_out = False;
  EState* s = strm->state;
  
  while (True) {
    if (!s->statusInputEqualsTrueVsOutputEqualsFalse) {
      progress_out |= copy_output_until_stop (s);
      if (s->state_out_pos < s->numZ) {
        break;
      }
      if (s->modus == BZ_MODUS_FINISHING && s->avail_in_expect == 0 && isempty_RL(s)) {
        break;
      }
      prepare_new_block ( s );
      s->statusInputEqualsTrueVsOutputEqualsFalse = True;
      if (s->modus == BZ_MODUS_FLUSHING && s->avail_in_expect == 0 && isempty_RL(s)) {
        break;
      }
    }
    
    if (s->statusInputEqualsTrueVsOutputEqualsFalse) {
      progress_in |= copy_input_until_stop (s);
      if (s->modus != BZ_MODUS_RUNNING && s->avail_in_expect == 0) {
        flush_RL (s);
        BZ2_compressBlock (s, (Bool)(s->modus == BZ_MODUS_FINISHING));
        s->statusInputEqualsTrueVsOutputEqualsFalse = False;
      }
      else {
        if (s->nblock >= s->nblockMAX) {
          BZ2_compressBlock (s, False);
          s->statusInputEqualsTrueVsOutputEqualsFalse = False;
        }
        else {
          if (s->strm->avail_in == 0) {
            break;
          }
        }
      }
    }    
  }
  
  return progress_in || progress_out;
}


/*---------------------------------------------------*/
int BZ2_bzCompress ( bz_stream *strm, int action ) {
  Bool progress;
  
  // Deklariere einen Zeiger vom Typ EState für den Status des übergebenen strm
  EState* statusOfStrm;
  
  // Prüfung der Vorbedingungen zum Methodeneintritt
  // Wenn der Zeiger auf die Struktur NULL ist
  if (strm == NULL) {
    // gebe einen Fehler zurück
    return BZ_PARAM_ERROR;
  }
  // sichere die Status aus der Struktur
  statusOfStrm = strm->state;
  // Wenn der Status NULL ist
  if (statusOfStrm == NULL) {
    // gebe einen Fehler zurück
    return BZ_PARAM_ERROR;
  }
  // Wenn der Status, der auf die Struktur zeigt nicht identisch ist wie die Struktur die auf diesen Status zeigt
  if (statusOfStrm->strm != strm) {
    // gebe einen Fehler zurück
    return BZ_PARAM_ERROR;
  }
  
  do {
    switch (statusOfStrm->modus) {
        
      case BZ_MODUS_IDLE:
        return BZ_SEQUENCE_ERROR;
        
      case BZ_MODUS_RUNNING:
        switch (action) {
          case BZ_RUN :
            // komprimiere die Daten
            progress = handle_compress ( strm );
            // verlasse die Funktion und berichte, ob der die Kompression erfolgreich war
            return progress ? BZ_RUN_OK : BZ_PARAM_ERROR;
          case BZ_FLUSH :
            statusOfStrm->avail_in_expect = strm->avail_in;
            statusOfStrm->modus = BZ_MODUS_FLUSHING;
            break; // Zurück zum Anfang der do-while Schleife
          case BZ_FINISH :
            statusOfStrm->avail_in_expect = strm->avail_in;
            statusOfStrm->modus = BZ_MODUS_FINISHING;
            break; // Zurück zum Anfang der do-while Schleife
          default :
            // Verlasse die Funktion mit einem Fehler
            return BZ_PARAM_ERROR;
        }
        break; // Verlasse den inneren Switch
        
      case BZ_MODUS_FLUSHING:
        if (action != BZ_FLUSH) {
          return BZ_SEQUENCE_ERROR;
        }
        if (statusOfStrm->avail_in_expect != statusOfStrm->strm->avail_in) {
          return BZ_SEQUENCE_ERROR;
        }
        progress = handle_compress ( strm );
        if (statusOfStrm->avail_in_expect > 0 || !isempty_RL(statusOfStrm) ||
            statusOfStrm->state_out_pos < statusOfStrm->numZ) {
          return BZ_FLUSH_OK;
        }
        statusOfStrm->modus = BZ_MODUS_RUNNING;
        return BZ_RUN_OK;
        
      case BZ_MODUS_FINISHING:
        if (action != BZ_FINISH) {
          return BZ_SEQUENCE_ERROR;
        }
        if (statusOfStrm->avail_in_expect != statusOfStrm->strm->avail_in) {
          return BZ_SEQUENCE_ERROR;
        }
        progress = handle_compress ( strm );
        if (!progress) {
          return BZ_SEQUENCE_ERROR;
        }
        if (statusOfStrm->avail_in_expect > 0 || !isempty_RL(statusOfStrm) ||
            statusOfStrm->state_out_pos < statusOfStrm->numZ) {
          return BZ_FINISH_OK;
        }
        statusOfStrm->modus = BZ_MODUS_IDLE;
        return BZ_STREAM_END;
        
      default:
        // Fehlerfall bei einen ungültigen Modus
        return BZ_PARAM_ERROR;
    }
  } while (True);
  return BZ_OK; /* wird niemals erreicht*/
}


/*---------------------------------------------------*/
int BZ2_bzCompressEnd  ( bz_stream *strm ) {
  EState* s;
  // Führe Vorprüfungen aus:
  {
    // Wenn der übergebene Datenstrom NULL ist
    if (strm == NULL) {
      // gebe zurück, dass die Funktion mit einem falschen Parameter aufgerufen wurde
      return BZ_PARAM_ERROR;
    }
    // Hole einen Zeiger auf die Status in der Struktur für den Datenstrom
    s = strm->state;
    // Wenn der Status NULL ist und somit im Datenstrom nicht gesetzt ist
    if (s == NULL) {
      // gebe zurück, dass die Funktion mit einem falschen Parameter aufgerufen wurde
      return BZ_PARAM_ERROR;
    }
    // Wenn der Status auf einen anderen Eingabestrom verweist, denn der Eingabestrom auf den Status
    if (s->strm != strm) {
      // gebe zurück, dass die Funktion mit einem falschen Parameter aufgerufen wurde
      return BZ_PARAM_ERROR;
    }
  }
  
  // Wenn das Array Nr. 1 nicht NULL ist
  if (s->arr1 != NULL) {
    // Rufe die Funktion BZFree auf und übergebe das Array Nr. 1
    BZFREE(s->arr1);
  }
  // Wenn das Array Nr. 2 nicht NULL ist
  if (s->arr2 != NULL) {
    // Rufe die Funktion BZFree auf und übergebe das Array Nr. 2
    BZFREE(s->arr2);
  }
  // Wenn ftab nicht NULL ist
  if (s->ftab != NULL) {
    // Rufe die Funktion BZFree auf und übergebe ftab
    BZFREE(s->ftab);
  }
  // Rufe die Funktion BZFree auf und übergebe den Status des Datenstroms
  BZFREE(strm->state);
  // setze den Status des Datenstrom auf NULL
  strm->state = NULL;
  
  // Gebe zurück, dass die Funktion erfolgreich beendet wurde
  return BZ_OK;
}


/*---------------------------------------------------*/
/*--- Decompression stuff                         ---*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
int BZ2_bzDecompressInit ( bz_stream* strm, int small ) {
  DState* s;
  
  if (!bz_config_ok()) {
    return BZ_CONFIG_ERROR;
  }
  
  if (strm == NULL) {
    return BZ_PARAM_ERROR;
  }
  if (small != 0 && small != 1) {
    return BZ_PARAM_ERROR;
  }
  
  if (strm->bzalloc == NULL) {
    strm->bzalloc = default_bzalloc;
  }
  if (strm->bzfree == NULL) {
    strm->bzfree = default_bzfree;
  }
  
  s = BZALLOC( sizeof(DState) );
  if (s == NULL) {
    return BZ_MEM_ERROR;
  }
  s->strm                  = strm;
  strm->state              = s;
  s->state                 = BZ_X_MAGIC_1;
  s->bsLive                = 0;
  s->bsBuff                = 0;
  s->calculatedCombinedCRC = 0;
  strm->total_in_lo32      = 0;
  strm->total_in_hi32      = 0;
  strm->total_out_lo32     = 0;
  strm->total_out_hi32     = 0;
  s->smallDecompress       = (Bool)small;
  s->ll4                   = NULL;
  s->ll16                  = NULL;
  s->tt                    = NULL;
  s->currBlockNo           = 0;
  
  return BZ_OK;
}

/*---------------------------------------------------*/
/* Return  True if data corruption is discovered.
   Returns False if there is no problem.
*/
static Bool unRLE_obuf_to_output_FAST ( DState* s ) {
  UChar k1;
  
  if (s->blockRandomised) {
    
    while (True) {
      /* try to finish existing run */
      while (True) {
        if (s->strm->avail_out == 0) {
          return False;
        }
        if (s->state_out_len == 0) {
          break;
        }
        *( (UChar*)(s->strm->next_out) ) = s->state_out_ch;
        BZ_UPDATE_CRC(&s->calculatedBlockCRC, s->state_out_ch);
        s->state_out_len -= 1;
        s->strm->next_out += 1;
        s->strm->avail_out -= 1;
        s->strm->total_out_lo32 += 1;
        if (s->strm->total_out_lo32 == 0) {
          s->strm->total_out_hi32 += 1;
        }
      }
      
      /* can a new run be started? */
      if (s->nblock_used == s->save_nblock+1) {
        return False;
      }
      
      /* Only caused by corrupt data stream? */
      if (s->nblock_used > s->save_nblock+1) {
        return True;
      }
      
      s->state_out_len = 1;
      s->state_out_ch = s->k0;
      BZ_GET_FAST(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      s->state_out_len = 2;
      BZ_GET_FAST(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      s->state_out_len = 3;
      BZ_GET_FAST(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      BZ_GET_FAST(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      s->state_out_len = ((Int32)k1) + 4;
      BZ_GET_FAST(s->k0);
      BZ_RAND_UPD_MASK;
      s->k0 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
    }
    
  }
  else {
    
    /* restore */
    UInt32        c_calculatedBlockCRC = s->calculatedBlockCRC;
    UChar         c_state_out_ch       = s->state_out_ch;
    Int32         c_state_out_len      = s->state_out_len;
    Int32         c_nblock_used        = s->nblock_used;
    Int32         c_k0                 = s->k0;
    UInt32*       c_tt                 = s->tt;
    UInt32        c_tPos               = s->tPos;
    char*         cs_next_out          = s->strm->next_out;
    unsigned int  cs_avail_out         = s->strm->avail_out;
    Int32         ro_blockSize100k     = s->blockSize100k;
    /* end restore */
    
    UInt32       avail_out_INIT = cs_avail_out;
    Int32        s_save_nblockPP = s->save_nblock+1;
    unsigned int total_out_lo32_old;
    
    while (True) {
      
      /* try to finish existing run */
      if (c_state_out_len > 0) {
        while (True) {
          if (cs_avail_out == 0) {
            goto return_notr;
          }
          if (c_state_out_len == 1) {
            break;
          }
          *( (UChar*)(cs_next_out) ) = c_state_out_ch;
          BZ_UPDATE_CRC(&c_calculatedBlockCRC, c_state_out_ch); // Funktionsaufruf (Pointer)
          c_state_out_len -= 1;
          cs_next_out += 1;
          cs_avail_out -= 1;
        }
        s_state_out_len_eq_one: {
          if (cs_avail_out == 0) {
            c_state_out_len = 1;
            goto return_notr;
          }
          *( (UChar*)(cs_next_out) ) = c_state_out_ch;
          BZ_UPDATE_CRC(&c_calculatedBlockCRC, c_state_out_ch); // Funktionsaufruf (Pointer)
          cs_next_out += 1;
          cs_avail_out -= 1;
        }
      }
      /* Only caused by corrupt data stream? */
      if (c_nblock_used > s_save_nblockPP) {
        return True;
      }
      
      /* can a new run be started? */
      if (c_nblock_used == s_save_nblockPP) {
        c_state_out_len = 0;
        goto return_notr;
      }
      c_state_out_ch = c_k0;
      BZ_GET_FAST_C(k1);
      c_nblock_used += 1;
      if (k1 != c_k0) {
        c_k0 = k1;
        goto s_state_out_len_eq_one;
      }
      if (c_nblock_used == s_save_nblockPP)
        goto s_state_out_len_eq_one;
      
      c_state_out_len = 2;
      BZ_GET_FAST_C(k1);
      c_nblock_used += 1;
      if (c_nblock_used == s_save_nblockPP) {
        continue;
      }
      if (k1 != c_k0) {
        c_k0 = k1;
        continue;
      }
      
      c_state_out_len = 3;
      BZ_GET_FAST_C(k1);
      c_nblock_used += 1;
      if (c_nblock_used == s_save_nblockPP) {
        continue;
      }
      if (k1 != c_k0) {
        c_k0 = k1;
        continue;
      }
      
      BZ_GET_FAST_C(k1);
      c_nblock_used += 1;
      c_state_out_len = ((Int32)k1) + 4;
      BZ_GET_FAST_C(c_k0);
      c_nblock_used += 1;
    }
    
  return_notr:
    total_out_lo32_old = s->strm->total_out_lo32;
    s->strm->total_out_lo32 += (avail_out_INIT - cs_avail_out);
    if (s->strm->total_out_lo32 < total_out_lo32_old) {
      s->strm->total_out_hi32 += 1;
    }
    
    /* save */
    s->calculatedBlockCRC = c_calculatedBlockCRC;
    s->state_out_ch       = c_state_out_ch;
    s->state_out_len      = c_state_out_len;
    s->nblock_used        = c_nblock_used;
    s->k0                 = c_k0;
    s->tt                 = c_tt;
    s->tPos               = c_tPos;
    s->strm->next_out     = cs_next_out;
    s->strm->avail_out    = cs_avail_out;
    /* end save */
  }
  return False;
}

/*---------------------------------------------------*/
inline Int32 BZ2_indexIntoF ( Int32 indx, Int32 *cftab ) {
  Int32 nb, na, mid;
  nb = 0;
  na = 256;
  do {
    mid = (nb + na) >> 1;
    if (indx >= cftab[mid]) {
      nb = mid;
    }
    else {
      na = mid;
    }
  }
  while (na - nb != 1);
  return nb;
}


/*---------------------------------------------------*/
/* Return  True iff data corruption is discovered.
   Returns False if there is no problem.
*/
static Bool unRLE_obuf_to_output_SMALL ( DState* s ) {
  UChar k1;
  
  if (s->blockRandomised) {
    
    while (True) {
      /* try to finish existing run */
      while (True) {
        if (s->strm->avail_out == 0) {
          return False;
        }
        if (s->state_out_len == 0) {
          break;
        }
        *( (UChar*)(s->strm->next_out) ) = s->state_out_ch;
        BZ_UPDATE_CRC(&s->calculatedBlockCRC, s->state_out_ch); // Funktionsaufruf (Pointer)
        s->state_out_len -= 1;
        s->strm->next_out += 1;
        s->strm->avail_out -= 1;
        s->strm->total_out_lo32 += 1;
        if (s->strm->total_out_lo32 == 0) {
          s->strm->total_out_hi32 += 1;
        }
      }
      
      /* can a new run be started? */
      if (s->nblock_used == s->save_nblock+1) {
        return False;
      }
      
      /* Only caused by corrupt data stream? */
      if (s->nblock_used > s->save_nblock+1) {
        return True;
      }
      
      s->state_out_len = 1;
      s->state_out_ch = s->k0;
      BZ_GET_SMALL(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      s->state_out_len = 2;
      BZ_GET_SMALL(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      s->state_out_len = 3;
      BZ_GET_SMALL(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      BZ_GET_SMALL(k1);
      BZ_RAND_UPD_MASK;
      k1 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
      s->state_out_len = ((Int32)k1) + 4;
      BZ_GET_SMALL(s->k0);
      BZ_RAND_UPD_MASK;
      s->k0 ^= BZ_RAND_MASK;
      s->nblock_used += 1;
    }
  }
  else {
    
    while (True) {
      /* try to finish existing run */
      while (True) {
        if (s->strm->avail_out == 0) {
          return False;
        }
        if (s->state_out_len == 0) {
          break;
        }
        *( (UChar*)(s->strm->next_out) ) = s->state_out_ch;
        BZ_UPDATE_CRC(&s->calculatedBlockCRC, s->state_out_ch); // Funktionsaufruf (Pointer)
        s->state_out_len -= 1;
        s->strm->next_out += 1;
        s->strm->avail_out -= 1;
        s->strm->total_out_lo32 += 1;
        if (s->strm->total_out_lo32 == 0) {
          s->strm->total_out_hi32 += 1;
        }
      }
      
      /* can a new run be started? */
      if (s->nblock_used == s->save_nblock+1) {
        return False;
      }
      
      /* Only caused by corrupt data stream? */
      if (s->nblock_used > s->save_nblock+1) {
        return True;
      }
      
      s->state_out_len = 1;
      s->state_out_ch = s->k0;
      BZ_GET_SMALL(k1);
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      s->state_out_len = 2;
      BZ_GET_SMALL(k1);
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      s->state_out_len = 3;
      BZ_GET_SMALL(k1);
      s->nblock_used += 1;
      if (s->nblock_used == s->save_nblock+1) {
        continue;
      }
      if (k1 != s->k0) {
        s->k0 = k1;
        continue;
      }
      
      BZ_GET_SMALL(k1);
      s->nblock_used += 1;
      s->state_out_len = ((Int32)k1) + 4;
      BZ_GET_SMALL(s->k0);
      s->nblock_used += 1;
    }
  }
}

/*---------------------------------------------------*/
int BZ2_bzDecompress ( bz_stream *strm ) {
  Bool    corrupt;
  DState* s;
  if (strm == NULL) {
    return BZ_PARAM_ERROR;
  }
  s = strm->state;
  if (s == NULL) {
    return BZ_PARAM_ERROR;
  }
  if (s->strm != strm) {
    return BZ_PARAM_ERROR;
  }
  
  while (True) {
    if (s->state == BZ_X_IDLE) {
      return BZ_SEQUENCE_ERROR;
    }
    if (s->state == BZ_X_OUTPUT) {
      if (s->smallDecompress) {
        corrupt = unRLE_obuf_to_output_SMALL ( s );
      }
      else {
        corrupt = unRLE_obuf_to_output_FAST  ( s );
      }
      if (corrupt) {
        return BZ_DATA_ERROR;
      }
      if (s->nblock_used == s->save_nblock+1 && s->state_out_len == 0) {
        BZ_FINALISE_CRC ( &s->calculatedBlockCRC );
        if (s->calculatedBlockCRC != s->storedBlockCRC) {
          return BZ_DATA_ERROR;
        }
        s->calculatedCombinedCRC = (s->calculatedCombinedCRC << 1) | (s->calculatedCombinedCRC >> 31);
        s->calculatedCombinedCRC ^= s->calculatedBlockCRC;
        s->state = BZ_X_BLKHDR_1;
      }
      else {
        return BZ_OK;
      }
    }
    if (s->state >= BZ_X_MAGIC_1) {
      Int32 r = BZ2_decompress ( s );
      if (r == BZ_STREAM_END) {
        if (s->calculatedCombinedCRC != s->storedCombinedCRC) {
          return BZ_DATA_ERROR;
        }
        return r;
      }
      if (s->state != BZ_X_OUTPUT) {
        return r;
      }
    }
  }
  
  return 666;  /*NOTREACHED*/
}

/*---------------------------------------------------*/
int BZ2_bzDecompressEnd ( bz_stream *strm ) {
  DState* s;
  if (strm == NULL) {
    return BZ_PARAM_ERROR;
  }
  s = strm->state;
  if (s == NULL) {
    return BZ_PARAM_ERROR;
  }
  if (s->strm != strm) {
    return BZ_PARAM_ERROR;
  }
  
  if (s->tt   != NULL) {
    BZFREE(s->tt);
  }
  if (s->ll16 != NULL) {
    BZFREE(s->ll16);
  }
  if (s->ll4  != NULL) {
    BZFREE(s->ll4);
  }
  
  BZFREE(strm->state);
  strm->state = NULL;
  
  return BZ_OK;
}

/*---------------------------------------------------*/
/*--- File I/O stuff                              ---*/
/*---------------------------------------------------*/

#define BZ_SETERR(eee)                    \
{                                         \
if (bzerror != NULL) {*bzerror = eee;}   \
if (bzf != NULL) {bzf->lastErr = eee;}   \
}

typedef struct {
  FILE*     handle;
  Char      buf[BUFFER_SIZE];
  Int32     bufN;
  Bool      writing;
  bz_stream strm;
  Int32     lastErr;
  Bool      initialisedOk;
} bzFile;

/*---------------------------------------------*/
static Bool myfeof ( FILE* f ) {
  Int32 c = fgetc ( f );
  if (c == EOF) {
    return True;
  }
  ungetc ( c, f );
  return False;
}

/*---------------------------------------------------*/
BZFILE* BZ2_bzWriteOpen ( int* bzerror, FILE* f, int blockSize100k, int workFactor ) {
  Int32   ret;
  bzFile* bzf = NULL;
  
  BZ_SETERR(BZ_OK);
  
  if (f == NULL || (blockSize100k < 1 || blockSize100k > 9) || (workFactor < 0 || workFactor > 250)) {
    BZ_SETERR(BZ_PARAM_ERROR);
    return NULL;
  }
  
  if (ferror(f)) {
    BZ_SETERR(BZ_IO_ERROR);
    return NULL;
  }
  
  bzf = malloc ( sizeof(bzFile) );
  if (bzf == NULL) {
    BZ_SETERR(BZ_MEM_ERROR);
    return NULL;
  }
  
  BZ_SETERR(BZ_OK);
  bzf->initialisedOk = False;
  bzf->bufN          = 0;
  bzf->handle        = f;
  bzf->writing       = True;
  bzf->strm.bzalloc  = NULL;
  bzf->strm.bzfree   = NULL;
  bzf->strm.opaque   = NULL;
  
  if (workFactor == 0) {
    workFactor = 30;
  }
  ret = BZ2_bzCompressInit ( &(bzf->strm), blockSize100k, workFactor );
  if (ret != BZ_OK) {
    BZ_SETERR(ret);
    free(bzf);
    return NULL;
  }
  
  bzf->strm.avail_in = 0;
  bzf->initialisedOk = True;
  return bzf;
}

/*---------------------------------------------------*/

void BZ2_bzWrite ( int*    bzerror, BZFILE* b, void*   buf, int     len ) {
  Int32 n;
  unsigned long n2;
  Int32 ret;
  bzFile* bzf = (bzFile*)b;
  
  BZ_SETERR(BZ_OK);
  if (bzf == NULL || buf == NULL || len < 0) {
    BZ_SETERR(BZ_PARAM_ERROR);
    return;
  }
  if (!(bzf->writing)) {
    BZ_SETERR(BZ_SEQUENCE_ERROR);
    return;
  }
  if (ferror(bzf->handle)) {
    BZ_SETERR(BZ_IO_ERROR);
    return;
  }
  
  if (len == 0) {
    BZ_SETERR(BZ_OK);
    return;
  }
  
  bzf->strm.avail_in = len;
  bzf->strm.next_in  = buf;
  
  while (True) {
    bzf->strm.avail_out = BUFFER_SIZE;
    bzf->strm.next_out = bzf->buf;
    ret = BZ2_bzCompress ( &(bzf->strm), BZ_RUN );
    if (ret != BZ_RUN_OK) {
      BZ_SETERR(ret);
      return;
    }
    
    if (bzf->strm.avail_out < BUFFER_SIZE) {
      n = BUFFER_SIZE - bzf->strm.avail_out;
      n2 = fwrite ( (void*)(bzf->buf), sizeof(UChar), n, bzf->handle );
      if (n != n2 || ferror(bzf->handle)) {
        BZ_SETERR(BZ_IO_ERROR);
        return;
      }
    }
    
    if (bzf->strm.avail_in == 0) {
      BZ_SETERR(BZ_OK);
      return;
    }
  }
}


/*---------------------------------------------------*/
void BZ2_bzWriteClose ( int* bzerror, BZFILE* b, int abandon, unsigned int* nbytes_in, unsigned int* nbytes_out ) {
   BZ2_bzWriteClose64 ( bzerror, b, abandon, nbytes_in, NULL, nbytes_out, NULL );
}

void BZ2_bzWriteClose64 ( int* bzerror, BZFILE* b, int abandon, unsigned int* nbytes_in_lo32, unsigned int* nbytes_in_hi32, unsigned int* nbytes_out_lo32, unsigned int* nbytes_out_hi32 ) {
  Int32 n;
  unsigned long n2;
  Int32 ret;
  bzFile* bzf = (bzFile*)b;
  
  if (bzf == NULL) {
    BZ_SETERR(BZ_OK);
    return;
  }
  if (!(bzf->writing)) {
    BZ_SETERR(BZ_SEQUENCE_ERROR);
    return;
  }
  if (ferror(bzf->handle)) {
    BZ_SETERR(BZ_IO_ERROR);
    return;
  }
  
  if (nbytes_in_lo32 != NULL) {
    *nbytes_in_lo32 = 0;
  }
  if (nbytes_in_hi32 != NULL) {
    *nbytes_in_hi32 = 0;
  }
  if (nbytes_out_lo32 != NULL) {
    *nbytes_out_lo32 = 0;
  }
  if (nbytes_out_hi32 != NULL) {
    *nbytes_out_hi32 = 0;
  }
  
  if ((!abandon) && bzf->lastErr == BZ_OK) {
    while (True) {
      bzf->strm.avail_out = BUFFER_SIZE;
      bzf->strm.next_out = bzf->buf;
      ret = BZ2_bzCompress ( &(bzf->strm), BZ_FINISH );
      if (ret != BZ_FINISH_OK && ret != BZ_STREAM_END) {
        BZ_SETERR(ret);
        return;
      }
      
      if (bzf->strm.avail_out < BUFFER_SIZE) {
        n = BUFFER_SIZE - bzf->strm.avail_out;
        n2 = fwrite ( (void*)(bzf->buf), sizeof(UChar), n, bzf->handle );
        if (n != n2 || ferror(bzf->handle)) {
          BZ_SETERR(BZ_IO_ERROR);
          return;
        }
      }
      
      if (ret == BZ_STREAM_END) {
        break;
      }
    }
  }
  
  if ( !abandon && !ferror ( bzf->handle ) ) {
    fflush ( bzf->handle );
    if (ferror(bzf->handle)) {
      BZ_SETERR(BZ_IO_ERROR);
      return;
    }
  }
  
  if (nbytes_in_lo32 != NULL) {
    *nbytes_in_lo32 = bzf->strm.total_in_lo32;
  }
  if (nbytes_in_hi32 != NULL) {
    *nbytes_in_hi32 = bzf->strm.total_in_hi32;
  }
  if (nbytes_out_lo32 != NULL) {
    *nbytes_out_lo32 = bzf->strm.total_out_lo32;
  }
  if (nbytes_out_hi32 != NULL) {
    *nbytes_out_hi32 = bzf->strm.total_out_hi32;
  }
  
  BZ_SETERR(BZ_OK);
  BZ2_bzCompressEnd ( &(bzf->strm) );
  free ( bzf );
}

/*---------------------------------------------------*/
BZFILE* BZ2_bzReadOpen ( int* bzerror, FILE* f, int small, void* unused, int nUnused ) {
  bzFile* bzf = NULL;
  int     ret;
  
  BZ_SETERR(BZ_OK);
  
  if (f == NULL ||
      (small != False && small != True) ||
      (unused == NULL && nUnused != 0) ||
      (unused != NULL && (nUnused < 0 || nUnused > BUFFER_SIZE))) {
    BZ_SETERR(BZ_PARAM_ERROR);
    return NULL;
  }
  
  if (ferror(f)) {
    BZ_SETERR(BZ_IO_ERROR);
    return NULL;
  }
  
  bzf = malloc ( sizeof(bzFile) );
  if (bzf == NULL) {
    BZ_SETERR(BZ_MEM_ERROR);
    return NULL;
  }
  
  BZ_SETERR(BZ_OK);
  
  bzf->initialisedOk = False;
  bzf->handle        = f;
  bzf->bufN          = 0;
  bzf->writing       = False;
  bzf->strm.bzalloc  = NULL;
  bzf->strm.bzfree   = NULL;
  bzf->strm.opaque   = NULL;
  
  while (nUnused > 0) {
    bzf->buf[bzf->bufN] = *((UChar*)(unused));
    bzf->bufN += 1;
    unused = ((void*)( 1 + ((UChar*)(unused))  ));
    nUnused -= 1;
  }
  
  ret = BZ2_bzDecompressInit ( &(bzf->strm), small );
  if (ret != BZ_OK) {
    BZ_SETERR(ret);
    free(bzf);
    return NULL;
  }
  
  bzf->strm.avail_in = bzf->bufN;
  bzf->strm.next_in  = bzf->buf;
  
  bzf->initialisedOk = True;
  return bzf;
}

/*---------------------------------------------------*/
void BZ2_bzReadClose ( int *bzerror, BZFILE *b ) {
   bzFile* bzf = (bzFile*)b;

   BZ_SETERR(BZ_OK);
  if (bzf == NULL) {
    BZ_SETERR(BZ_OK);
    return;
  }

  if (bzf->writing) {
    BZ_SETERR(BZ_SEQUENCE_ERROR);
    return;
  }

  if (bzf->initialisedOk) {
    (void)BZ2_bzDecompressEnd ( &(bzf->strm) );
  }
   free ( bzf );
}

/*---------------------------------------------------*/
int BZ2_bzRead ( int* bzerror, BZFILE* b, void* buf, int len ) {
  Int32   n, ret;
  bzFile* bzf = (bzFile*)b;
  
  BZ_SETERR(BZ_OK);
  
  if (bzf == NULL || buf == NULL || len < 0) {
    BZ_SETERR(BZ_PARAM_ERROR);
    return 0;
  };
  
  if (bzf->writing) {
    BZ_SETERR(BZ_SEQUENCE_ERROR);
    return 0;
  };
  
  if (len == 0) {
    BZ_SETERR(BZ_OK);
    return 0;
  };
  
  bzf->strm.avail_out = len;
  bzf->strm.next_out = buf;
  
  while (True) {
    
    if (ferror(bzf->handle)) {
      BZ_SETERR(BZ_IO_ERROR);
      return 0;
    };
    
    if (bzf->strm.avail_in == 0 && !myfeof(bzf->handle)) {
      n = (unsigned int) fread ( bzf->buf, sizeof(UChar), BUFFER_SIZE, bzf->handle );
      if (ferror(bzf->handle)) {
        BZ_SETERR(BZ_IO_ERROR);
        return 0;
      }
      bzf->bufN = n;
      bzf->strm.avail_in = bzf->bufN;
      bzf->strm.next_in = bzf->buf;
    }
    
    ret = BZ2_bzDecompress ( &(bzf->strm) );
    
    if (ret != BZ_OK && ret != BZ_STREAM_END) {
      BZ_SETERR(ret);
      return 0;
    }
    
    if (ret == BZ_OK && myfeof(bzf->handle) &&
        bzf->strm.avail_in == 0 && bzf->strm.avail_out > 0) {
      BZ_SETERR(BZ_UNEXPECTED_EOF);
      return 0;
    };
    
    if (ret == BZ_STREAM_END) {
      BZ_SETERR(BZ_STREAM_END);
      return len - bzf->strm.avail_out;
    };
    if (bzf->strm.avail_out == 0) {
      BZ_SETERR(BZ_OK);
      return len;
    };
    
  }
  
  return 0; /*not reached*/
}

/*---------------------------------------------------*/
void BZ2_bzReadGetUnused ( int* bzerror, BZFILE* b, void** unused, int* nUnused ) {
  bzFile* bzf = (bzFile*)b;
  if (bzf == NULL) {
    BZ_SETERR(BZ_PARAM_ERROR);
    return;
  }
  if (bzf->lastErr != BZ_STREAM_END) {
    BZ_SETERR(BZ_SEQUENCE_ERROR);
    return;
  }
  if (unused == NULL || nUnused == NULL) {
    BZ_SETERR(BZ_PARAM_ERROR);
    return;
  };
  
  BZ_SETERR(BZ_OK);
  *nUnused = bzf->strm.avail_in;
  *unused = bzf->strm.next_in;
}

/*---------------------------------------------------*/
/*--- Misc convenience stuff                      ---*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
int BZ2_bzBuffToBuffCompress (char* dest, unsigned int* destLen, char* source, unsigned int sourceLen, int  blockSize100k, int workFactor) {
  bz_stream strm;
  int ret;
  
  if (dest == NULL || destLen == NULL || source == NULL || blockSize100k < 1 || blockSize100k > 9 || workFactor < 0 || workFactor > 250) {
    return BZ_PARAM_ERROR;
  }
  
  if (workFactor == 0) {
    workFactor = 30;
  }
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit ( &strm, blockSize100k, workFactor );
  if (ret != BZ_OK) {
    return ret;
  }
  
  strm.next_in = source;
  strm.next_out = dest;
  strm.avail_in = sourceLen;
  strm.avail_out = *destLen;
  
  ret = BZ2_bzCompress ( &strm, BZ_FINISH );
  if (ret == BZ_FINISH_OK) {
    BZ2_bzCompressEnd ( &strm );
    return BZ_OUTBUFF_FULL;
  }
  if (ret != BZ_STREAM_END) {
    BZ2_bzCompressEnd ( &strm );
    return ret;
  }
  
  /* normal termination */
  *destLen -= strm.avail_out;
  BZ2_bzCompressEnd ( &strm );
  return BZ_OK;
}

/*---------------------------------------------------*/
int BZ2_bzBuffToBuffDecompress (char* dest, unsigned int* destLen, char* source, unsigned int sourceLen, int small) {
  bz_stream strm;
  int ret;
  
  if (dest == NULL || destLen == NULL || source == NULL || (small != 0 && small != 1) ) {
    return BZ_PARAM_ERROR;
  }
  
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzDecompressInit ( &strm, small );
  if (ret != BZ_OK) {
    return ret;
  }
  
  strm.next_in = source;
  strm.next_out = dest;
  strm.avail_in = sourceLen;
  strm.avail_out = *destLen;
  
  ret = BZ2_bzDecompress ( &strm );
  if (ret == BZ_OK) {
    if (strm.avail_out > 0) {
      BZ2_bzDecompressEnd ( &strm );
      return BZ_UNEXPECTED_EOF;
    }
    else {
      BZ2_bzDecompressEnd ( &strm );
      return BZ_OUTBUFF_FULL;
    };
  }
  if (ret != BZ_STREAM_END) {
    BZ2_bzDecompressEnd ( &strm );
    return ret;
  }
  
  /* normal termination */
  *destLen -= strm.avail_out;
  BZ2_bzDecompressEnd ( &strm );
  return BZ_OK;
}

#   define SET_BINARY_MODE(file)
static BZFILE * bzopen_or_bzdopen (const char *path,   /* no use when bzdopen */
                                   int fd,             /* no use when bzdopen */
                                   const char *mode,
                                   int open_mode) {     /* bzopen: 0, bzdopen:1 */
  int    bzerr;
  char   unused[BUFFER_SIZE];
  int    blockSize100k            = 9;
  Bool   isWriting                = False;
  char   mode2[10]                = "";
  FILE   *fp                      = NULL;
  BZFILE *bzip2FilePointer        = NULL;
  const int    workFactor         = 30;
  Bool   isSmallMode              = False;
  
  if (mode == NULL) {
    return NULL;
  }
  while (*mode) {
    switch (*mode) {
      case 'r':
        isWriting = False;
        break;
      case 'w':
        isWriting = True;
        break;
      case 's':
        isSmallMode = False;
        break;
      default:
        if (isdigit((int)(*mode))) {
          blockSize100k = *mode-BZ_HDR_0;
        }
    }
    mode += 1;
  }
  
  strcat(mode2, isWriting ? "wb" : "rb" );
  
  /* open fds with O_CLOEXEC _only_ when we are the initiator
   * aka. bzopen() but not bzdopen() */
  if (open_mode == 0) {
    strcat (mode2, "e");
    if (path==NULL || strcmp(path,"")==0) {
      fp = (isWriting ? stdout : stdin);
      SET_BINARY_MODE(fp);
    }
    else {
      fp = fopen(path,mode2);
    }
  }
  else {
    fp = fdopen(fd,mode2);
  }
  if (fp == NULL) {
    return NULL;
  }
  
  if (isWriting) {
    /* Guard against total chaos and anarchy -- JRS */
    if (blockSize100k < 1) {
      blockSize100k = 1;
    }
    if (blockSize100k > 9) {
      blockSize100k = 9;
    }
    bzip2FilePointer = BZ2_bzWriteOpen (&bzerr, fp, blockSize100k, workFactor);
  }
  else {
    bzip2FilePointer = BZ2_bzReadOpen (&bzerr, fp, isSmallMode, unused, 0);
  }
  if (bzip2FilePointer == NULL) {
    if (fp != stdin && fp != stdout) {
      fclose(fp);
    }
    return NULL;
  }
  return bzip2FilePointer;
}

/*---------------------------------------------------*/
/*--
   open file for read or write.
      ex) bzopen("file","w9")
      case path="" or NULL => use stdin or stdout.
--*/
BZFILE * BZ2_bzopen ( const char *path, const char *mode ) {
  return bzopen_or_bzdopen(path,-1,mode,/*bzopen*/0);
}

/*---------------------------------------------------*/
BZFILE * BZ2_bzdopen ( int fd, const char *mode ) {
   return bzopen_or_bzdopen(NULL,fd,mode,/*bzdopen*/1);
}

/*---------------------------------------------------*/
int BZ2_bzread (BZFILE* b, void* buf, int len ) {
  int bzerr;
  int nread;
  if (((bzFile*)b)->lastErr == BZ_STREAM_END) {
    return 0;
  }
  nread = BZ2_bzRead(&bzerr,b,buf,len);
  if (bzerr == BZ_OK || bzerr == BZ_STREAM_END) {
    return nread;
  }
  else {
    return -1;
  }
}

/*---------------------------------------------------*/
int BZ2_bzwrite (BZFILE* b, void* buf, int len ) {
  int bzerr;
  
  BZ2_bzWrite(&bzerr,b,buf,len);
  switch (bzerr) {
    case  BZ_OK:
      return len;
    default:
      return -1;
  }
}

/*---------------------------------------------------*/
int BZ2_bzflush (BZFILE *b) {
   /* do nothing now... */
   return 0;
}

/*---------------------------------------------------*/
void BZ2_bzclose (BZFILE* b) {
  int bzerr;
  FILE *fp;
  
  if (NULL != b) {
    fp = ((bzFile *)b)->handle;
    if (((bzFile*)b)->writing) {
      BZ2_bzWriteClose(&bzerr,b,0,NULL,NULL);
      if(bzerr != BZ_OK) {
        BZ2_bzWriteClose(NULL,b,1,NULL,NULL);
      }
    }
    else {
      BZ2_bzReadClose(&bzerr,b);
    }
    // wenn der Dateizeiger nicht auf die Standard-Eingabe zeigt und wenn der Dateizeiger nicht auf Standard-Ausgabe zeigt
    if (fp!=stdin && fp!=stdout) {
      // Schliesse die Datei hinter dem Dateizeiger
      fclose(fp);
    }
  }
}

inline void BZ_UPDATE_CRC(uint32_t *crcVar, uint8_t cha) {
  *crcVar = (*crcVar << 8) ^ BZ2_crc32Table[(*crcVar >> 24) ^ cha];
}
inline void BZ_FINALISE_CRC(uint32_t *crcVar) {
  *crcVar = ~(*crcVar);
}

/*---------------------------------------------------*/
/*--
   return last error code
--*/
static const char *bzerrorstrings[] = {
       "OK"
      ,"SEQUENCE_ERROR"
      ,"PARAM_ERROR"
      ,"MEM_ERROR"
      ,"DATA_ERROR"
      ,"DATA_ERROR_MAGIC"
      ,"IO_ERROR"
      ,"UNEXPECTED_EOF"
      ,"OUTBUFF_FULL"
      ,"CONFIG_ERROR"
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
};


const char * BZ2_bzerror (BZFILE *b, int *errnum) {
  int err = ((bzFile *)b)->lastErr;
  
  if(err>0) {
    err = 0;
  }
  *errnum = err;
  return bzerrorstrings[err*-1];
}

/*-------------------------------------------------------------*/
/*--- end                                           bzlib.c ---*/
/*-------------------------------------------------------------*/
