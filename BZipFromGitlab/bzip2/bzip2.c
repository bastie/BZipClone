
/*-----------------------------------------------------------*/
/*--- A block-sorting, lossless compressor        bzip2.c ---*/
/*-----------------------------------------------------------*/

/* ------------------------------------------------------------------
   This file is part of bzip2/libbzip2, a program and library for
   lossless, block-sorting data compression.

   bzip2/libbzip2 version 1.1.0 of 6 September 2010
   Copyright (C) 1996-2010 Julian Seward <jseward@acm.org>

   Please read the WARNING, DISCLAIMER and PATENTS sections in the
   README file.

   This program is released under the terms of the license contained
   in the file LICENSE.
   ------------------------------------------------------------------ */


/*---------------------------------------------*/
/*--
  Some stuff for all platforms.
--*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include "bzlib.h"

#define ERROR_IF_EOF(i)       { if ((i) == EOF)  handleIoErrorsAndExitApplication(); }
#define ERROR_IF_NOT_ZERO(i)  { if ((i) != 0)    handleIoErrorsAndExitApplication(); }
#define ERROR_IF_MINUS_ONE(i) { if ((i) == (-1)) handleIoErrorsAndExitApplication(); }

/*---------------------------------------------*/

#   include <fcntl.h>
#   include <sys/types.h>
#   include <utime.h>
#   include <unistd.h>
#   include <sys/stat.h>
#   include <sys/times.h>

#   define PATH_SEP    '/'
#   define MY_LSTAT    lstat
#   define MY_STAT     stat
#   define MY_S_ISREG  S_ISREG
#   define MY_S_ISDIR  S_ISDIR

#   define APPEND_FILESPEC(root, name) \
      root=snocString((root), (name))

#   define APPEND_FLAG(root, name) \
      root=snocString((root), (name))

#      define NORETURN __attribute__ ((noreturn))


/*---------------------------------------------*/
/*--
  Some more stuff for all platforms :-)
--*/

typedef char            Char;
typedef unsigned char   Bool;
typedef unsigned char   UChar;
typedef int             Int32;
typedef unsigned int    UInt32;
typedef short           Int16;
typedef unsigned short  UInt16;

#define True  ((Bool)1)
#define False ((Bool)0)

/*--
  IntNative is your platform's `native' int size.
  Only here to avoid probs with 64-bit platforms.
--*/
typedef int IntNative;


/*---------------------------------------------------*/
/*--- Misc (file handling) data decls             ---*/
/*---------------------------------------------------*/

/**
 @brief Steuert den Detaillierungsgrad der Programmausgabe.
 
 Diese Variable legt fest, wie ausführlich das Programm während seiner Ausführung
 Informationen ausgibt. Höhere Werte führen zu detaillierteren Ausgaben,
 während niedrigere Werte die Ausgabe auf wesentliche Informationen beschränken.
 
 @discussion
 Die folgenden Werte sind zulässig:
 
     0: Keine Ausgabe (nur kritische Fehler)
 
     1: Grundlegende Informationen
 
     2: Detaillierte Informationen
 
     3: Sehr detaillierte Informationen (Debug-Modus)
 
     4: Maximale Ausführlichkeit (umfassender Debug-Modus)
 
 @note Höhere Werte führen zu einer umfangreicheren Ausgabe, die bei der
 Fehlersuche und Überwachung des Programmablaufs hilfreich sein kann. Werte
 höher als 4 werden als 4 interpretiert.
 
 @code
 // Beispielhafte Verwendung
 if (verbosity >= 2) {
   printf("Debug: Aktueller Wert von x ist %d\n", x);
 }
 @endcode
 */
Int32   verbosity;
Bool    keepInputFiles, smallMode, deleteOutputOnInterrupt;
Bool    forceOverwrite, testFailsExist, unzFailsExist;
Bool quiet;
Int32   numFileNames, numFilesProcessed, blockSize100k;
Int32   exitValue;

/*-- source modes --*/
static const int SourceMode_StandardInput2StandardOutput = 1;
static const int SourceMode_File2StandardOutput = 2;
static const int SourceMode_File2File = 3;

/*-- operation modes --*/
static const int OPERATION_MODE_COMPRESS = 1;
static const int OPERATION_MODE_DECOMPRESS = 2;
static const int OPERATION_MODE_TEST = 3;

Int32   operationMode;
Int32   srcMode;

#define FILE_NAME_LEN 1034

Int32   longestFileName;
Char    inName [FILE_NAME_LEN];
Char    outName[FILE_NAME_LEN];
Char    tmpName[FILE_NAME_LEN];
Char    *progName;
Char    progNameReally[FILE_NAME_LEN];
FILE    *outputHandleJustInCase;
Int32   workFactor;

static void    panic                 ( const Char* ) NORETURN;
static void    handleIoErrorsAndExitApplication        ( void )        NORETURN;
static void    outOfMemory           ( void )        NORETURN;
static void    printConfigErrorAndExit           ( void )        NORETURN;
static void    crcError              ( void )        NORETURN;
static void    cleanUpAndFailAndExitApplication        ( Int32 )       NORETURN;
static void    compressedStreamEOF   ( void )        NORETURN;

static void    copyFileName ( Char*, Char* );
static void*   myMalloc     ( Int32 );
static void    applySavedFileAttrToOutputFile ( IntNative fd );



/*---------------------------------------------------*/
/*--- An implementation of 64-bit ints.  Sigh.    ---*/
/*--- Roll on widespread deployment of ANSI C9X ! ---*/
/*---------------------------------------------------*/

typedef struct {
  UChar b[8];
} UInt64;


static void uInt64_from_UInt32s ( UInt64* n, UInt32 lo32, UInt32 hi32 ) {
   n->b[7] = (UChar)((hi32 >> 24) & 0xFF);
   n->b[6] = (UChar)((hi32 >> 16) & 0xFF);
   n->b[5] = (UChar)((hi32 >> 8)  & 0xFF);
   n->b[4] = (UChar) (hi32        & 0xFF);
   n->b[3] = (UChar)((lo32 >> 24) & 0xFF);
   n->b[2] = (UChar)((lo32 >> 16) & 0xFF);
   n->b[1] = (UChar)((lo32 >> 8)  & 0xFF);
   n->b[0] = (UChar) (lo32        & 0xFF);
}


static double uInt64_to_double ( UInt64* n ) {
   double base = 1.0;
   double sum  = 0.0;
   for (int i = 0; i < 8; i++) {
      sum  += base * (double)(n->b[i]);
      base *= 256.0;
   }
   return sum;
}


static Bool uInt64_isZero ( UInt64* n ) {
  for (int i = 0; i < 8; i++) {
    if (n->b[i] != 0) {
      return 0;
    }
  }
  return 1;
}


/** (KI generierte Dokumentation)
 @brief Teilt eine 64-Bit-Ganzzahl durch 10 und gibt den Rest zurück.
 
 Die Funktion `uInt64_qrm10` teilt die übergebene 64-Bit-Ganzzahl `n` durch 10.
 Dabei wird die Zahl Byte für Byte (in Big-Endian-Reihenfolge) verarbeitet.
 Das Ergebnis der Division wird in den Bytes des Eingabeparameters `n` gespeichert,
 und der Rest der Division wird als Rückgabewert zurückgegeben.
 
 @param n Ein Zeiger auf eine 64-Bit-Ganzzahl (UInt64), die durch 10 geteilt werden soll.
 Die Bytes des Ergebnisses werden in diesem Parameter gespeichert.
 
 @return Der Rest der Division (0-9).
 
 @note Die Funktion modifiziert den Inhalt des übergebenen `UInt64`-Parameters `n`.
 
 @code
 UInt64 myNumber;
 myNumber.b[0] = 0;
 myNumber.b[1] = 0;
 myNumber.b[2] = 0;
 myNumber.b[3] = 0;
 myNumber.b[4] = 0;
 myNumber.b[5] = 0;
 myNumber.b[6] = 1;
 myNumber.b[7] = 234; // Beispiel: myNumber = 490
 
 Int32 remainder = uInt64_qrm10(&myNumber);
 
 // Nach dem Aufruf:
 // myNumber.b[0..6] enthält das Ergebnis der Division (49)
 // myNumber.b[7] == 0
 // remainder == 0
 @endcode
 */
static Int32 uInt64_qrm10 ( UInt64* n ) {
  UInt32 rem;
  UInt32 tmp;
  rem = 0;
  for (int i = 7; i >= 0; i--) {
    tmp = rem * 256 + n->b[i];
    n->b[i] = tmp / 10;
    rem = tmp % 10;
  }
  return rem;
}


/** (KI generierte Dokumentation)
 @brief Konvertiert einen UInt64-Wert in eine ASCII-Zeichenkette.
 
 Diese Funktion konvertiert einen gegebenen UInt64-Wert in seine entsprechende
 ASCII-Zeichenkettendarstellung. Die resultierende Zeichenkette wird in den
 bereitgestellten Ausgabepuffer geschrieben.
 
 @param outbuf Ein Zeiger auf den Ausgabepuffer, in den die ASCII-Zeichenkette
 geschrieben wird. Der Puffer muss ausreichend groß sein, um die
 resultierende Zeichenkette aufzunehmen (maximal 20 Zeichen + Nullterminierung).
 
 @param n Ein Zeiger auf den UInt64-Wert, der konvertiert werden soll.
 
 @discussion Die Funktion verwendet interne Hilfsfunktionen `uInt64_qrm10` und
 `uInt64_isZero`, um die Konvertierung durchzuführen. Die resultierende
 Zeichenkette ist nullterminiert.
 
 @note Der Ausgabepuffer `outbuf` muss vom Aufrufer bereitgestellt werden. Es wird
 keine Fehlerprüfung auf die Größe des Puffers durchgeführt.
 
 @code
 UInt64 value = 1234567890123456789ULL;
 char buffer[21]; // 20 Ziffern + Nullterminierung
 uInt64_toAscii(buffer, &value);
 // buffer enthält nun "1234567890123456789"
 @endcode
 
 @see uInt64_qrm10
 
 @see uInt64_isZero
 */
static void uInt64_toAscii ( char* outbuf, UInt64* n ) {
  Int32  q;
  UChar  buf[32];
  Int32  nBuf   = 0;
  UInt64 n_copy = *n;
  do {
    q = uInt64_qrm10 ( &n_copy );
    buf[nBuf] = q + '0';
    nBuf += 1;
  }
  while (!uInt64_isZero(&n_copy));
  
  outbuf[nBuf] = 0;
  for (int i = 0; i < nBuf; i++) {
    outbuf[i] = buf[nBuf-i-1];
  }
}


/*---------------------------------------------------*/
/*--- Processing of complete files and streams    ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
/**  (KI generierte Dokumentation)
 @brief Überprüft, ob das Dateiende eines Dateistreams erreicht wurde.
 
 Diese Funktion überprüft, ob das Ende eines gegebenen Dateistreams erreicht wurde,
 ohne dabei das nächste Zeichen zu konsumieren.
 
 @param f Ein Zeiger auf den FILE-Stream, der überprüft werden soll.
 
 @return `True`, wenn das Dateiende erreicht ist, andernfalls `False`.
 
 @discussion Die Funktion liest temporär ein Zeichen aus dem Stream, um zu überprüfen,
 ob das Dateiende erreicht ist. Wenn das Dateiende erreicht ist (EOF),
 gibt die Funktion `True` zurück. Andernfalls wird das gelesene Zeichen
 mit `ungetc` zurück in den Stream geschoben, und die Funktion gibt `False` zurück.
 
 @note Diese Funktion ist eine benutzerdefinierte Alternative zu `feof`, die das
 nächste Zeichen nicht konsumiert.
 
 @code
 FILE *file = fopen("datei.txt", "r");
 if (file != NULL) {
   while (!myfeof(file)) {
     // Verarbeite das nächste Zeichen
     int c = fgetc(file);
     if (c != EOF) {
       // ...
     }
   }
   fclose(file);
 }
 @endcode
 
 @see feof
 @see fgetc
 @see ungetc
 */
static Bool myfeof ( FILE* f ) {
  // lese ein Zeichen aus dem Eingabstrom als int Wert
  Int32 c = fgetc ( f );
  // Falls ein Fehler auftritt
  if (c == EOF) {
    // gebe true zurück
    return True;
  }
  // wenn kein Fehler auftritt
  else {
    // lege das Zeichen zurück in den Eingabestrom
    ungetc ( c, f );
    // gebe false zurück
    return False;
  }
}


/*---------------------------------------------*/
void handleErrors (int* bzerror, BZFILE* bzf, int abandon, unsigned int* nbytes_in_lo32, unsigned int* nbytes_in_hi32, unsigned int* nbytes_out_lo32, unsigned int* nbytes_out_hi32, int bzerr);

inline void handleErrors (int* bzerror, BZFILE* bzf, int abandon, unsigned int* nbytes_in_lo32, unsigned int* nbytes_in_hi32, unsigned int* nbytes_out_lo32, unsigned int* nbytes_out_hi32, int bzerr) {
  
  BZ2_bzWriteClose64 ( bzerror, bzf, 1,
                      nbytes_in_lo32, nbytes_in_hi32,
                      nbytes_out_lo32, nbytes_out_hi32 );
  switch (bzerr) {
    case BZ_CONFIG_ERROR:
      printConfigErrorAndExit();
      break;
    case BZ_MEM_ERROR:
      outOfMemory ();
      break;
    case BZ_IO_ERROR:
      handleIoErrorsAndExitApplication();
      break;
    default:
      panic ( "compress:unexpected error" );
  }
  
  panic ( "compress:end" );
  /*notreached*/
}

static void compressStream ( FILE *stream, FILE *zStream ) {
  BZFILE* bzf = NULL;
  const int bufferSize = 5000;
  UChar   buffer[bufferSize];
  unsigned long   countOfElementsInBuffer;
  UInt32  nbytes_in_lo32;
  UInt32  nbytes_in_hi32;
  UInt32  nbytes_out_lo32;
  UInt32  nbytes_out_hi32;
  Int32   bzerr;
  Int32   bzerr_dummy;
  Int32   ret;
  
  if (ferror(stream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  if (ferror(zStream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  
  bzf = BZ2_bzWriteOpen ( &bzerr, zStream, blockSize100k, verbosity, workFactor );
  if (bzerr != BZ_OK) {
    // führe die Fehlerbehandlung aus
    handleErrors (&bzerr_dummy, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
  }
  
  if (verbosity >= 2) {
    fprintf ( stderr, "\n" );
  }
  
  while (True) {
    // Wenn das Ende des Eingabestroms erreicht ist
    if (myfeof(stream)) {
      // Beende die Schleife
      break;
    }
    // Lese aus dem Eingabstrom `stream` maximal soviele Elemente wie in `bufferSize` definiert ist, wobei ein Elemen eine Anzahl von Bytes entspricht die `sizeof(UChar)` zurückgibt und speicher diese im Puffer `buffer`. Die Anzahl der gelesenen Zeichen speichere dabei in `countOfElementsInBuffer`.
    countOfElementsInBuffer = fread ( buffer, sizeof(UChar), bufferSize, stream );
    // prüfe, ob beim lesen ein Fehler aufgetreten ist
    if (ferror(stream)) {
      // führe die Fehlerbehandlung aus
      handleIoErrorsAndExitApplication();
    }
    // Wenn aus dem Strom etwas gelesen wurde
    if (countOfElementsInBuffer > 0) {
      // rufe die Funktion `BZ2_bzWrite` auf
      BZ2_bzWrite ( &bzerr, bzf, (void*)buffer, (int)countOfElementsInBuffer );
    }
    if (bzerr != BZ_OK) {
      // führe die Fehlerbehandlung aus
      handleErrors (&bzerr_dummy, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
    }
  }
  
  BZ2_bzWriteClose64 ( &bzerr, bzf, 0, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32 );
  if (bzerr != BZ_OK) {
    // führe die Fehlerbehandlung aus
    handleErrors (&bzerr_dummy, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
  }
  
  if (ferror(zStream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  // empfehle dem Betriebssystem den Buffer für zStream zu leeren
  ret = fflush ( zStream );
  // wenn ein Fehler aufgetreten ist
  if (ret == EOF) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  if (zStream != stdout) {
    Int32 fd = fileno ( zStream );
    if (fd < 0) {
      // führe die Fehlerbehandlung aus
      handleIoErrorsAndExitApplication();
    }
    applySavedFileAttrToOutputFile ( fd );
    ret = fclose ( zStream );
    outputHandleJustInCase = NULL;
    if (ret == EOF) {
      // führe die Fehlerbehandlung aus
      handleIoErrorsAndExitApplication();
    }
  }
  outputHandleJustInCase = NULL;
  if (ferror(stream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  ret = fclose ( stream );
  if (ret == EOF) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  
  if (verbosity >= 1) {
    if (nbytes_in_lo32 == 0 && nbytes_in_hi32 == 0) {
      fprintf ( stderr, " no data compressed.\n");
    }
    else {
      Char   buf_nin[32];
      Char   buf_nout[32];
      UInt64 nbytes_in,   nbytes_out;
      double nbytes_in_d, nbytes_out_d;
      uInt64_from_UInt32s ( &nbytes_in, nbytes_in_lo32, nbytes_in_hi32 );
      uInt64_from_UInt32s ( &nbytes_out, nbytes_out_lo32, nbytes_out_hi32 );
      nbytes_in_d  = uInt64_to_double ( &nbytes_in );
      nbytes_out_d = uInt64_to_double ( &nbytes_out );
      uInt64_toAscii ( buf_nin, &nbytes_in );
      uInt64_toAscii ( buf_nout, &nbytes_out );
      fprintf ( stderr, "%6.3f:1, %6.3f bits/byte, " "%5.2f%% saved, %s in, %s out.\n", nbytes_in_d / nbytes_out_d, (8.0 * nbytes_out_d) / nbytes_in_d, 100.0 * (1.0 - nbytes_out_d / nbytes_in_d), buf_nin, buf_nout);
    }
  }
  
  // beenden der Funktion (ohne Fehler)
  return;
}


/*---------------------------------------------*/
static Bool uncompressStream ( FILE *zStream, FILE *stream ) {
  const int bufferSize = 5000;
  BZFILE* bzf = NULL;
  Int32   bzerr;
  Int32 bzerr_dummy;
  Int32 ret;
  unsigned long nread;
  Int32 streamNo;
  Int32 i;
  UChar   obuf[bufferSize];
  UChar   unused[bufferSize];
  Int32   nUnused;
  void*   unusedTmpV;
  UChar*  unusedTmp;
  
  nUnused = 0;
  streamNo = 0;
  
  if (ferror(stream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  if (ferror(zStream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  
  while (True) {
    
    bzf = BZ2_bzReadOpen ( &bzerr, zStream, verbosity, (int)smallMode, unused, nUnused );
    if (bzf == NULL || bzerr != BZ_OK) {
      goto errhandler;
    }
    streamNo += 1;
    
    while (bzerr == BZ_OK) {
      nread = BZ2_bzRead ( &bzerr, bzf, obuf, bufferSize );
      if (bzerr == BZ_DATA_ERROR_MAGIC) {
        goto trycat;
      }
      if ((bzerr == BZ_OK || bzerr == BZ_STREAM_END) && nread > 0) {
        fwrite ( obuf, sizeof(UChar), nread, stream );
      }
      if (ferror(stream)) {
        // führe die Fehlerbehandlung aus
        handleIoErrorsAndExitApplication();
      }
    }
    if (bzerr != BZ_STREAM_END) {
      goto errhandler;
    }
    
    BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
    if (bzerr != BZ_OK) {
      panic ( "decompress:bzReadGetUnused" );
    }
    
    unusedTmp = (UChar*)unusedTmpV;
    for (i = 0; i < nUnused; i++) {
      unused[i] = unusedTmp[i];
    }
    
    BZ2_bzReadClose ( &bzerr, bzf );
    if (bzerr != BZ_OK) {
      panic ( "decompress:bzReadGetUnused" );
    }
    
    if (nUnused == 0 && myfeof(zStream)) {
      break;
    }
  }
  
closeok:
  if (ferror(zStream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  if (stream != stdout) {
    Int32 fd = fileno ( stream );
    if (fd < 0) {
      // führe die Fehlerbehandlung aus
      handleIoErrorsAndExitApplication();
    }
    applySavedFileAttrToOutputFile ( fd );
  }
  ret = fclose ( zStream );
  if (ret == EOF) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  
  if (ferror(stream)) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  ret = fflush ( stream );
  if (ret != 0) {
    // führe die Fehlerbehandlung aus
    handleIoErrorsAndExitApplication();
  }
  if (stream != stdout) {
    ret = fclose ( stream );
    outputHandleJustInCase = NULL;
    if (ret == EOF) {
      // führe die Fehlerbehandlung aus
      handleIoErrorsAndExitApplication();
    }
  }
  outputHandleJustInCase = NULL;
  if (verbosity >= 2) {
    fprintf ( stderr, "\n    " );
  }
  return True;
  
trycat:
  if (forceOverwrite) {
    rewind(zStream);
    while (True) {
      if (myfeof(zStream)) {
        break;
      }
      nread = fread ( obuf, sizeof(UChar), bufferSize, zStream );
      if (ferror(zStream)) {
        // führe die Fehlerbehandlung aus
        handleIoErrorsAndExitApplication();
      }
      if (nread > 0) {
        fwrite ( obuf, sizeof(UChar), nread, stream );
      }
      if (ferror(stream)) {
        // führe die Fehlerbehandlung aus
        handleIoErrorsAndExitApplication();
      }
    }
    goto closeok;
  }
  
errhandler:
  BZ2_bzReadClose ( &bzerr_dummy, bzf );
  switch (bzerr) {
    case BZ_CONFIG_ERROR:
      printConfigErrorAndExit(); break;
    case BZ_IO_ERROR:
      handleIoErrorsAndExitApplication(); break;
    case BZ_DATA_ERROR:
      crcError();
    case BZ_MEM_ERROR:
      outOfMemory();
    case BZ_UNEXPECTED_EOF:
      compressedStreamEOF();
    case BZ_DATA_ERROR_MAGIC:
      if (zStream != stdin) fclose(zStream);
      if (stream != stdout) fclose(stream);
      if (streamNo == 1) {
        return False;
      } else {
        if (!quiet) {
          fprintf ( stderr, "\n%s: %s: trailing garbage after EOF ignored\n", progName, inName );
        }
        return True;
      }
    default:
      panic ( "decompress:unexpected error" );
  }
  
  panic ( "decompress:end" );
  return True; /*notreached*/
}


/*---------------------------------------------*/
static Bool testStream ( FILE *zStream ) {
  const int bufferSize = 5000;
  BZFILE* bzf = NULL;
  Int32   bzerr, bzerr_dummy, ret, streamNo, i;
  UChar   obuf[bufferSize];
  UChar   unused[bufferSize];
  Int32   nUnused;
  void*   unusedTmpV;
  UChar*  unusedTmp;
  
  nUnused = 0;
  streamNo = 0;
  
  if (ferror(zStream)) {
    handleIoErrorsAndExitApplication();
  }
  
  while (True) {
    
    bzf = BZ2_bzReadOpen ( &bzerr, zStream, verbosity, (int)smallMode, unused, nUnused );
    if (bzf == NULL || bzerr != BZ_OK) {
      goto errhandler;
    }
    streamNo += 1;
    
    while (bzerr == BZ_OK) {
      BZ2_bzRead ( &bzerr, bzf, obuf, bufferSize );
      if (bzerr == BZ_DATA_ERROR_MAGIC) {
        goto errhandler;
      }
    }
    if (bzerr != BZ_STREAM_END) {
      goto errhandler;
    }
    
    BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
    if (bzerr != BZ_OK) {
      panic ( "test:bzReadGetUnused" );
    }
    
    unusedTmp = (UChar*)unusedTmpV;
    for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];
    
    BZ2_bzReadClose ( &bzerr, bzf );
    if (bzerr != BZ_OK) {
      panic ( "test:bzReadGetUnused" );
    }
    if (nUnused == 0 && myfeof(zStream)) {
      break;
    }
    
  }
  
  if (ferror(zStream)) {
    handleIoErrorsAndExitApplication();
  }
  ret = fclose ( zStream );
  if (ret == EOF) {
    handleIoErrorsAndExitApplication();
  }
  
  if (verbosity >= 2) {
    fprintf ( stderr, "\n    " );
  }
  return True;
  
errhandler:
  BZ2_bzReadClose ( &bzerr_dummy, bzf );
  if (verbosity == 0)
    fprintf ( stderr, "%s: %s: ", progName, inName );
  switch (bzerr) {
    case BZ_CONFIG_ERROR:
      printConfigErrorAndExit();
      break;
    case BZ_IO_ERROR:
      // rufe die Funktion `ioError` auf (welche später `exit(int)` aufruft
      handleIoErrorsAndExitApplication();
      break;
    case BZ_DATA_ERROR:
      fprintf ( stderr, "data integrity (CRC) error in data\n" );
      return False;
    case BZ_MEM_ERROR:
      outOfMemory();
    case BZ_UNEXPECTED_EOF:
      fprintf ( stderr, "file ends unexpectedly\n" );
      return False;
    case BZ_DATA_ERROR_MAGIC:
      if (zStream != stdin) {
        fclose(zStream);
      }
      if (streamNo == 1) {
        fprintf ( stderr, "bad magic number (file not created by bzip2)\n" );
        return False;
      }
      else {
        if (!quiet) {
          fprintf ( stderr, "trailing garbage after EOF ignored\n" );
        }
        return True;
      }
    default:
      panic ( "test:unexpected error" );
  }
  
  panic ( "test:end" );
  return True; /*notreached*/
}


/*---------------------------------------------------*/
/*--- Error [non-] handling grunge                ---*/
/*---------------------------------------------------*/

/** (KI generiert nach funktionsinterner Kommentierung)
 @brief Setzt den Exit-Statuscode der Anwendung.
 
 Diese Funktion aktualisiert den globalen Exit-Statuscode der Anwendung,
 falls der neue Wert größer ist als der aktuell gespeicherte Wert.
 
 @param newExitValue Der neue Exit-Statuscode, der gesetzt werden soll.
 
 @discussion
 Die Funktion vergleicht den übergebenen `newExitValue` mit dem aktuell
 gespeicherten `exitValue`. Wenn `newExitValue` größer ist, wird er
 als neuer `exitValue` gespeichert. Andernfalls bleibt der aktuelle
 `exitValue` unverändert.
 
 @note
 Diese Funktion dient dazu, den höchsten aufgetretenen Fehlercode während
 der Ausführung der Anwendung zu speichern.
 
 @code
 setExit(1); // Setzt den Exit-Code auf 1, falls er bisher kleiner war
 setExit(2); // Setzt den Exit-Code auf 2, falls er bisher kleiner war
 @endcode
 
 @see exit
 */
static void setExit ( Int32 newExitValue ) {
  // wenn der übergebene neue Wert für den Statuscode der Anwendung größer ist als der bisher gespeicherte Statuscode
  if (newExitValue > exitValue) {
    // speichere den neuen Statuscode
    exitValue = newExitValue;
  }
  else {
    // behalte den bisherigen Statuscode
  }
}


/*---------------------------------------------*/
static void cadvise ( void ) {
  if (!quiet) {
    fprintf (
             stderr,
             "\nIt is possible that the compressed file(s) have become corrupted.\n"
             "You can use the -tvv option to test integrity of such files.\n\n"
             "You can use the `bzip2recover' program to attempt to recover\n"
             "data from undamaged sections of corrupted files.\n\n"
             );
  }
}


/*---------------------------------------------*/
static void showFileNames ( void ) {
  if (!quiet) {
    fprintf ( stderr, "\tInput file = %s, output file = %s\n", inName, outName );
  }
}


/*---------------------------------------------*/
static void cleanUpAndFailAndExitApplication ( Int32 ec ) {
  IntNative      retVal;
  struct MY_STAT statBuf;
  
  if ( srcMode == SourceMode_File2File && operationMode != OPERATION_MODE_TEST && deleteOutputOnInterrupt ) {
    
    /* Check whether input file still exists.  Delete output file
     only if input exists to avoid loss of data.  Joerg Prante, 5
     January 2002.  (JRS 06-Jan-2002: other changes in 1.0.2 mean
     this is less likely to happen.  But to be ultra-paranoid, we
     do the check anyway.)  */
    retVal = MY_STAT ( inName, &statBuf );
    if (retVal == 0) {
      if (!quiet) {
        fprintf ( stderr, "%s: Deleting output file %s, if it exists.\n", progName, outName );
      }
      if (outputHandleJustInCase != NULL) {
        fclose ( outputHandleJustInCase );
      }
      retVal = remove ( outName );
      if (retVal != 0) {
        fprintf ( stderr, "%s: WARNING: deletion of output file " "(apparently) failed.\n", progName );
      }
    }
    else {
      fprintf ( stderr, "%s: WARNING: deletion of output file suppressed\n", progName );
      fprintf ( stderr, "%s:    since input file no longer exists.  Output file\n", progName );
      fprintf ( stderr, "%s:    `%s' may be incomplete.\n", progName, outName );
      fprintf ( stderr, "%s:    I suggest doing an integrity test (bzip2 -tv)" " of it.\n", progName );
    }
  }
  
  if (!quiet && numFileNames > 0 && numFilesProcessed < numFileNames) {
    fprintf ( stderr, "%s: WARNING: some files have not been processed:\n" "%s:    %d specified on command line, %d not processed yet.\n\n", progName, progName, numFileNames, numFileNames - numFilesProcessed );
  }
  setExit(ec);
  exit(exitValue);
}


/*---------------------------------------------*/
static void panic ( const Char* s ) {
  fprintf ( stderr,
           "\n%s: PANIC -- internal consistency error:\n"
           "\t%s\n"
           "\tThis is a BUG.  Please report it at:\n"
           "\thttps://gitlab.com/bzip2/bzip2/-/issues\n",
           progName, s );
  showFileNames();
  cleanUpAndFailAndExitApplication( 3 );
}


/*---------------------------------------------*/
static void crcError ( void ) {
  fprintf ( stderr,
           "\n%s: Data integrity error when decompressing.\n",
           progName );
  showFileNames();
  cadvise();
  cleanUpAndFailAndExitApplication( 2 );
}


/*---------------------------------------------*/
static void compressedStreamEOF ( void ) {
  if (!quiet) {
    fprintf ( stderr, "\n%s: Compressed file ends unexpectedly;\n\t" "perhaps it is corrupted?  *Possible* reason follows.\n", progName );
    perror ( progName );
    showFileNames();
    cadvise();
  }
  cleanUpAndFailAndExitApplication( 2 );
}


/*---------------------------------------------*/
static void handleIoErrorsAndExitApplication ( void ) {
  fprintf ( stderr, "\n%s: I/O or other error, bailing out.  " "Possible reason follows.\n", progName );
  perror ( progName );
  showFileNames();
  cleanUpAndFailAndExitApplication( 1 );
}


/*---------------------------------------------*/
static void mySignalCatcher ( IntNative n ) {
  fprintf ( stderr,
           "\n%s: Control-C or similar caught, quitting.\n",
           progName );
  cleanUpAndFailAndExitApplication(1);
}


/*---------------------------------------------*/
static void mySIGSEGVorSIGBUScatcher ( IntNative n ) {
  const char *msg;
  if (operationMode == OPERATION_MODE_COMPRESS) {
    msg = ": Caught a SIGSEGV or SIGBUS whilst compressing.\n"
    "\n"
    "   Possible causes are (most likely first):\n"
    "   (1) This computer has unreliable memory or cache hardware\n"
    "       (a surprisingly common problem; try a different machine.)\n"
    "   (2) A bug in the compiler used to create this executable\n"
    "       (unlikely, if you didn't compile bzip2 yourself.)\n"
    "   (3) A real bug in bzip2 -- I hope this should never be the case.\n"
    "   The user's manual, Section 4.3, has more info on (1) and (2).\n"
    "   \n"
    "   If you suspect this is a bug in bzip2, or are unsure about (1)\n"
    "   or (2), report it at: https://gitlab.com/bzip2/bzip2/-/issues\n"
    "   Section 4.3 of the user's manual describes the info a useful\n"
    "   bug report should have.  If the manual is available on your\n"
    "   system, please try and read it before mailing me.  If you don't\n"
    "   have the manual or can't be bothered to read it, mail me anyway.\n"
    "\n";
  }
  else {
    msg = ": Caught a SIGSEGV or SIGBUS whilst decompressing.\n"
    "\n"
    "   Possible causes are (most likely first):\n"
    "   (1) The compressed data is corrupted, and bzip2's usual checks\n"
    "       failed to detect this.  Try bzip2 -tvv my_file.bz2.\n"
    "   (2) This computer has unreliable memory or cache hardware\n"
    "       (a surprisingly common problem; try a different machine.)\n"
    "   (3) A bug in the compiler used to create this executable\n"
    "       (unlikely, if you didn't compile bzip2 yourself.)\n"
    "   (4) A real bug in bzip2 -- I hope this should never be the case.\n"
    "   The user's manual, Section 4.3, has more info on (2) and (3).\n"
    "   \n"
    "   If you suspect this is a bug in bzip2, or are unsure about (2)\n"
    "   or (3), report it at: https://gitlab.com/bzip2/bzip2/-/issues\n"
    "   Section 4.3 of the user's manual describes the info a useful\n"
    "   bug report should have.  If the manual is available on your\n"
    "   system, please try and read it before mailing me.  If you don't\n"
    "   have the manual or can't be bothered to read it, mail me anyway.\n"
    "\n";
  }
  write ( STDERR_FILENO, "\n", 1 );
  write ( STDERR_FILENO, progName, strlen ( progName ) );
  write ( STDERR_FILENO, msg, strlen ( msg ) );
  
  msg = "\tInput file = ";
  write ( STDERR_FILENO, msg, strlen (msg) );
  write ( STDERR_FILENO, inName, strlen (inName) );
  write ( STDERR_FILENO, "\n", 1 );
  msg = "\tOutput file = ";
  write ( STDERR_FILENO, msg, strlen (msg) );
  write ( STDERR_FILENO, outName, strlen (outName) );
  write ( STDERR_FILENO, "\n", 1 );
  
  /* Don't call cleanupAndFail. If we ended up here something went
   terribly wrong. Trying to clean up might fail spectacularly. */
  
  if (operationMode == OPERATION_MODE_COMPRESS) {
    setExit(3);
  }
  else {
    setExit(2);
  }
  _exit(exitValue);
}


/*---------------------------------------------*/
static void outOfMemory ( void ) {
  fprintf ( stderr,
           "\n%s: couldn't allocate enough memory\n",
           progName );
  showFileNames();
  cleanUpAndFailAndExitApplication(1);
}


/*---------------------------------------------*/
static void printConfigErrorAndExit ( void ) {
  fprintf ( stderr,
           "bzip2: I'm not configured correctly for this platform!\n"
           "\tI require Int32, Int16 and Char to have sizes\n"
           "\tof 4, 2 and 1 bytes to run properly, and they don't.\n"
           "\tProbably you can fix this by defining them correctly,\n"
           "\tand recompiling.  Bye!\n" );
  setExit(3);
  exit(exitValue);
}


/*---------------------------------------------------*/
/*--- The main driver machinery                   ---*/
/*---------------------------------------------------*/

/* All rather crufty.  The main problem is that input files
   are stat()d multiple times before use.  This should be
   cleaned up.
*/

/*---------------------------------------------*/
static void pad ( Char *s ) {
  Int32 i;
  if ( (Int32)strlen(s) >= longestFileName ) {return;}
  for (i = 1; i <= longestFileName - (Int32)strlen(s); i++) {
    fprintf ( stderr, " " );
  }
}


/** (KI generiert und manuell angepasst)
 @brief Kopiert einen Dateinamen sicher.
 
 Diese Funktion kopiert einen Dateinamen von einem Quellpuffer in einen Zielpuffer,
 wobei eine Längenprüfung durchgeführt wird, um Pufferüberläufe zu verhindern.
 
 @param to Ein Zeiger auf den Zielpuffer, in den der Dateiname kopiert wird.
 @param from Ein Zeiger auf den Quellpuffer, der den zu kopierenden Dateinamen enthält.
 
 @discussion
 Die Funktion überprüft, ob die Länge des Quell-Dateinamens die maximale zulässige
 Länge (`FILE_NAME_LEN - 10`) überschreitet. Wenn dies der Fall ist, wird eine
 Fehlermeldung auf `stderr` ausgegeben und das Programm beendet. Andernfalls wird
 der Dateiname mit `strncpy` in den Zielpuffer kopiert.
 
 @note
 `FILE_NAME_LEN` ist eine definierte Konstante, die die maximale Länge des
 Dateinamens angibt. Die Funktion reserviert 10 Zeichen für eventuelle
 zusätzliche Zeichen (z.B. eine Dateiendung).
 
 @warning
 Wenn der Dateiname zu lang ist, wird das Programm mit einem Fehlercode beendet.
 
 @code
 char sourceFileName[] = "my_very_long_file_name.txt";
 char destinationFileName[256]; // Angenommen FILE_NAME_LEN ist 266
 copyFileName(destinationFileName, sourceFileName);
 @endcode
 */
static void copyFileName ( Char* to, Char* from ) {
  if ( strlen(from) > FILE_NAME_LEN-10 )  {
    fprintf (
             stderr,
             "bzip2: file name\n`%s'\n"
             "is suspiciously (more than %d chars) long.\n"
             "Try using a reasonable file name instead.  Sorry! :-)\n",
             from, FILE_NAME_LEN-10
             );
    setExit(1);
    exit(exitValue);
  }
  
  strncpy(to,from,FILE_NAME_LEN-10);
  to[FILE_NAME_LEN-10]='\0';
}


/*---------------------------------------------*/
static Bool fileExists ( Char* name ) {
  FILE *tmp   = fopen ( name, "rb" );
  Bool exists = (tmp != NULL);
  if (tmp != NULL) {
    fclose ( tmp );
  }
  return exists;
}


/*---------------------------------------------*/
/* Open an output file safely with O_EXCL and good permissions.
   This avoids a race condition in versions < 1.0.2, in which
   the file was first opened and then had its interim permissions
   set safely.  We instead use open() to create the file with
   the interim permissions required. (--- --- rw-).

   For non-Unix platforms, if we are not worrying about
   security issues, simple this simply behaves like fopen.
*/
static FILE* fopen_output_safely ( Char* name, const char* mode ) {
  FILE*     fp;
  IntNative fh;
  fh = open(name, O_WRONLY|O_CREAT|O_EXCL, S_IWUSR|S_IRUSR);
  if (fh == -1) {return NULL;}
  fp = fdopen(fh, mode);
  if (fp == NULL) {close(fh);}
  return fp;
}


/*---------------------------------------------*/
/*--
  if in doubt, return True
--*/
static Bool notAStandardFile ( Char* name ) {
  IntNative      i;
  struct MY_STAT statBuf;
  
  i = MY_LSTAT ( name, &statBuf );
  if (i != 0) {return True;}
  if (MY_S_ISREG(statBuf.st_mode)) {return False;}
  return True;
}


/*---------------------------------------------*/
/*--
  rac 11/21/98 see if file has hard links to it
--*/
static Int32 countHardLinks ( Char* name ) {
  IntNative      i;
  struct MY_STAT statBuf;
  
  i = MY_LSTAT ( name, &statBuf );
  if (i != 0) { return 0; }
  return (statBuf.st_nlink - 1);
}


/*---------------------------------------------*/
/* Copy modification date, access date, permissions and owner from the
   source to destination file.  We have to copy this meta-info off
   into fileMetaInfo before starting to compress / decompress it,
   because doing it afterwards means we get the wrong access time.

   To complicate matters, in compress() and decompress() below, the
   sequence of tests preceding the call to saveInputFileMetaInfo()
   involves calling fileExists(), which in turn establishes its result
   by attempting to fopen() the file, and if successful, immediately
   fclose()ing it again.  So we have to assume that the fopen() call
   does not cause the access time field to be updated.

   Reading of the man page for stat() (man 2 stat) on RedHat 7.2 seems
   to imply that merely doing open() will not affect the access time.
   Therefore we merely need to hope that the C library only does
   open() as a result of fopen(), and not any kind of read()-ahead
   cleverness.

   It sounds pretty fragile to me.  Whether this carries across
   robustly to arbitrary Unix-like platforms (or even works robustly
   on this one, RedHat 7.2) is unknown to me.  Nevertheless ...
*/
#if __APPLE__
static
struct MY_STAT fileMetaInfo;
#endif

static
void saveInputFileMetaInfo ( Char *srcName )
{
#  if __APPLE__
   IntNative retVal;
   /* Note use of stat here, not lstat. */
   retVal = MY_STAT( srcName, &fileMetaInfo );
   ERROR_IF_NOT_ZERO ( retVal );
#  endif
}


static
void applySavedTimeInfoToOutputFile ( Char *dstName )
{
#  if __APPLE__
   IntNative      retVal;
   struct utimbuf uTimBuf;

   uTimBuf.actime = fileMetaInfo.st_atime;
   uTimBuf.modtime = fileMetaInfo.st_mtime;

   retVal = utime ( dstName, &uTimBuf );
   ERROR_IF_NOT_ZERO ( retVal );
#  endif
}

static
void applySavedFileAttrToOutputFile ( IntNative fd )
{
#  if __APPLE__
   IntNative retVal;

   retVal = fchmod ( fd, fileMetaInfo.st_mode );
   ERROR_IF_NOT_ZERO ( retVal );

   (void) fchown ( fd, fileMetaInfo.st_uid, fileMetaInfo.st_gid );
   /* chown() will in many cases return with EPERM, which can
      be safely ignored.
   */
#  endif
}


/*---------------------------------------------*/
static
Bool containsDubiousChars ( Char* name )
{
#  if __APPLE__
   /* On unix, files can contain any characters and the file expansion
    * is performed by the shell.
    */
   return False;
#  else /* ! BZ_UNIX */
   /* On non-unix (Win* platforms), wildcard characters are not allowed in
    * filenames.
    */
   for (; *name != '\0'; name++)
      if (*name == '?' || *name == '*') return True;
   return False;
#  endif /* BZ_UNIX */
}


/*---------------------------------------------*/
#define BZ_N_SUFFIX_PAIRS 4

const Char* zSuffix[BZ_N_SUFFIX_PAIRS]
   = { ".bz2", ".bz", ".tbz2", ".tbz" };
const Char* unzSuffix[BZ_N_SUFFIX_PAIRS]
   = { "", "", ".tar", ".tar" };

static Bool hasSuffix ( Char* s, const Char* suffix ) {
  size_t ns = strlen(s);
  size_t nx = strlen(suffix);
  if (ns < nx) {
    return False;
  }
  if (strcmp(s + ns - nx, suffix) == 0) {
    return True;
  }
  return False;
}

static
Bool mapSuffix ( Char* name,
                 const Char* oldSuffix,
                const Char* newSuffix ) {
  if (!hasSuffix(name,oldSuffix)) {
    return False;
  }
  name[strlen(name)-strlen(oldSuffix)] = 0;
  strcat ( name, newSuffix );
  return True;
}


/*---------------------------------------------*/
static
void compress ( Char *name )
{
   FILE  *inStr;
   FILE  *outStr;
   Int32 n, i;
   struct MY_STAT statBuf;

   deleteOutputOnInterrupt = False;

   if (name == NULL && srcMode != SourceMode_StandardInput2StandardOutput)
      panic ( "compress: bad modes\n" );

   switch (srcMode) {
      case SourceMode_StandardInput2StandardOutput:
         copyFileName ( inName, (Char*)"(stdin)" );
         copyFileName ( outName, (Char*)"(stdout)" );
         break;
      case SourceMode_File2File:
         copyFileName ( inName, name );
         copyFileName ( outName, name );
         strcat ( outName, ".bz2" );
         break;
      case SourceMode_File2StandardOutput:
         copyFileName ( inName, name );
         copyFileName ( outName, (Char*)"(stdout)" );
         break;
   }

   if ( srcMode != SourceMode_StandardInput2StandardOutput && containsDubiousChars ( inName ) ) {
     if (!quiet) {
       fprintf ( stderr, "%s: There are no files matching `%s'.\n", progName, inName );
     }
      setExit(1);
      return;
   }
   if ( srcMode != SourceMode_StandardInput2StandardOutput && !fileExists ( inName ) ) {
      fprintf ( stderr, "%s: Can't open input file %s: %s.\n",
                progName, inName, strerror(errno) );
      setExit(1);
      return;
   }
   for (i = 0; i < BZ_N_SUFFIX_PAIRS; i++) {
      if (hasSuffix(inName, zSuffix[i])) {
        if (!quiet) {
          fprintf ( stderr, "%s: Input file %s already has %s suffix.\n", progName, inName, zSuffix[i] );
        }
         setExit(1);
         return;
      }
   }
   if ( srcMode == SourceMode_File2File || srcMode == SourceMode_File2StandardOutput ) {
      MY_STAT(inName, &statBuf);
      if ( MY_S_ISDIR(statBuf.st_mode) ) {
         fprintf( stderr, "%s: Input file %s is a directory.\n", progName,inName);
         setExit(1);
         return;
      }
   }
   if ( srcMode == SourceMode_File2File && !forceOverwrite && notAStandardFile ( inName )) {
     if (!quiet) {
       fprintf ( stderr, "%s: Input file %s is not a normal file.\n", progName, inName );
     }
      setExit(1);
      return;
   }
   if ( srcMode == SourceMode_File2File && fileExists ( outName ) ) {
      if (forceOverwrite) {
         remove(outName);
      } else {
         fprintf ( stderr, "%s: Output file %s already exists.\n",
            progName, outName );
         setExit(1);
         return;
      }
   }
   if ( srcMode == SourceMode_File2File && !forceOverwrite &&
        (n=countHardLinks ( inName )) > 0) {
      fprintf ( stderr, "%s: Input file %s has %d other link%s.\n",
                progName, inName, n, n > 1 ? "s" : "" );
      setExit(1);
      return;
   }

   if ( srcMode == SourceMode_File2File ) {
      /* Save the file's meta-info before we open it.  Doing it later
         means we mess up the access times. */
      saveInputFileMetaInfo ( inName );
   }

   switch ( srcMode ) {

      case SourceMode_StandardInput2StandardOutput:
         inStr = stdin;
         outStr = stdout;
         if ( isatty ( fileno ( stdout ) ) ) {
            fprintf ( stderr,
                      "%s: I won't write compressed data to a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            setExit(1);
            return;
         };
         break;

      case SourceMode_File2StandardOutput:
         inStr = fopen ( inName, "rb" );
         outStr = stdout;
         if ( isatty ( fileno ( stdout ) ) ) {
            fprintf ( stderr,
                      "%s: I won't write compressed data to a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            if ( inStr != NULL ) fclose ( inStr );
            setExit(1);
            return;
         };
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s: %s.\n",
                      progName, inName, strerror(errno) );
            setExit(1);
            return;
         };
         break;

      case SourceMode_File2File:
         inStr = fopen ( inName, "rb" );
         outStr = fopen_output_safely ( outName, "wb" );
         if ( outStr == NULL) {
            fprintf ( stderr, "%s: Can't create output file %s: %s.\n",
                      progName, outName, strerror(errno) );
            if ( inStr != NULL ) fclose ( inStr );
            setExit(1);
            return;
         }
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s: %s.\n",
                      progName, inName, strerror(errno) );
            if ( outStr != NULL ) fclose ( outStr );
            setExit(1);
            return;
         };
         break;

      default:
         panic ( "compress: bad srcMode" );
         break;
   }

   if (verbosity >= 1) {
      fprintf ( stderr,  "  %s: ", inName );
      pad ( inName );
      fflush ( stderr );
   }

   /*--- Now the input and output handles are sane.  Do the Biz. ---*/
   outputHandleJustInCase = outStr;
   deleteOutputOnInterrupt = True;
   compressStream ( inStr, outStr );
   outputHandleJustInCase = NULL;

   /*--- If there was an I/O error, we won't get here. ---*/
   if ( srcMode == SourceMode_File2File ) {
      applySavedTimeInfoToOutputFile ( outName );
      deleteOutputOnInterrupt = False;
      if ( !keepInputFiles ) {
         IntNative retVal = remove ( inName );
         ERROR_IF_NOT_ZERO ( retVal );
      }
   }

   deleteOutputOnInterrupt = False;
}


/*---------------------------------------------*/
static
void uncompress ( Char *name )
{
   FILE  *inStr;
   FILE  *outStr;
   Int32 n, i;
   Bool  magicNumberOK;
   Bool  cantGuess;
   struct MY_STAT statBuf;

   deleteOutputOnInterrupt = False;

   if (name == NULL && srcMode != SourceMode_StandardInput2StandardOutput)
      panic ( "uncompress: bad modes\n" );

   cantGuess = False;
   switch (srcMode) {
      case SourceMode_StandardInput2StandardOutput:
         copyFileName ( inName, (Char*)"(stdin)" );
         copyFileName ( outName, (Char*)"(stdout)" );
         break;
      case SourceMode_File2File:
         copyFileName ( inName, name );
         copyFileName ( outName, name );
         for (i = 0; i < BZ_N_SUFFIX_PAIRS; i++)
            if (mapSuffix(outName,zSuffix[i],unzSuffix[i]))
               goto zzz;
         cantGuess = True;
         strcat ( outName, ".out" );
         break;
      case SourceMode_File2StandardOutput:
         copyFileName ( inName, name );
         copyFileName ( outName, (Char*)"(stdout)" );
         break;
   }

   zzz:
  if ( srcMode != SourceMode_StandardInput2StandardOutput && containsDubiousChars ( inName ) ) {
    if (!quiet) {
      fprintf ( stderr, "%s: There are no files matching `%s'.\n", progName, inName );
    }
    setExit(1);
    return;
  }
   if ( srcMode != SourceMode_StandardInput2StandardOutput && !fileExists ( inName ) ) {
      fprintf ( stderr, "%s: Can't open input file %s: %s.\n",
                progName, inName, strerror(errno) );
      setExit(1);
      return;
   }
   if ( srcMode == SourceMode_File2File || srcMode == SourceMode_File2StandardOutput ) {
      MY_STAT(inName, &statBuf);
      if ( MY_S_ISDIR(statBuf.st_mode) ) {
         fprintf( stderr,
                  "%s: Input file %s is a directory.\n",
                  progName,inName);
         setExit(1);
         return;
      }
   }
   if ( srcMode == SourceMode_File2File && !forceOverwrite && notAStandardFile ( inName )) {
     if (!quiet) {
       fprintf ( stderr, "%s: Input file %s is not a normal file.\n", progName, inName );
     }
      setExit(1);
      return;
   }
   if ( /* srcMode == SM_F2F implied && */ cantGuess ) {
     if (!quiet) {
       fprintf ( stderr, "%s: Can't guess original name for %s -- using %s\n", progName, inName, outName );
     }
      /* just a warning, no return */
   }
   if ( srcMode == SourceMode_File2File && fileExists ( outName ) ) {
      if (forceOverwrite) {
         remove(outName);
      }
      else {
        fprintf ( stderr, "%s: Output file %s already exists.\n", progName, outName );
        setExit(1);
        return;
      }
   }
   if ( srcMode == SourceMode_File2File && !forceOverwrite &&
        (n=countHardLinks ( inName ) ) > 0) {
      fprintf ( stderr, "%s: Input file %s has %d other link%s.\n",
                progName, inName, n, n > 1 ? "s" : "" );
      setExit(1);
      return;
   }

   if ( srcMode == SourceMode_File2File ) {
      /* Save the file's meta-info before we open it.  Doing it later
         means we mess up the access times. */
      saveInputFileMetaInfo ( inName );
   }

   switch ( srcMode ) {

      case SourceMode_StandardInput2StandardOutput:
         inStr = stdin;
         outStr = stdout;
         if ( isatty ( fileno ( stdin ) ) ) {
            fprintf ( stderr,
                      "%s: I won't read compressed data from a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            setExit(1);
            return;
         };
         break;

      case SourceMode_File2StandardOutput:
         inStr = fopen ( inName, "rb" );
         outStr = stdout;
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s:%s.\n",
                      progName, inName, strerror(errno) );
            if ( inStr != NULL ) fclose ( inStr );
            setExit(1);
            return;
         };
         break;

      case SourceMode_File2File:
         inStr = fopen ( inName, "rb" );
         outStr = fopen_output_safely ( outName, "wb" );
         if ( outStr == NULL) {
            fprintf ( stderr, "%s: Can't create output file %s: %s.\n",
                      progName, outName, strerror(errno) );
            if ( inStr != NULL ) fclose ( inStr );
            setExit(1);
            return;
         }
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s: %s.\n",
                      progName, inName, strerror(errno) );
            if ( outStr != NULL ) fclose ( outStr );
            setExit(1);
            return;
         };
         break;

      default:
         panic ( "uncompress: bad srcMode" );
         break;
   }

   if (verbosity >= 1) {
      fprintf ( stderr, "  %s: ", inName );
      pad ( inName );
      fflush ( stderr );
   }

   /*--- Now the input and output handles are sane.  Do the Biz. ---*/
   outputHandleJustInCase = outStr;
   deleteOutputOnInterrupt = True;
   magicNumberOK = uncompressStream ( inStr, outStr );
   outputHandleJustInCase = NULL;

   /*--- If there was an I/O error, we won't get here. ---*/
   if ( magicNumberOK ) {
      if ( srcMode == SourceMode_File2File ) {
         applySavedTimeInfoToOutputFile ( outName );
         deleteOutputOnInterrupt = False;
         if ( !keepInputFiles ) {
            IntNative retVal = remove ( inName );
            ERROR_IF_NOT_ZERO ( retVal );
         }
      }
   } else {
      unzFailsExist = True;
      deleteOutputOnInterrupt = False;
      if ( srcMode == SourceMode_File2File ) {
         IntNative retVal = remove ( outName );
         ERROR_IF_NOT_ZERO ( retVal );
      }
   }
   deleteOutputOnInterrupt = False;

   if ( magicNumberOK ) {
      if (verbosity >= 1)
         fprintf ( stderr, "done\n" );
   } else {
      setExit(2);
      if (verbosity >= 1)
         fprintf ( stderr, "not a bzip2 file.\n" ); else
         fprintf ( stderr,
                   "%s: %s is not a bzip2 file.\n",
                   progName, inName );
   }

}


/*---------------------------------------------*/
static
void testf ( Char *name )
{
   FILE *inStr;
   Bool allOK;
   struct MY_STAT statBuf;

   deleteOutputOnInterrupt = False;

   if (name == NULL && srcMode != SourceMode_StandardInput2StandardOutput)
      panic ( "testf: bad modes\n" );

   copyFileName ( outName, (Char*)"(none)" );
   switch (srcMode) {
      case SourceMode_StandardInput2StandardOutput: copyFileName ( inName, (Char*)"(stdin)" ); break;
      case SourceMode_File2File: copyFileName ( inName, name ); break;
      case SourceMode_File2StandardOutput: copyFileName ( inName, name ); break;
   }

   if ( srcMode != SourceMode_StandardInput2StandardOutput && containsDubiousChars ( inName ) ) {
     if (!quiet) {
       fprintf ( stderr, "%s: There are no files matching `%s'.\n", progName, inName );
     }
      setExit(1);
      return;
   }
   if ( srcMode != SourceMode_StandardInput2StandardOutput && !fileExists ( inName ) ) {
      fprintf ( stderr, "%s: Can't open input %s: %s.\n", progName, inName, strerror(errno) );
      setExit(1);
      return;
   }
   if ( srcMode != SourceMode_StandardInput2StandardOutput ) {
      MY_STAT(inName, &statBuf);
      if ( MY_S_ISDIR(statBuf.st_mode) ) {
         fprintf( stderr, "%s: Input file %s is a directory.\n", progName,inName);
         setExit(1);
         return;
      }
   }

   switch ( srcMode ) {

      case SourceMode_StandardInput2StandardOutput:
         if ( isatty ( fileno ( stdin ) ) ) {
            fprintf ( stderr,
                      "%s: I won't read compressed data from a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            setExit(1);
            return;
         };
         inStr = stdin;
         break;

      case SourceMode_File2StandardOutput: case SourceMode_File2File:
         inStr = fopen ( inName, "rb" );
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s:%s.\n",
                      progName, inName, strerror(errno) );
            setExit(1);
            return;
         };
         break;

      default:
         panic ( "testf: bad srcMode" );
         break;
   }

   if (verbosity >= 1) {
      fprintf ( stderr, "  %s: ", inName );
      pad ( inName );
      fflush ( stderr );
   }

   /*--- Now the input handle is sane.  Do the Biz. ---*/
   outputHandleJustInCase = NULL;
   allOK = testStream ( inStr );

   if (allOK && verbosity >= 1) fprintf ( stderr, "ok\n" );
   if (!allOK) testFailsExist = True;
}


/*---------------------------------------------*/
static
void license ( void )
{
   fprintf ( stdout,

    "bzip2, a block-sorting file compressor.  "
    "Version %s.\n"
    "   \n"
    "   Copyright (C) 1996-2010 by Julian Seward.\n"
    "   \n"
    "   This program is free software; you can redistribute it and/or modify\n"
    "   it under the terms set out in the LICENSE file, which is included\n"
    "   in the bzip2-1.0.6 source distribution.\n"
    "   \n"
    "   This program is distributed in the hope that it will be useful,\n"
    "   but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "   LICENSE file for more details.\n"
    "   \n",
    BZ2_bzlibVersion()
   );
}


/*---------------------------------------------*/
static void usage ( Char *fullProgName ) {
   fprintf (
      stderr,
      "bzip2, a block-sorting file compressor.  "
      "Version %s.\n"
      "\n   usage: %s [flags and input files in any order]\n"
      "\n"
      "   -h --help           print this message\n"
      "   -d --decompress     force decompression\n"
      "   -z --compress       force compression\n"
      "   -k --keep           keep (don't delete) input files\n"
      "   -f --force          overwrite existing output files\n"
      "   -t --test           test compressed file integrity\n"
      "   -c --stdout         output to standard out\n"
      "   -q --quiet          suppress noncritical error messages\n"
      "   -v --verbose        be verbose (a 2nd -v gives more)\n"
      "   -L --license        display software version & license\n"
      "   -V --version        display software version & license\n"
      "   -s --small          use less memory (at most 2500k)\n"
      "   -1 .. -9            set block size to 100k .. 900k\n"
      "   --fast              alias for -1\n"
      "   --best              alias for -9\n"
      "\n"
      "   If invoked as `bzip2', default action is to compress.\n"
      "              as `bunzip2',  default action is to decompress.\n"
      "              as `bzcat', default action is to decompress to stdout.\n"
      "\n"
      "   If no file names are given, bzip2 compresses or decompresses\n"
      "   from standard input to standard output.  You can combine\n"
      "   short flags, so `-v -4' means the same as -v4 or -4v, &c.\n"
      "\n"
      ,

      BZ2_bzlibVersion(),
      fullProgName
   );
}


/*---------------------------------------------*/
static
void redundant ( Char* flag )
{
   fprintf (
      stderr,
      "%s: %s is redundant in versions 0.9.5 and above\n",
      progName, flag );
}


/*---------------------------------------------*/
/*--
  All the garbage from here to main() is purely to
  implement a linked list of command-line arguments,
  into which main() copies argv[1 .. argc-1].

  The purpose of this exercise is to facilitate
  the expansion of wildcard characters * and ? in
  filenames for OSs which don't know how to do it
  themselves, like MSDOS, Windows 95 and NT.

  The actual Dirty Work is done by the platform-
  specific macro APPEND_FILESPEC.
--*/

typedef
   struct zzzz {
      Char        *name;
      struct zzzz *link;
   }
   Cell;


/*---------------------------------------------*/
static void *myMalloc ( Int32 n ) {
  void* p;
  
  p = malloc ( (size_t)n );
  if (p == NULL) {
    outOfMemory ();
  }
  return p;
}


/*---------------------------------------------*/
static
Cell *mkCell ( void )
{
   Cell *c;

   c = (Cell*) myMalloc ( sizeof ( Cell ) );
   c->name = NULL;
   c->link = NULL;
   return c;
}


/*---------------------------------------------*/
static Cell *snocString ( Cell *root, Char *name ) {
  if (root == NULL) {
    Cell *tmp = mkCell();
    tmp->name = (Char*) myMalloc ( 5 + (unsigned int)strlen(name) );
    strcpy ( tmp->name, name );
    return tmp;
  }
  else {
    Cell *tmp = root;
    while (tmp->link != NULL) tmp = tmp->link;
    tmp->link = snocString ( tmp->link, name );
    return root;
  }
}


/*---------------------------------------------*/
static void addFlagsFromEnvVar ( Cell** argList, Char* varName ) {
  Int32 i, j, k;
  Char *envbase, *p;
  
  envbase = getenv(varName);
  if (envbase != NULL) {
    p = envbase;
    i = 0;
    while (True) {
      if (p[i] == 0) {
        break;
      }
      p += i;
      i = 0;
      while (isspace((Int32)(p[0]))) {
        p += 1;
      }
      while (p[i] != 0 && !isspace((Int32)(p[i]))) {
        i += 1;
      }
      if (i > 0) {
        k = i;
        if (k > FILE_NAME_LEN-10) {
          k = FILE_NAME_LEN-10;
        }
        for (j = 0; j < k; j++) {
          tmpName[j] = p[j];
        }
        tmpName[k] = 0;
        APPEND_FLAG(*argList, tmpName);
      }
    }
  }
}


/*---------------------------------------------*/
#define ISFLAG(s) (strcmp(aa->name, (s))==0)


int main ( int argc, char *argv[] ) {
  Int32  i = 0;
  Int32  j = 0;
  Char   *tmp;
  Cell   *argList;
  Cell   *aa;
  Bool   decode;
  
  // Stelle sicher, dass die Größe der Typen für den Algorithmus stimmen.
  if (sizeof(Int32) != 4 || sizeof(UInt32) != 4  ||
      sizeof(Int16) != 2 || sizeof(UInt16) != 2  ||
      sizeof(Char)  != 1 || sizeof(UChar)  != 1) {
    printConfigErrorAndExit();
  }
  
  // Initialisiere die Variablen mit Standardwerten
  outputHandleJustInCase  = NULL;
  smallMode               = False;
  keepInputFiles          = False;
  forceOverwrite          = False;
  quiet                   = False;
  verbosity               = 0;
  blockSize100k           = 9;
  testFailsExist          = False;
  unzFailsExist           = False;
  numFileNames            = 0;
  numFilesProcessed       = 0;
  workFactor              = 30;
  deleteOutputOnInterrupt = False;
  exitValue               = 0;
  
  /*-- Set up signal handlers for mem access errors --*/
  signal (SIGSEGV, mySIGSEGVorSIGBUScatcher);
  signal (SIGBUS,  mySIGSEGVorSIGBUScatcher);
  
  copyFileName ( inName,  (Char*)"(none)" );
  copyFileName ( outName, (Char*)"(none)" );
  
  copyFileName ( progNameReally, argv[0] );
  progName = &progNameReally[0];
  for (tmp = &progNameReally[0]; *tmp != '\0'; tmp++) {
    if (*tmp == PATH_SEP) {
      progName = tmp + 1;
    }
  }
  
  
  /*-- Copy flags from env var BZIP2, and
   expand filename wildcards in arg list.
   --*/
  argList = NULL;
  addFlagsFromEnvVar ( &argList,  (Char*)"BZIP2" );
  addFlagsFromEnvVar ( &argList,  (Char*)"BZIP" );
  for (i = 1; i <= argc-1; i++) {
    APPEND_FILESPEC(argList, argv[i]);
  }
  
  
  /*-- Find the length of the longest filename --*/
  longestFileName = 7;
  numFileNames    = 0;
  decode          = True;
  for (aa = argList; aa != NULL; aa = aa->link) {
    if (ISFLAG("--")) {
      decode = False;
      continue;
    }
    if (aa->name[0] == '-' && decode) {
      continue;
    }
    numFileNames += 1;
    if (longestFileName < (Int32)strlen(aa->name) ) {
      longestFileName = (Int32)strlen(aa->name);
    }
  }
  
  
  /*-- Determine source modes; flag handling may change this too. --*/
  if (numFileNames == 0) {
    srcMode = SourceMode_StandardInput2StandardOutput;
  }
  else {
    srcMode = SourceMode_File2File;
  }
  
  
  /*-- Determine what to do (compress/uncompress/test/cat). --*/
  /*-- Note that subsequent flag handling may change this. --*/
  operationMode = OPERATION_MODE_COMPRESS;
  
  if ( (strstr ( progName, "unzip" ) != 0) ||
      (strstr ( progName, "UNZIP" ) != 0) ) {
    operationMode = OPERATION_MODE_DECOMPRESS;
  }
  
  if ( (strstr ( progName, "z2cat" ) != 0) ||
      (strstr ( progName, "Z2CAT" ) != 0) ||
      (strstr ( progName, "zcat" ) != 0)  ||
      (strstr ( progName, "ZCAT" ) != 0) )  {
    operationMode = OPERATION_MODE_DECOMPRESS;
    srcMode = (numFileNames == 0) ? SourceMode_StandardInput2StandardOutput : SourceMode_File2StandardOutput;
  }
  
  
  /*-- Look at the flags. --*/
  for (aa = argList; aa != NULL; aa = aa->link) {
    if (ISFLAG("--")) {
      break;
    }
    if (aa->name[0] == '-' && aa->name[1] != '-') {
      for (j = 1; aa->name[j] != '\0'; j++) {
        switch (aa->name[j]) {
          case 'c':
            srcMode = SourceMode_File2StandardOutput;
            break;
          case 'd':
            operationMode = OPERATION_MODE_DECOMPRESS;
            break;
          case 'z':
            operationMode = OPERATION_MODE_COMPRESS;
            break;
          case 'f':
            forceOverwrite = True;
            break;
          case 't':
            operationMode = OPERATION_MODE_TEST;
            break;
          case 'k':
            keepInputFiles = True;
            break;
          case 's':
            smallMode = True;
            break;
          case 'q':
            quiet = True;
            break;
          case '1':
            blockSize100k    = 1;
            break;
          case '2':
            blockSize100k    = 2;
            break;
          case '3':
            blockSize100k    = 3;
            break;
          case '4':
            blockSize100k    = 4;
            break;
          case '5':
            blockSize100k    = 5;
            break;
          case '6':
            blockSize100k    = 6;
            break;
          case '7':
            blockSize100k    = 7;
            break;
          case '8':
            blockSize100k    = 8;
            break;
          case '9':
            blockSize100k    = 9;
            break;
          case 'V':
          case 'L': license();
            exit ( 0 );
            break;
          case 'v': verbosity += 1; break;
          case 'h': usage ( progName );
            exit ( 0 );
            break;
          default:  fprintf ( stderr, "%s: Bad flag `%s'\n",
                             progName, aa->name );
            usage ( progName );
            exit ( 1 );
            break;
        }
      }
    }
  }
  
  /*-- And again ... --*/
  for (aa = argList; aa != NULL; aa = aa->link) {
    if (ISFLAG("--")) {
      break;
    }
    else {
      if (ISFLAG("--stdout")) {
        srcMode          = SourceMode_File2StandardOutput;
      }
      else {
        if (ISFLAG("--decompress")) {
          operationMode           = OPERATION_MODE_DECOMPRESS;
        }
        else {
          if (ISFLAG("--compress"))          {
            operationMode           = OPERATION_MODE_COMPRESS;
          }
          else {
            if (ISFLAG("--force"))             {
              forceOverwrite   = True;
            }
            else {
              if (ISFLAG("--test"))              {
                operationMode           = OPERATION_MODE_TEST;
              }
              else {
                if (ISFLAG("--keep"))              {
                  keepInputFiles   = True;
                }
                else {
                  if (ISFLAG("--small"))             {
                    smallMode        = True;
                  }
                  else {
                    if (ISFLAG("--quiet"))             {
                      quiet = True;
                    }
                    else {
                      if (ISFLAG("--version"))           {
                        license();
                        exit ( 0 );
                      }
                      else {
                        if (ISFLAG("--license"))           {
                          license();
                          exit ( 0 );
                        }
                        else {
                          if (ISFLAG("--exponential"))       {
                            workFactor = 1;
                          }
                          else {
                            if (ISFLAG("--repetitive-best"))   {
                              redundant(aa->name);
                            }
                            else {
                              if (ISFLAG("--repetitive-fast"))   {
                                redundant(aa->name);
                              }
                              else {
                                if (ISFLAG("--fast"))              {
                                  blockSize100k = 1;
                                }
                                else {
                                  if (ISFLAG("--best"))              {
                                    blockSize100k = 9;
                                  }
                                  else {
                                    if (ISFLAG("--verbose"))           {
                                      verbosity += 1;
                                    }
                                    else {
                                      if (ISFLAG("--help"))              {
                                        usage ( progName );
                                        exit ( 0 );
                                      }
                                      else {
                                        if (strncmp ( aa->name, "--", 2) == 0) {
                                          fprintf ( stderr, "%s: Bad flag `%s'\n", progName, aa->name );
                                          usage ( progName );
                                          exit ( 1 );
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  
  if (verbosity > 4) {
    verbosity = 4;
  }
  if (operationMode == OPERATION_MODE_COMPRESS && smallMode && blockSize100k > 2) {
    blockSize100k = 2;
  }
  
  if (operationMode == OPERATION_MODE_TEST && srcMode == SourceMode_File2StandardOutput) {
    fprintf ( stderr, "%s: -c and -t cannot be used together.\n", progName );
    exit ( 1 );
  }
  
  if (srcMode == SourceMode_File2StandardOutput && numFileNames == 0) {
    srcMode = SourceMode_StandardInput2StandardOutput;
  }
  
  if (operationMode != OPERATION_MODE_COMPRESS) {
    blockSize100k = 0;
  }
  
  if (srcMode == SourceMode_File2File) {
    signal (SIGINT,  mySignalCatcher);
    signal (SIGTERM, mySignalCatcher);
    signal (SIGHUP,  mySignalCatcher);
  }
  
  if (operationMode == OPERATION_MODE_COMPRESS) {
    if (srcMode == SourceMode_StandardInput2StandardOutput) {
      compress ( NULL );
    }
    else {
      decode = True;
      for (aa = argList; aa != NULL; aa = aa->link) {
        if (ISFLAG("--")) {
          decode = False;
          continue;
        }
        if (aa->name[0] == '-' && decode) {
          continue;
        }
        numFilesProcessed += 1;
        compress ( aa->name );
      }
    }
  }
  else {
    
    if (operationMode == OPERATION_MODE_DECOMPRESS) {
      unzFailsExist = False;
      if (srcMode == SourceMode_StandardInput2StandardOutput) {
        uncompress ( NULL );
      }
      else {
        decode = True;
        for (aa = argList; aa != NULL; aa = aa->link) {
          if (ISFLAG("--")) {
            decode = False;
            continue;
          }
          if (aa->name[0] == '-' && decode) {
            continue;
          }
          numFilesProcessed += 1;
          uncompress ( aa->name );
        }
      }
      if (unzFailsExist) {
        setExit(2);
        exit(exitValue);
      }
    }
    
    else {
      testFailsExist = False;
      if (srcMode == SourceMode_StandardInput2StandardOutput) {
        testf ( NULL );
      }
      else {
        decode = True;
        for (aa = argList; aa != NULL; aa = aa->link) {
          if (ISFLAG("--")) {
            decode = False;
            continue;
          }
          if (aa->name[0] == '-' && decode) {
            continue;
          }
          numFilesProcessed += 1;
          testf ( aa->name );
        }
      }
      if (testFailsExist) {
        if (!quiet) {
          fprintf ( stderr, "\n" "You can use the `bzip2recover' program to attempt to recover\n" "data from undamaged sections of corrupted files.\n\n" );
        }
        setExit(2);
        exit(exitValue);
      }
    }
  }
  
  /* Free the argument list memory to mollify leak detectors
   (eg) Purify, Checker.  Serves no other useful purpose.
   */
  aa = argList;
  while (aa != NULL) {
    Cell* aa2 = aa->link;
    if (aa->name != NULL) {
      free(aa->name);
    }
    free(aa);
    aa = aa2;
  }
  
  return exitValue;
}


/*-----------------------------------------------------------*/
/*--- end                                         bzip2.c ---*/
/*-----------------------------------------------------------*/
