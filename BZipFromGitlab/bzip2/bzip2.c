
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
   in the file COPYING.
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
#include "bzlib_private.h"

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

/*--
  IntNative is your platform's `native' int size.
  Only here to avoid probs with 64-bit platforms.
--*/
typedef int IntNative;


/*---------------------------------------------------*/
/*--- Misc (file handling) data decls             ---*/
/*---------------------------------------------------*/

Bool    keepInputFiles, smallMode, deleteOutputOnInterrupt;
/**
 Flag, ob die Datei(en) überschrieben werden sollen.
 */
Bool    forceOverwrite;
/**
 Flag, ob es Fehler beim Testen der Datei(en) gab.
 */
Bool    testFailsExist;
/**
 Flag, ob es Fehler beim Dekomprimieren der Datei(en) gab.
 */
Bool    decompressFailsExist;
Bool    quiet;
Int32   numFileNames, numFilesProcessed, blockSize100k;
Int32   exitReturnCode;

/*-- source modes --*/
const int SourceMode_StandardInput2StandardOutput = 1;
const int SourceMode_File2StandardOutput = 2;
const int SourceMode_File2File = 3;

/*-- operation modes --*/
const int OPERATION_MODE_COMPRESS = 1;
const int OPERATION_MODE_DECOMPRESS = 2;
const int OPERATION_MODE_TEST = 3;

Int32   operationMode;
Int32   srcMode;

const int FILE_NAME_LEN = 1034;

Int32   longestFilename;
Char    inputFilename [FILE_NAME_LEN];
Char    outputFilename[FILE_NAME_LEN];
Char    tmporaryFilename[FILE_NAME_LEN];
// Deklariere eine Variable für einen Zeiger auf Zeichenkette, die den Programmnamen ohne Pfadangaben enthalten soll
Char*   progName;
// Deklariere ein Array von Zeichen in der Länge `FILE_NAME_LEN`
Char    progNameReally[FILE_NAME_LEN];
FILE    *outputHandleJustInCase;
Int32   workFactor;

void    printUnexpectedProgramStateAndExitApplication                 ( const Char* ) NORETURN;
void    handleIoErrorsAndExitApplication        ( void )        NORETURN;
void    printOutOfMemoryAndExitApplication           ( void )        NORETURN;
void    printConfigErrorAndExitApplication           ( void )        NORETURN;
void    crcError              ( void )        NORETURN;
void    cleanUpAndFailAndExitApplication        ( Int32 )       NORETURN;
void    compressedStreamEOF   ( void )        NORETURN;

void    copyFileName ( Char*, Char* );
void*   myMalloc     ( Int32 );
void    applySavedFileAttrToOutputFile ( IntNative fd );



/*---------------------------------------------------*/
/*--- An implementation of 64-bit ints.  Sigh.    ---*/
/*--- Roll on widespread deployment of ANSI C9X ! ---*/
/*---------------------------------------------------*/

typedef struct {
  UChar b[8];
} UInt64;


void uInt64_from_UInt32s ( UInt64* n, UInt32 lo32, UInt32 hi32 ) {
   n->b[7] = (UChar)((hi32 >> 24) & 0xFF);
   n->b[6] = (UChar)((hi32 >> 16) & 0xFF);
   n->b[5] = (UChar)((hi32 >> 8)  & 0xFF);
   n->b[4] = (UChar) (hi32        & 0xFF);
   n->b[3] = (UChar)((lo32 >> 24) & 0xFF);
   n->b[2] = (UChar)((lo32 >> 16) & 0xFF);
   n->b[1] = (UChar)((lo32 >> 8)  & 0xFF);
   n->b[0] = (UChar) (lo32        & 0xFF);
}


double uInt64_to_double ( UInt64* n ) {
   double base = 1.0;
   double sum  = 0.0;
   for (int i = 0; i < 8; i++) {
      sum  += base * (double)(n->b[i]);
      base *= 256.0;
   }
   return sum;
}


Bool uInt64_isZero ( UInt64* n ) {
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
Int32 uInt64_qrm10 ( UInt64* n ) {
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
void uInt64_toAscii ( char* outbuf, UInt64* n ) {
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
Bool myfeof ( FILE* f ) {
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


/**
 @brief Behandelt Fehler und beendet die Anwendung.
 
 Diese Funktion behandelt verschiedene Fehlerzustände, die während der bzip2-Komprimierung
 auftreten können, schließt den bzip2-Stream und beendet die Anwendung entsprechend.
 
 @param bzerror Ein Zeiger auf den Fehlercode der bzip2-Bibliothek.
 @param bzf Ein Zeiger auf die bzip2-Datei-Struktur (BZFILE).
 @param abandon Ein Flag, das angibt, ob der Stream abgebrochen werden soll (1) oder nicht (0).
 @param nbytes_in_lo32 Ein Zeiger auf die unteren 32 Bit der Anzahl der Eingabe-Bytes.
 @param nbytes_in_hi32 Ein Zeiger auf die oberen 32 Bit der Anzahl der Eingabe-Bytes.
 @param nbytes_out_lo32 Ein Zeiger auf die unteren 32 Bit der Anzahl der Ausgabe-Bytes.
 @param nbytes_out_hi32 Ein Zeiger auf die oberen 32 Bit der Anzahl der Ausgabe-Bytes.
 @param bzerr Der spezifische bzip2-Fehlercode.
 
 @discussion
 Die Funktion schließt zunächst den bzip2-Stream mit `BZ2_bzWriteClose64`, wobei die
 Anzahl der Eingabe- und Ausgabe-Bytes aktualisiert wird. Anschließend wird anhand
 des `bzerr`-Wertes entschieden, welche Fehlerbehandlungsroutine aufgerufen wird:
 
 - `BZ_CONFIG_ERROR`: Ruft `printConfigErrorAndExitApplication` auf.
 
 - `BZ_MEM_ERROR`: Ruft `printOutOfMemoryAndExitApplication` auf.
 
 - `BZ_IO_ERROR`: Ruft `handleIoErrorsAndExitApplication` auf.
 
 - Alle anderen Fehler: Ruft `printUnexpectedProgramStateAndExitApplication` mit einer
 allgemeinen Fehlermeldung auf.
 
 Nach der Fehlerbehandlung wird `printUnexpectedProgramStateAndExitApplication` mit
 einer Endmeldungen aufgerufen, die Funktion sollte jedoch nie diesen Punkt erreichen.
 
 @note
 Diese Funktion dient dazu, die Fehlerbehandlung in der bzip2-Komprimierungsanwendung
 zu zentralisieren und einheitlich zu gestalten.
 
 @warning
 Diese Funktion beendet die Anwendung.
 
 @code
 // Beispielaufruf (angenommen, bzerr ist BZ_IO_ERROR)
 int bzerror = BZ_OK;
 BZFILE *bzf = ...; // Initialisierte BZFILE-Struktur
 unsigned int nbytes_in_lo32 = 0, nbytes_in_hi32 = 0, nbytes_out_lo32 = 0, nbytes_out_hi32 = 0;
 int bzerr = BZ_IO_ERROR;
 
 handleErrorsAndExitApplication(&bzerror, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
 // Anwendung wird beendet
 @endcode
 
 @see BZ2_bzWriteClose64
 @see printConfigErrorAndExitApplication
 @see printOutOfMemoryAndExitApplication
 @see handleIoErrorsAndExitApplication
 @see printUnexpectedProgramStateAndExitApplication
 */
void handleErrorsAndExitApplication (int* bzerror, BZFILE* bzf, int abandon, unsigned int* nbytes_in_lo32, unsigned int* nbytes_in_hi32, unsigned int* nbytes_out_lo32, unsigned int* nbytes_out_hi32, int bzerr);

inline void handleErrorsAndExitApplication (int* bzerror, BZFILE* bzf, int abandon, unsigned int* nbytes_in_lo32, unsigned int* nbytes_in_hi32, unsigned int* nbytes_out_lo32, unsigned int* nbytes_out_hi32, int bzerr) {
  
  BZ2_bzWriteClose64 ( bzerror, bzf, 1,
                      nbytes_in_lo32, nbytes_in_hi32,
                      nbytes_out_lo32, nbytes_out_hi32 );
  switch (bzerr) {
    case BZ_CONFIG_ERROR:
      printConfigErrorAndExitApplication();
      break;
    case BZ_MEM_ERROR:
      printOutOfMemoryAndExitApplication ();
      break;
    case BZ_IO_ERROR:
      handleIoErrorsAndExitApplication();
      break;
    default:
      printUnexpectedProgramStateAndExitApplication ( "compress:unexpected error" );
  }
  
  printUnexpectedProgramStateAndExitApplication ( "compress:end" );
  /*notreached*/
}

void compressStream ( FILE *stream, FILE *zStream ) {
  BZFILE* bzf = NULL;
  UChar   buffer[BUFFER_SIZE];
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
  
  bzf = BZ2_bzWriteOpen ( &bzerr, zStream, blockSize100k, workFactor );
  if (bzerr != BZ_OK) {
    // führe die Fehlerbehandlung aus
    handleErrorsAndExitApplication (&bzerr_dummy, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
  }
  
  // Arbeite bis zum Ende aller Tage
  while (True) {
    // Wenn das Ende des Eingabestroms erreicht ist
    if (myfeof(stream)) {
      // Beende die Schleife
      break;
    }
    // Lese aus dem Eingabstrom `stream` maximal soviele Elemente wie in `bufferSize` definiert ist, wobei ein Elemen eine Anzahl von Bytes entspricht die `sizeof(UChar)` zurückgibt und speicher diese im Puffer `buffer`. Die Anzahl der gelesenen Zeichen speichere dabei in `countOfElementsInBuffer`.
    countOfElementsInBuffer = fread ( buffer, sizeof(UChar), BUFFER_SIZE, stream );
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
      handleErrorsAndExitApplication (&bzerr_dummy, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
    }
  }
  // MARK: hier erfolgt nicht nur das schreiben sondern auch die eigentlich Komprimierung :-(
  BZ2_bzWriteClose64 ( &bzerr, bzf, 0, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32 );
  if (bzerr != BZ_OK) {
    // führe die Fehlerbehandlung aus
    handleErrorsAndExitApplication (&bzerr_dummy, bzf, 1, &nbytes_in_lo32, &nbytes_in_hi32, &nbytes_out_lo32, &nbytes_out_hi32, bzerr);
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
   
  // beenden der Funktion (ohne Fehler)
  return;
}


/*---------------------------------------------*/
Bool uncompressStream ( FILE *zStream, FILE *stream ) {
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
    
    bzf = BZ2_bzReadOpen ( &bzerr, zStream, (int)smallMode, unused, nUnused );
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
      printUnexpectedProgramStateAndExitApplication ( "decompress:bzReadGetUnused" );
    }
    
    unusedTmp = (UChar*)unusedTmpV;
    for (i = 0; i < nUnused; i++) {
      unused[i] = unusedTmp[i];
    }
    
    BZ2_bzReadClose ( &bzerr, bzf );
    if (bzerr != BZ_OK) {
      printUnexpectedProgramStateAndExitApplication ( "decompress:bzReadGetUnused" );
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
      printConfigErrorAndExitApplication(); break;
    case BZ_IO_ERROR:
      handleIoErrorsAndExitApplication(); break;
    case BZ_DATA_ERROR:
      crcError();
    case BZ_MEM_ERROR:
      printOutOfMemoryAndExitApplication();
    case BZ_UNEXPECTED_EOF:
      compressedStreamEOF();
    case BZ_DATA_ERROR_MAGIC:
      if (zStream != stdin) fclose(zStream);
      if (stream != stdout) fclose(stream);
      if (streamNo == 1) {
        return False;
      } else {
        if (!quiet) {
          fprintf ( stderr, "\n%s: %s: trailing garbage after EOF ignored\n", progName, inputFilename );
        }
        return True;
      }
    default:
      printUnexpectedProgramStateAndExitApplication ( "decompress:unexpected error" );
  }
  
  printUnexpectedProgramStateAndExitApplication ( "decompress:end" );
  return True; /*notreached*/
}


/*---------------------------------------------*/
Bool testStream ( FILE *zStream ) {
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
    
    bzf = BZ2_bzReadOpen ( &bzerr, zStream, (int)smallMode, unused, nUnused );
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
      printUnexpectedProgramStateAndExitApplication ( "test:bzReadGetUnused" );
    }
    
    unusedTmp = (UChar*)unusedTmpV;
    for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];
    
    BZ2_bzReadClose ( &bzerr, bzf );
    if (bzerr != BZ_OK) {
      printUnexpectedProgramStateAndExitApplication ( "test:bzReadGetUnused" );
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
  
  return True;
  
errhandler:
  BZ2_bzReadClose ( &bzerr_dummy, bzf );
  switch (bzerr) {
    case BZ_CONFIG_ERROR:
      printConfigErrorAndExitApplication();
      break;
    case BZ_IO_ERROR:
      // rufe die Funktion `ioError` auf (welche später `exit(int)` aufruft
      handleIoErrorsAndExitApplication();
      break;
    case BZ_DATA_ERROR:
      fprintf ( stderr, "data integrity (CRC) error in data\n" );
      return False;
    case BZ_MEM_ERROR:
      printOutOfMemoryAndExitApplication();
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
      printUnexpectedProgramStateAndExitApplication ( "test:unexpected error" );
  }
  
  printUnexpectedProgramStateAndExitApplication ( "test:end" );
  return True; /*notreached*/
}


/*---------------------------------------------------*/
/*--- Error [non-] handling grunge                ---*/
/*---------------------------------------------------*/

/** (KI generiert nach funktionsinterner Kommentierung)
 @brief Setzt den Exit-Statuscode der Anwendung.
 
 Diese Funktion aktualisiert den globalen Exit-Statuscode der Anwendung,
 falls der neue Wert größer ist als der aktuell gespeicherte Wert.
 
 @param newExitReturnCode Der neue Exit-Statuscode, der gesetzt werden soll.
 
 @discussion
 Die Funktion vergleicht den übergebenen `newExitReturnCode` mit dem aktuell
 gespeicherten `exitReturnCode`. Wenn `newExitReturnCode` größer ist, wird er
 als neuer `exitReturnCode` gespeichert. Andernfalls bleibt der aktuelle
 `exitReturnCode` unverändert.
 
 @note
 Diese Funktion dient dazu, den höchsten aufgetretenen Fehlercode während
 der Ausführung der Anwendung zu speichern.
 
 @code
 setExit(1); // Setzt den Exit-Code auf 1, falls er bisher kleiner war
 setExit(2); // Setzt den Exit-Code auf 2, falls er bisher kleiner war
 @endcode
 
 @see exit
 */
void setExitReturnCode ( Int32 newExitReturnCode ) {
  // wenn der übergebene neue Wert für den Statuscode der Anwendung größer ist als der bisher gespeicherte Statuscode
  if (newExitReturnCode > exitReturnCode) {
    // speichere den neuen Statuscode
    exitReturnCode = newExitReturnCode;
  }
  else {
    // behalte den bisherigen Statuscode
  }
}


/*---------------------------------------------*/
void cadvise ( void ) {
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
void showFileNames ( void ) {
  if (!quiet) {
    fprintf ( stderr, "\tInput file = %s, output file = %s\n", inputFilename, outputFilename );
  }
}


/*---------------------------------------------*/
void cleanUpAndFailAndExitApplication ( Int32 ec ) {
  IntNative      retVal;
  struct stat statBuf;
  
  if ( srcMode == SourceMode_File2File && operationMode != OPERATION_MODE_TEST && deleteOutputOnInterrupt ) {
    
    /* Check whether input file still exists.  Delete output file
     only if input exists to avoid loss of data.  Joerg Prante, 5
     January 2002.  (JRS 06-Jan-2002: other changes in 1.0.2 mean
     this is less likely to happen.  But to be ultra-paranoid, we
     do the check anyway.)  */
    retVal = stat ( inputFilename, &statBuf );
    if (retVal == 0) {
      if (!quiet) {
        fprintf ( stderr, "%s: Deleting output file %s, if it exists.\n", progName, outputFilename );
      }
      if (outputHandleJustInCase != NULL) {
        fclose ( outputHandleJustInCase );
      }
      retVal = remove ( outputFilename );
      if (retVal != 0) {
        fprintf ( stderr, "%s: WARNING: deletion of output file " "(apparently) failed.\n", progName );
      }
    }
    else {
      fprintf ( stderr, "%s: WARNING: deletion of output file suppressed\n", progName );
      fprintf ( stderr, "%s:    since input file no longer exists.  Output file\n", progName );
      fprintf ( stderr, "%s:    `%s' may be incomplete.\n", progName, outputFilename );
      fprintf ( stderr, "%s:    I suggest doing an integrity test (bzip2 -tv)" " of it.\n", progName );
    }
  }
  
  if (!quiet && numFileNames > 0 && numFilesProcessed < numFileNames) {
    fprintf ( stderr, "%s: WARNING: some files have not been processed:\n" "%s:    %d specified on command line, %d not processed yet.\n\n", progName, progName, numFileNames, numFileNames - numFilesProcessed );
  }
  setExitReturnCode(ec);
  exit(exitReturnCode);
}


/*---------------------------------------------*/
void printUnexpectedProgramStateAndExitApplication ( const Char* s ) {
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
void crcError ( void ) {
  fprintf ( stderr, "\n%s: Data integrity error when decompressing.\n", progName );
  showFileNames();
  cadvise();
  cleanUpAndFailAndExitApplication( 2 );
}


/*---------------------------------------------*/
void compressedStreamEOF ( void ) {
  if (!quiet) {
    fprintf ( stderr, "\n%s: Compressed file ends unexpectedly;\n\t" "perhaps it is corrupted?  *Possible* reason follows.\n", progName );
    perror ( progName );
    showFileNames();
    cadvise();
  }
  cleanUpAndFailAndExitApplication( 2 );
}


/*---------------------------------------------*/
void handleIoErrorsAndExitApplication ( void ) {
  fprintf ( stderr, "\n%s: I/O or other error, bailing out.  " "Possible reason follows.\n", progName );
  perror ( progName );
  showFileNames();
  cleanUpAndFailAndExitApplication( 1 );
}


/*---------------------------------------------*/
void mySignalCatcher ( IntNative n, siginfo_t *info, void *context ) {
  fprintf ( stderr, "\n%s: Control-C or similar caught, quitting.\n", progName );
  cleanUpAndFailAndExitApplication(1);
}


/*---------------------------------------------*/
void mySIGSEGVorSIGBUScatcher ( IntNative n, siginfo_t *info, void *context ) {
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
  write ( STDERR_FILENO, inputFilename, strlen (inputFilename) );
  write ( STDERR_FILENO, "\n", 1 );
  msg = "\tOutput file = ";
  write ( STDERR_FILENO, msg, strlen (msg) );
  write ( STDERR_FILENO, outputFilename, strlen (outputFilename) );
  write ( STDERR_FILENO, "\n", 1 );
  
  /* Don't call cleanupAndFail. If we ended up here something went
   terribly wrong. Trying to clean up might fail spectacularly. */
  
  if (operationMode == OPERATION_MODE_COMPRESS) {
    setExitReturnCode(3);
  }
  else {
    setExitReturnCode(2);
  }
  _exit(exitReturnCode);
}


/*---------------------------------------------*/
void printOutOfMemoryAndExitApplication ( void ) {
  fprintf ( stderr, "\n%s: couldn't allocate enough memory\n", progName );
  showFileNames();
  cleanUpAndFailAndExitApplication(1);
}


/*---------------------------------------------*/
void printConfigErrorAndExitApplication ( void ) {
  fprintf ( stderr,
           "bzip2: I'm not configured correctly for this platform!\n"
           "\tI require Int32, Int16 and Char to have sizes\n"
           "\tof 4, 2 and 1 bytes to run properly, and they don't.\n"
           "\tProbably you can fix this by defining them correctly,\n"
           "\tand recompiling.  Bye!\n" );
  setExitReturnCode(3);
  exit(exitReturnCode);
}


/*---------------------------------------------------*/
/*--- Die Hauptsteuerungsmechanik                 ---*/
/*---------------------------------------------------*/

/* Alles ziemlich schlampig. Das Hauptproblem ist, dass
 Eingabedateien vor der Verwendung mehrmals mit stat()
 abgefragt werden. Das sollte bereinigt werden.
 */

/** (KI generierte Dokumentation war stark fehlerhaft!)
 @brief Gibt Leerzeichen auf `stderr`, um eine Zeichenkette mit eine bestimmte Länge auszugeben.
 
 Diese Funktion gibt Leerzeichen aus, um zusammen mit der Zeichenkette auf die Länge
 `longestFilename` zu kommen. Die Funktion gibt nichts zurück, sondern schreibt
 die Leerzeichen direkt auf `stderr`.
 
 @param s Ein Zeiger auf die Zeichenkette.
 
 @discussion
 Die Funktion überprüft zunächst, ob die Länge der Zeichenkette `s` bereits größer oder
 gleich `longestFilename` ist. Wenn dies der Fall ist, wird die Funktion sofort
 beendet. Andernfalls gibt die Funktion so viele Leerzeichen aus, dass,
 zusammen mit der übergebenen Zeichenkette, die Gesamtlänge
 `longestFilename` erreicht wird.
 
 @note
 `longestFilename` ist eine globale Variable, die die maximale Länge des Dateinamens
 speichert.
 
 @warning
 Die Leerzeichen werden direkt auf `stderr` geschrieben.
 
 @code
 char filename[] = "kurz.txt";
 longestFilename = 20;
 pad(filename);
 @endcode
 */
void pad ( Char *s ) {
  // wenn die maximale Länge des Dateinamen kleiner als die Länge der übergebenen Zeichenkette ist
  if ( longestFilename < strlen(s)) {
    // beende die Funktion
    return;
  }
  // wenn die maximale Länge des Dateinamen nicht kleiner als die Länge der übergebene Zeichenkette ist
  else {
    // ermittle den Unterschied zwischen der Länge der übergebene Zeichenkette und der maximalen Länge eines Dateinamen
    const Int32 rangeBetweenLengthOfLongestFilenameAndParameter = longestFilename - (Int32)strlen(s);
    // Führe für so oft wie der Unterschied zwischen der Länge der übergebene Zeichenkette und der maximalen Länge eines Dateinamen ist folgende Anweisungen durch:
    for (int i = 1; i <= rangeBetweenLengthOfLongestFilenameAndParameter; i++) {
      // Gebe auf dem Standard-Fehlerdatenstrom ein Leerzeichen aus
      fprintf ( stderr, " " );
    }
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
void copyFileName ( Char* to, Char* from ) {
  if ( strlen(from) > FILE_NAME_LEN-10 )  {
    fprintf ( stderr, "bzip2: file name\n`%s'\n" "is suspiciously (more than %d chars) long.\n" "Try using a reasonable file name instead.  Sorry! :-)\n", from, FILE_NAME_LEN-10 );
    setExitReturnCode(1);
    exit(exitReturnCode);
  }
  
  strncpy(to,from,FILE_NAME_LEN-10);
  to[FILE_NAME_LEN-10] = '\0';
}


/*---------------------------------------------*/
Bool fileExists ( Char* name ) {
  FILE *tmp   = fopen ( name, "rb" );
  Bool exists = (tmp != NULL);
  if (tmp != NULL) {
    fclose ( tmp );
  }
  return exists;
}

/**
 @brief Öffnet eine Ausgabedatei sicher und gibt einen FILE-Zeiger zurück.
 
 Diese Funktion öffnet eine Ausgabedatei mit dem angegebenen Namen und Modus,
 wobei sichergestellt wird, dass die Datei nicht bereits existiert.
 
 @param name Der Name der Ausgabedatei.
 @param mode Der Modus, in dem die Datei geöffnet werden soll (z.B. "w" für Schreiben).
 
 @return Ein FILE-Zeiger auf die geöffnete Datei oder NULL, wenn ein Fehler auftritt.
 
 @discussion
 Die Funktion verwendet `open`, um die Datei mit den Flags `O_WRONLY`, `O_CREAT` und `O_EXCL`
 zu öffnen. Dies stellt sicher, dass die Datei nur zum Schreiben geöffnet wird, erstellt wird,
 wenn sie nicht existiert, und einen Fehler zurückgibt, wenn sie bereits existiert.
 Die Dateiberechtigungen werden auf Schreiben und Lesen für den Eigentümer gesetzt.
 
 Wenn `open` erfolgreich ist, wird `fdopen` verwendet, um einen FILE-Zeiger zu erstellen.
 Wenn `fdopen` fehlschlägt, wird die mit `open` geöffnete Datei geschlossen.
 
 @note
 Diese Funktion dient dazu, race conditions beim Erstellen von Ausgabedateien zu vermeiden.
 
 @code
 FILE *outputFile = fopen_output_safely("output.txt", "w");
 if (outputFile != NULL) {
   // Schreibe Daten in die Datei
   fprintf(outputFile, "Hallo Welt!\n");
   fclose(outputFile);
 }
 else {
   perror("fopen_output_safely");
 }
 @endcode
 
 @see open
 @see fdopen
 @see close
 */
FILE* fopen_output_safely ( Char* name, const char* mode ) {
  // Öffne betriebssystemnah die Datei mit dem Namen `name` und kombinierten Modus write_only + create_if_not_exist + error_if_exist und setze die Permissions auf schreibbar für den Dateibesitzer und lesbar für den Dateibesitzer
  int fh = open (name, O_WRONLY|O_CREAT|O_EXCL, S_IWUSR|S_IRUSR);
  // Wenn open erfolgreich war
  if (fh > -1) {
    // erzeuge einen Zeiger auf die Datei mit dem Namen `name` und im übergebenen Modus `mode`
    FILE* fp = fdopen (fh, mode);
    // wenn es zu einem Fehler beim öffnen der Datei mit `fdopen` kam
    if (fp == NULL) {
      // schliesse die Datei die mit `open` geöffnet wurde
      close (fh);
    }
    // gebe den Zeiger auf die Datei mit dem Namen `name` zurück
    return fp;
  }
  // Wenn open nicht erfolgreich war
  else {
    // gebe `NULL` zurück um den Fehler zu signalisieren
    return NULL;
  }
}


/*---------------------------------------------*/
/*--
  if in doubt, return True
--*/
Bool notAStandardFile ( Char* name ) {
  IntNative      i;
  struct stat statBuf;
  
  i = lstat ( name, &statBuf );
  if (i != 0) {
    return True;
  }
  // Prüfe, ob die Datei ein reguläres Dateisystemobjekt ist, im Gegensatz zu Verzeichnissen, symbolischen Links, Gerätedateien oder anderen speziellen Dateitypen.
  // S_ISREG gibt einen Wert ungleich 0 (wahr) zurück, wenn die Datei ein reguläres Dateisystemobjekt ist, andernfalls 0 (falsch).
  // Wenn es eine reguläre Datei ist
  if (MY_S_ISREG(statBuf.st_mode)) {
    // gebe Falsch zurück
    return False;
  }
  // Wenn es keine reguläre Datei ist
  else {
    // Gebe Wahr zurück
    return True;
  }
}


/*---------------------------------------------*/
/*--
  rac 11/21/98 see if file has hard links to it
--*/
Int32 countHardLinks ( Char* name ) {
  IntNative      i;
  struct stat statBuf;
  
  i = lstat ( name, &statBuf );
  if (i != 0) {
    return 0;
  }
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
struct stat fileMetaInfo;

void saveInputFileMetaInfo ( Char *srcName ) {
   IntNative retVal;
   /* Note use of stat here, not lstat. */
   retVal = stat( srcName, &fileMetaInfo );
   ERROR_IF_NOT_ZERO ( retVal );
}


void applySavedTimeInfoToOutputFile ( Char *dstName ) {
   IntNative      retVal;
   struct utimbuf uTimBuf;

   uTimBuf.actime = fileMetaInfo.st_atime;
   uTimBuf.modtime = fileMetaInfo.st_mtime;

   retVal = utime ( dstName, &uTimBuf );
   ERROR_IF_NOT_ZERO ( retVal );
}

void applySavedFileAttrToOutputFile ( IntNative fd ) {
   IntNative retVal;

   retVal = fchmod ( fd, fileMetaInfo.st_mode );
   ERROR_IF_NOT_ZERO ( retVal );

   (void) fchown ( fd, fileMetaInfo.st_uid, fileMetaInfo.st_gid );
   /* chown() will in many cases return with EPERM, which can be safely ignored. */
}


/*---------------------------------------------*/
const int BZ_N_SUFFIX_PAIRS = 4;

const Char* compressedFilenameSuffix[BZ_N_SUFFIX_PAIRS] = { ".bz2", ".bz", ".tbz2", ".tbz" };
const Char* uncompressedFilenameSuffix[BZ_N_SUFFIX_PAIRS] = { "", "", ".tar", ".tar" };

Bool hasSuffix ( Char* s, const Char* suffix ) {
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

Bool mapSuffix ( Char* name, const Char* oldSuffix, const Char* newSuffix ) {
  if (!hasSuffix(name,oldSuffix)) {
    return False;
  }
  name[strlen(name)-strlen(oldSuffix)] = 0;
  strcat ( name, newSuffix );
  return True;
}


/*---------------------------------------------*/
void compress ( Char *name ) {
  FILE  *inStr;
  FILE  *outStr;
  Int32 n, i;
  struct stat statBuf;
  
  deleteOutputOnInterrupt = False;
  
  // Ist der Name nicht gesetzt und der SourceModus nicht StandardInput nach StandardOutput
  if (name == NULL && srcMode != SourceMode_StandardInput2StandardOutput) {
    // Gebe einen Fehler aus und beende das Programm
    printUnexpectedProgramStateAndExitApplication ( "compress: bad modes\n" );
  }
  
  switch (srcMode) {
    case SourceMode_StandardInput2StandardOutput:
      copyFileName ( inputFilename, (Char*)"(stdin)" );
      copyFileName ( outputFilename, (Char*)"(stdout)" );
      break;
    case SourceMode_File2File:
      copyFileName ( inputFilename, name );
      copyFileName ( outputFilename, name );
      strcat ( outputFilename, ".bz2" );
      break;
    case SourceMode_File2StandardOutput:
      copyFileName ( inputFilename, name );
      copyFileName ( outputFilename, (Char*)"(stdout)" );
      break;
  }
  
  if ( srcMode != SourceMode_StandardInput2StandardOutput && !fileExists ( inputFilename ) ) {
    fprintf ( stderr, "%s: Can't open input file %s: %s.\n", progName, inputFilename, strerror(errno) );
    setExitReturnCode(1);
    return;
  }
  for (i = 0; i < BZ_N_SUFFIX_PAIRS; i++) {
    if (hasSuffix(inputFilename, compressedFilenameSuffix[i])) {
      if (!quiet) {
        fprintf ( stderr, "%s: Input file %s already has %s suffix.\n", progName, inputFilename, compressedFilenameSuffix[i] );
      }
      setExitReturnCode(1);
      return;
    }
  }
  if ( srcMode == SourceMode_File2File || srcMode == SourceMode_File2StandardOutput ) {
    stat(inputFilename, &statBuf);
    if ( MY_S_ISDIR(statBuf.st_mode) ) {
      fprintf( stderr, "%s: Input file %s is a directory.\n", progName,inputFilename);
      setExitReturnCode(1);
      return;
    }
  }
  if ( srcMode == SourceMode_File2File && !forceOverwrite && notAStandardFile ( inputFilename )) {
    if (!quiet) {
      fprintf ( stderr, "%s: Input file %s is not a normal file.\n", progName, inputFilename );
    }
    setExitReturnCode(1);
    return;
  }
  if ( srcMode == SourceMode_File2File && fileExists ( outputFilename ) ) {
    if (forceOverwrite) {
      remove(outputFilename);
    } else {
      fprintf ( stderr, "%s: Output file %s already exists.\n", progName, outputFilename );
      setExitReturnCode(1);
      return;
    }
  }
  if ( srcMode == SourceMode_File2File && !forceOverwrite && (n=countHardLinks ( inputFilename )) > 0) {
    fprintf ( stderr, "%s: Input file %s has %d other link%s.\n", progName, inputFilename, n, n > 1 ? "s" : "" );
    setExitReturnCode(1);
    return;
  }
  
  if ( srcMode == SourceMode_File2File ) {
    /* Save the file's meta-info before we open it.  Doing it later
     means we mess up the access times. */
    saveInputFileMetaInfo ( inputFilename );
  }
  
  switch ( srcMode ) {
      
    case SourceMode_StandardInput2StandardOutput:
      inStr = stdin;
      outStr = stdout;
      if ( isatty ( fileno ( stdout ) ) ) {
        fprintf ( stderr, "%s: I won't write compressed data to a terminal.\n", progName );
        fprintf ( stderr, "%s: For help, type: `%s --help'.\n", progName, progName );
        setExitReturnCode(1);
        return;
      }
      break;
      
    case SourceMode_File2StandardOutput:
      inStr = fopen ( inputFilename, "rb" );
      outStr = stdout;
      if ( isatty ( fileno ( stdout ) ) ) {
        fprintf ( stderr, "%s: I won't write compressed data to a terminal.\n", progName );
        fprintf ( stderr, "%s: For help, type: `%s --help'.\n", progName, progName );
        if ( inStr != NULL ) {
          fclose ( inStr );
        }
        setExitReturnCode(1);
        return;
      }
      if ( inStr == NULL ) {
        fprintf ( stderr, "%s: Can't open input file %s: %s.\n", progName, inputFilename, strerror(errno) );
        setExitReturnCode(1);
        return;
      }
      break;
      
    case SourceMode_File2File:
      inStr = fopen ( inputFilename, "rb" );
      outStr = fopen_output_safely ( outputFilename, "wb" );
      if ( outStr == NULL) {
        fprintf ( stderr, "%s: Can't create output file %s: %s.\n",
                 progName, outputFilename, strerror(errno) );
        if ( inStr != NULL ) fclose ( inStr );
        setExitReturnCode(1);
        return;
      }
      if ( inStr == NULL ) {
        fprintf ( stderr, "%s: Can't open input file %s: %s.\n",
                 progName, inputFilename, strerror(errno) );
        if ( outStr != NULL ) fclose ( outStr );
        setExitReturnCode(1);
        return;
      }
      break;
      
    default:
      printUnexpectedProgramStateAndExitApplication ( "compress: bad srcMode" );
      break;
  }
    
  /*--- Now the input and output handles are sane.  Do the Biz. ---*/
  outputHandleJustInCase = outStr;
  deleteOutputOnInterrupt = True;
  compressStream ( inStr, outStr );
  outputHandleJustInCase = NULL;
  
  /*--- If there was an I/O error, we won't get here. ---*/
  if ( srcMode == SourceMode_File2File ) {
    applySavedTimeInfoToOutputFile ( outputFilename );
    deleteOutputOnInterrupt = False;
    if ( !keepInputFiles ) {
      IntNative retVal = remove ( inputFilename );
      ERROR_IF_NOT_ZERO ( retVal );
    }
  }
  
  deleteOutputOnInterrupt = False;
}


/*---------------------------------------------*/
void uncompress ( Char *name ) {
  FILE  *inStr;
  FILE  *outStr;
  Int32 n, i;
  Bool  magicNumberOK;
  Bool  cantGuess;
  struct stat statBuf;
  
  deleteOutputOnInterrupt = False;
  
  if (name == NULL && srcMode != SourceMode_StandardInput2StandardOutput) {
    printUnexpectedProgramStateAndExitApplication ( "uncompress: bad modes\n" );
  }
  
  cantGuess = False;
  switch (srcMode) {
    case SourceMode_StandardInput2StandardOutput:
      copyFileName ( inputFilename, (Char*)"(stdin)" );
      copyFileName ( outputFilename, (Char*)"(stdout)" );
      break;
    case SourceMode_File2File:
      copyFileName ( inputFilename, name );
      copyFileName ( outputFilename, name );
      for (i = 0; i < BZ_N_SUFFIX_PAIRS; i++) {
        if (mapSuffix(outputFilename,compressedFilenameSuffix[i],uncompressedFilenameSuffix[i])) {
          goto zzz;
        }
      }
      cantGuess = True;
      strcat ( outputFilename, ".out" );
      break;
    case SourceMode_File2StandardOutput:
      copyFileName ( inputFilename, name );
      copyFileName ( outputFilename, (Char*)"(stdout)" );
      break;
  }
  
zzz:
  if ( srcMode != SourceMode_StandardInput2StandardOutput && !fileExists ( inputFilename ) ) {
    fprintf ( stderr, "%s: Can't open input file %s: %s.\n", progName, inputFilename, strerror(errno) );
    setExitReturnCode(1);
    return;
  }
  if ( srcMode == SourceMode_File2File || srcMode == SourceMode_File2StandardOutput ) {
    stat(inputFilename, &statBuf);
    if ( MY_S_ISDIR(statBuf.st_mode) ) {
      fprintf( stderr, "%s: Input file %s is a directory.\n", progName,inputFilename);
      setExitReturnCode(1);
      return;
    }
  }
  if ( srcMode == SourceMode_File2File && !forceOverwrite && notAStandardFile ( inputFilename )) {
    if (!quiet) {
      fprintf ( stderr, "%s: Input file %s is not a normal file.\n", progName, inputFilename );
    }
    setExitReturnCode(1);
    return;
  }
  if ( /* srcMode == SM_F2F implied && */ cantGuess ) {
    if (!quiet) {
      fprintf ( stderr, "%s: Can't guess original name for %s -- using %s\n", progName, inputFilename, outputFilename );
    }
    /* just a warning, no return */
  }
  if ( srcMode == SourceMode_File2File && fileExists ( outputFilename ) ) {
    if (forceOverwrite) {
      remove(outputFilename);
    }
    else {
      fprintf ( stderr, "%s: Output file %s already exists.\n", progName, outputFilename );
      setExitReturnCode(1);
      return;
    }
  }
  if ( srcMode == SourceMode_File2File && !forceOverwrite &&
      (n=countHardLinks ( inputFilename ) ) > 0) {
    fprintf ( stderr, "%s: Input file %s has %d other link%s.\n", progName, inputFilename, n, n > 1 ? "s" : "" );
    setExitReturnCode(1);
    return;
  }
  
  if ( srcMode == SourceMode_File2File ) {
    /* Save the file's meta-info before we open it.  Doing it later
     means we mess up the access times. */
    saveInputFileMetaInfo ( inputFilename );
  }
  
  switch ( srcMode ) {
      
    case SourceMode_StandardInput2StandardOutput:
      inStr = stdin;
      outStr = stdout;
      if ( isatty ( fileno ( stdin ) ) ) {
        fprintf ( stderr, "%s: I won't read compressed data from a terminal.\n", progName );
        fprintf ( stderr, "%s: For help, type: `%s --help'.\n", progName, progName );
        setExitReturnCode(1);
        return;
      };
      break;
      
    case SourceMode_File2StandardOutput:
      inStr = fopen ( inputFilename, "rb" );
      outStr = stdout;
      if ( inStr == NULL ) {
        fprintf ( stderr, "%s: Can't open input file %s:%s.\n", progName, inputFilename, strerror(errno) );
        if ( inStr != NULL ) {
          fclose ( inStr );
        }
        setExitReturnCode(1);
        return;
      }
      break;
      
    case SourceMode_File2File:
      inStr = fopen ( inputFilename, "rb" );
      outStr = fopen_output_safely ( outputFilename, "wb" );
      if ( outStr == NULL) {
        fprintf ( stderr, "%s: Can't create output file %s: %s.\n", progName, outputFilename, strerror(errno) );
        if ( inStr != NULL ) {
          fclose ( inStr );
        }
        setExitReturnCode(1);
        return;
      }
      if ( inStr == NULL ) {
        fprintf ( stderr, "%s: Can't open input file %s: %s.\n", progName, inputFilename, strerror(errno) );
        if ( outStr != NULL ) {
          fclose ( outStr );
        }
        setExitReturnCode(1);
        return;
      }
      break;
      
    default:
      printUnexpectedProgramStateAndExitApplication ( "uncompress: bad srcMode" );
      break;
  }
  
  /*--- Now the input and output handles are sane.  Do the Biz. ---*/
  outputHandleJustInCase = outStr;
  deleteOutputOnInterrupt = True;
  magicNumberOK = uncompressStream ( inStr, outStr );
  outputHandleJustInCase = NULL;
  
  /*--- If there was an I/O error, we won't get here. ---*/
  if ( magicNumberOK ) {
    if ( srcMode == SourceMode_File2File ) {
      applySavedTimeInfoToOutputFile ( outputFilename );
      deleteOutputOnInterrupt = False;
      if ( !keepInputFiles ) {
        IntNative retVal = remove ( inputFilename );
        ERROR_IF_NOT_ZERO ( retVal );
      }
    }
  }
  else {
    decompressFailsExist = True;
    deleteOutputOnInterrupt = False;
    if ( srcMode == SourceMode_File2File ) {
      IntNative retVal = remove ( outputFilename );
      ERROR_IF_NOT_ZERO ( retVal );
    }
  }
  deleteOutputOnInterrupt = False;
  
  if ( !magicNumberOK ) {
    setExitReturnCode(2);
    fprintf ( stderr, "%s: %s is not a bzip2 file.\n", progName, inputFilename );
  }
}


/*---------------------------------------------*/
void testFile ( Char *name ) {
  FILE *inStr;
  Bool allOK;
  struct stat statBuf;
  
  deleteOutputOnInterrupt = False;
  
  if (name == NULL && srcMode != SourceMode_StandardInput2StandardOutput)
    printUnexpectedProgramStateAndExitApplication ( "testf: bad modes\n" );
  
  copyFileName ( outputFilename, (Char*)"(none)" );
  switch (srcMode) {
    case SourceMode_StandardInput2StandardOutput: copyFileName ( inputFilename, (Char*)"(stdin)" ); break;
    case SourceMode_File2File: copyFileName ( inputFilename, name ); break;
    case SourceMode_File2StandardOutput: copyFileName ( inputFilename, name ); break;
  }
  
  if ( srcMode != SourceMode_StandardInput2StandardOutput && !fileExists ( inputFilename ) ) {
    fprintf ( stderr, "%s: Can't open input %s: %s.\n", progName, inputFilename, strerror(errno) );
    setExitReturnCode(1);
    return;
  }
  if ( srcMode != SourceMode_StandardInput2StandardOutput ) {
    stat(inputFilename, &statBuf);
    if ( MY_S_ISDIR(statBuf.st_mode) ) {
      fprintf( stderr, "%s: Input file %s is a directory.\n", progName,inputFilename);
      setExitReturnCode(1);
      return;
    }
  }
  
  switch ( srcMode ) {
      
    case SourceMode_StandardInput2StandardOutput:
      if ( isatty ( fileno ( stdin ) ) ) {
        fprintf ( stderr, "%s: I won't read compressed data from a terminal.\n", progName );
        fprintf ( stderr, "%s: For help, type: `%s --help'.\n", progName, progName );
        setExitReturnCode(1);
        return;
      };
      inStr = stdin;
      break;
      
    case SourceMode_File2StandardOutput: case SourceMode_File2File:
      inStr = fopen ( inputFilename, "rb" );
      if ( inStr == NULL ) {
        fprintf ( stderr, "%s: Can't open input file %s:%s.\n", progName, inputFilename, strerror(errno) );
        setExitReturnCode(1);
        return;
      }
      break;
      
    default:
      printUnexpectedProgramStateAndExitApplication ( "testf: bad srcMode" );
      break;
  }
  
  /*--- Now the input handle is sane.  Do the Biz. ---*/
  outputHandleJustInCase = NULL;
  allOK = testStream ( inStr );
  
  if (!allOK) {
    testFailsExist = True;
  }
}


/** (KI generiert und angepasst)
 *
 * @brief Gibt die Lizenzinformationen auf der Standardausgabe aus.
 *
 * Diese Funktion gibt die Versionsnummer, das Copyright und die Lizenzbedingungen
 * auf der Standardausgabe aus. Die Ausgabe informiert den Benutzer
 * darüber, dass die Anwendung freie Software ist und unter den Bedingungen der im
 * LICENSE-Datei definierten Lizenz vertrieben wird.
 *
 * @note Diese Funktion gibt keine Werte zurück.
 */
void printLicenseOnStandardOutputStream ( void ) {
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


/**
 * @brief Gibt die Nutzungsinformationen für das Programm auf der Standardfehlerausgabe aus.
 *
 * Diese Funktion zeigt eine Hilfemeldung an, die die verfügbaren Befehlszeilenoptionen und deren
 * Verwendung beschreibt. Sie wird typischerweise aufgerufen, wenn der Benutzer die Option
 * `-h` oder `--help` angibt oder wenn ungültige Befehlszeilenargumente erkannt werden.
 *
 * @param fullProgName Der vollständige Name des Programms.
 * Dieser Name wird verwendet, um die den Programmnamen auszugeben.
 *
 * @details
 * Die Funktion gibt die Version des Programmes, die verfügbaren Optionen und deren Beschreibungen aus.
 * Sie erklärt auch, wie das Programm aufgerufen wird und wie es mit Standardein- und -ausgabe umgeht.
 *
 */
void printUsageInformationOnStandardErrorStream ( Char *fullProgName ) {
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
      "   -L --license        display software version & license\n"
      "   -V --version        display software version & license\n"
      "   -s --small          use less memory (at most 2500k)\n"
      "   -1 .. -9            set block size to 100k .. 900k\n"
      "   --fast              alias for -1\n"
      "   --best              alias for -9\n"
      "\n"
      "   If no file names are given, bzip2 compresses or decompresses\n"
      "   from standard input to standard output.  You can combine\n"
      "   short flags, so `-k -4' means the same as -k4 or -4k, etc.\n"
      "\n"
      ,

      BZ2_bzlibVersion(),
      fullProgName
   );
}


/*---------------------------------------------*/
void redundant ( Char* flag ) {
   fprintf ( stderr, "%s: %s is redundant in versions 0.9.5 and above\n", progName, flag );
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

/**
 * @brief Ein Element in einer verketteten Liste, die einen Namen und einen Zeiger auf die nächste Zelle enthält.
 *
 * Diese Struktur wird verwendet, um eine einfach verkettete Liste von Elementen zu erstellen,
 * wobei jedes Element eine Zeichenkette (Namen) und einen Zeiger auf das nächste Element enthält.
 */
typedef struct Element {
  /**
   * @brief Der Name, der in dieser Zelle gespeichert ist.
   *
   * Dies ist ein Zeiger auf eine Zeichenkette, die den Namen des Elements darstellt.
   */
  Char*        name;
  /**
   * @brief Ein Zeiger auf die nächste Zelle in der Liste.
   *
   * Wenn dies NULL ist, ist dies das letzte Element in der Liste.
   */
  struct Element* next;
} LinkedListElementOfStrings;


/*---------------------------------------------*/
void *myMalloc ( Int32 n ) {
  void* p;
  
  p = malloc ( (size_t)n );
  if (p == NULL) {
    printOutOfMemoryAndExitApplication ();
  }
  return p;
}


/*---------------------------------------------*/
LinkedListElementOfStrings *mkCell ( void ) {
   LinkedListElementOfStrings *c;

   c = (LinkedListElementOfStrings*) myMalloc ( sizeof ( LinkedListElementOfStrings ) );
   c->name = NULL;
   c->next = NULL;
   return c;
}


/*---------------------------------------------*/
LinkedListElementOfStrings *snocString ( LinkedListElementOfStrings *root, Char *name ) {
  if (root == NULL) {
    LinkedListElementOfStrings *tmp = mkCell();
    tmp->name = (Char*) myMalloc ( 5 + (unsigned int)strlen(name) );
    strcpy ( tmp->name, name );
    return tmp;
  }
  else {
    LinkedListElementOfStrings *tmp = root;
    while (tmp->next != NULL) tmp = tmp->next;
    tmp->next = snocString ( tmp->next, name );
    return root;
  }
}


/*---------------------------------------------*/
void addFlagsFromEnvVar ( LinkedListElementOfStrings** argList, Char* varName ) {
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
          tmporaryFilename[j] = p[j];
        }
        tmporaryFilename[k] = 0;
        APPEND_FLAG(*argList, tmporaryFilename);
      }
    }
  }
}


/*---------------------------------------------*/
Bool ISFLAG(LinkedListElementOfStrings *aa, Char* s) {
  return (strcmp(aa->name, (s))==0);
}

/**
 @brief Funktion, welche die eigentliche Verarbeitung anstößt.
 
 @param fileList
 Verkette Liste die die Kommandozeilenparameter enthält.
 */
void operate(LinkedListElementOfStrings *fileList) {
  LinkedListElementOfStrings *file;
  Bool operationFailed = False;
  void (*operationFunc)(char*) = NULL;
  
  // Setze die entsprechende Operationsfunktion basierend auf dem Modus auf den passenden Funktionspointer
  switch (operationMode) {
    case OPERATION_MODE_COMPRESS:
      operationFunc = compress;
      break;
    case OPERATION_MODE_DECOMPRESS:
      operationFunc = uncompress;
      break;
    case OPERATION_MODE_TEST:
      operationFunc = testFile;
      break;
    default:
      fprintf(stderr, "\n Unsupported operation. Only compress, decompress and test are supported.\n");
      setExitReturnCode(1);
      exit(exitReturnCode);
  }
  
  // Standard Input/Output Fall
  if (srcMode == SourceMode_StandardInput2StandardOutput) {
    operationFunc(NULL);
  }
  // Dateilisten Fall
  else {
    for (file = fileList; file != NULL; file = file->next) {
      numFilesProcessed += 1;
      operationFunc(file->name);
    }
  }
  
  // Kam es zu einem Fehler (bei decompress oder test?
  operationFailed = decompressFailsExist + testFailsExist; // MARK: c-specific, Bool is true if value>0
  
  // Fehlerbehandlung für Decompress und Test
  if (operationFailed) {
    if (operationMode == OPERATION_MODE_TEST && !quiet) {
      fprintf(stderr, "\nYou can use the `bzip2recover' program to attempt to recover\ndata from undamaged sections of corrupted files.\n\n");
    }
    setExitReturnCode(2);
    exit(exitReturnCode);
  }
}

/**
 @brief Liefert die Dateinamen aus der Liste aller Argumente beim Aufruf der Anwendung
 
 */
void getFilenames(LinkedListElementOfStrings* inputList, LinkedListElementOfStrings** fileList) {
  LinkedListElementOfStrings* lastFile = NULL;
  *fileList = NULL;
  
  int decode = 1;  // Flags werden zunächst decodiert (bis "--" auftaucht)
  
  for (LinkedListElementOfStrings* current = inputList;
       current != NULL;
       current = current->next) {
    
    if (ISFLAG(current, "--")) {
      decode = 0;  // Keine weitere Flag-Verarbeitung nach "--"
    }
    else {
      
      if (!(decode && current->name[0] == '-')) {
        // Füge zu Argument-Liste hinzu
        LinkedListElementOfStrings* newFile = malloc(sizeof(LinkedListElementOfStrings));
        newFile->name = current->name;
        newFile->next = NULL;
        
        if (*fileList == NULL) {
          *fileList = newFile;
        }
        else {
          lastFile->next = newFile;
        }
        lastFile = newFile;
      }
    }
  }
}

/**
 Gibt den Speicher in der Liste frei. Dabei kann übergeben werden, ob auch die Elemente in der Struktur freigeben werden sollen.
 */
void freeList (LinkedListElementOfStrings* list, Bool deepClean) {
  while (list != NULL) {
    LinkedListElementOfStrings* next = list->next;
    if (deepClean) {
      if (list->name != NULL) {
        free (list->name);
      }
    }
    free(list);
    list = next;
  }
}

/// @brief
/// Prüft die Größen der C-Datentypen
Bool isCTypeSizesFits2BZip(void) {
  return (
          sizeof(Int32) != 4 || sizeof(UInt32) != 4  ||
          sizeof(Int16) != 2 || sizeof(UInt16) != 2  ||
          sizeof(Char)  != 1 || sizeof(UChar)  != 1
  );
}


struct sigaction sa;

void registerSignalHandlers4MemErrors (void) {
  // Struct initialisieren
  memset(&sa, 0, sizeof(sa));
  // Flags setzen für erweiterte Signalinformationen
  sa.sa_flags = SA_SIGINFO;
  // Signalmaske leeren (keine zusätzlichen Signale blockieren)
  sigemptyset(&sa.sa_mask);
  
  
  // Handler-Funktion zuweisen
  sa.sa_sigaction = mySIGSEGVorSIGBUScatcher;
  // melde eine Call-Back Funktion an, wenn das Programm auf einen nicht zugewiesenen Speicher zugreifen will
  sigaction (SIGSEGV, &sa, NULL);
  // melde eine Call-Back Funktion an, wenn das Programm auf eine Variable zugreifen will die nicht korrekt im Speicher ausgerichtet ist
  sigaction (SIGBUS, &sa, NULL);
  
}

struct sigaction saWithFileCleanUp;
void registerSignalHandlers4File2FileOperation (void) {
  // Struct initialisieren
  memset(&saWithFileCleanUp, 0, sizeof(saWithFileCleanUp));
  // Flags setzen für erweiterte Signalinformationen
  saWithFileCleanUp.sa_flags = SA_SIGINFO;
  // Signalmaske leeren (keine zusätzlichen Signale blockieren)
  sigemptyset(&saWithFileCleanUp.sa_mask);
  
  // Handler-Funktion zuweisen
  saWithFileCleanUp.sa_sigaction = mySignalCatcher;
  
  // Ergänze die Fehlerbehandlung um SIGINT
  sigaction (SIGINT, &saWithFileCleanUp, NULL); // CTRL+C gedrückt
  
  // Ergänze die Fehlerbehandlung um SIGTERM
  sigaction (SIGTERM, &saWithFileCleanUp, NULL); // Prozess mit `kill` beendet
  
  // Ergänze die Fehlerbehandlung um SIGHUP
  sigaction (SIGHUP, &saWithFileCleanUp, NULL); // Terminalsession beendet
  
  // Ergänze die Fehlerbehandlung um SIGQUIT
  sigaction (SIGQUIT, &saWithFileCleanUp, NULL); // Prozess mit kill -3 oder CTRL+\ beendet
}

int main ( int argc, char *argv[] ) {
  Int32  i = 0;
  Int32  j = 0;
  Char   *tmp;
  LinkedListElementOfStrings   *argumentList;
  LinkedListElementOfStrings   *argument;
  /**
   Mit Hilfe dieser Variable wird sichergestellt, dass bei den Argumenten
   nur bis zum ersten Auftreten von "--" geprüft wird, ob mit einem
   einzelnen "-" ein Flag eingeleitet wird.
   Dadurch können auch Dateinamen die mit "-" beginnen bearbeitet
   werden.
   */
  Bool   decode;
  
  // Stelle sicher, dass die Größe der Typen für den Algorithmus stimmen.
  if (isCTypeSizesFits2BZip()) {
    printConfigErrorAndExitApplication();
  }
  
  // Initialisiere die Variablen mit Standardwerten
  outputHandleJustInCase  = NULL;
  smallMode               = False;
  keepInputFiles          = False;
  forceOverwrite          = False;
  quiet                   = False;
  blockSize100k           = 9;
  testFailsExist          = False;
  decompressFailsExist    = False;
  numFileNames            = 0;
  numFilesProcessed       = 0;
  workFactor              = 30;
  deleteOutputOnInterrupt = False;
  exitReturnCode          = 0;
  
  /*-- Set up signal handlers for mem access errors --*/
  registerSignalHandlers4MemErrors();

  // setze `inputFilename` auf "(none)"
  copyFileName ( inputFilename,  (Char*)"(none)" );
  // setze `outputFilename` auf "(none)"
  copyFileName ( outputFilename, (Char*)"(none)" );
  
  // setze `progNameReally` auf den Wert des ersten Argumentes. In C ist dies der Programmname.
  copyFileName ( progNameReally, argv[0] ); // MARK: c-specific
  // lasse den Zeiger `progName` auf die Adresse des ersten Elementes des Char-Array
  progName = &progNameReally[0];
  // ermittle nun den `basename` (shell) des Programmes indem du...
  // setze einen Zeiger auf das erste Zeichen im CharArray und solange nicht das Terminatorzeichen 0x00 auftritt gehe vor dem nachfolgenden Schleifendurchlauf mit dem Zeiger eine Adresse weiter
  for (tmp = &progNameReally[0]; *tmp != '\0'; tmp++) {
    // wenn das Zeichen am Zeiger identisch mit dem PATH_SEPARATOR ('/') ist
    if (*tmp == '/') { // MARK: os-specific
      // setze den Zeiger für den Programmnamen auf das Zeichen nach dem PATH_SEPARATOR
      progName = tmp + 1;
    }
  }
  
  
  /*-- Copy flags from env var BZIP2, and
   expand filename wildcards in arg list.
   --*/
  argumentList = NULL;
  addFlagsFromEnvVar ( &argumentList,  (Char*)"BZIP2" );
  addFlagsFromEnvVar ( &argumentList,  (Char*)"BZIP" );
  for (i = 1; i <= argc-1; i++) {
    APPEND_FILESPEC(argumentList, argv[i]);
  }
  
  
  /*-- Find the length of the longest filename --*/
  longestFilename = 7;
  numFileNames    = 0;
  decode          = True;
  // Für jedes Argument führe die Schleife aus
  for (argument = argumentList; argument != NULL; argument = argument->next) {
    // Wenn das Argument genau "--" ist
    if (ISFLAG(argument,"--")) {
      // setze das dekodieren auf falsch
      decode = False;
    }
    // sonst, also das Argument ist ungleich "--"
    else {
      // prüfe, ob das Zeichen mit einem "-" beginnt und dekodiert werden soll
      if (argument->name[0] == '-' && decode) {
        // mache nichts
      }
      // sonst, also das Argument ist ungleich "--" und entweder beginnt das Argument nicht mit "-" und/oder es soll nicht mehr dekodiert werden
      else {
        // erhöhe die Anzahl der Dateinamen um 1
        numFileNames += 1;
        // wenn der Wert in longestFilename < der Länge des Argumentes ist
        if (longestFilename < (Int32)strlen(argument->name) ) {
          // setze den Wert von longestFilname auf den Wert der der Länge des Argumentes entspricht
          longestFilename = (Int32)strlen(argument->name);
        }
        // sonst, also der Wert in longestFilename >= der Länge des Argumentes ist
        else {
          // mache nichts
        }
      }
    }
  }
  
  
  // Ermittle den SourceMode vor Auswertung der Argumente, da auch über Argumente der SourceMode gesetzt werden kann.
  // wenn die Anzahl der Dateien 0 ist
  if (numFileNames == 0) {
    // setze den SourceMode auf `SourceMode_StandardInput2StandardOutput`
    srcMode = SourceMode_StandardInput2StandardOutput;
  }
  // sonst, also die Anzahl der Datein != 0 ist
  else {
    // setze den SourceMode auf `SourceMode_File2File`
    srcMode = SourceMode_File2File;
  }
  
  // Setze den Standardwert für die zu erledigende Aufgabe auf Komprimierung
  operationMode = OPERATION_MODE_COMPRESS;

  // Werte jetzt die Argumente für die Posix-Kurzform aus:
  // Für jedes Argument führe die Schleife aus
  for (argument = argumentList; argument != NULL; argument = argument->next) {
    // Wenn das Argument "--" ist
    if (ISFLAG(argument,"--")) {
      // beende die Auswertung, so wie es POSIX vorsieht
      break;
    }
    else {
      // Wenn das Argument mit einem "-" beginnt, dem keine weiteren "-" folgend (also auch nicht mehr als zwei "-"
      if (argument->name[0] == '-' && argument->name[1] != '-') {
        // für jedes (j++) Zeichen nach dem Minus (j=1) solange bis die Zeichenkette zu Ende ist (j != '\0')
        for (j = 1; argument->name[j] != '\0'; j++) {
          // Prüfe den Wert des Zeichens (der hier einem Kurzargument nach POSIX entspricht)
          switch (argument->name[j]) {
            case 'c': // Wenn das Argument 'c' ist
              // setze den SourceMode auf `SourceMode_File2StandardOutput`
              srcMode = SourceMode_File2StandardOutput;
              break;
            case 'd': // Wenn das Argument 'd' ist
              // setze die zu erledigende Aufgaben auf Dekomrimierung
              operationMode = OPERATION_MODE_DECOMPRESS;
              break;
            case 'z': // Wenn das Argument 'z' ist
              // setze die zu erledigende Aufgabe auf Komprimierung
              operationMode = OPERATION_MODE_COMPRESS;
              break;
            case 'f': // Wenn das Argument 'f' ist
              // überschreibe eine evtl. vorhandene Zieldatei
              forceOverwrite = True;
              break;
            case 't': // Wenn das Argument 't' ist
              // setze die zu erledigende Aufgabe auf Testen der Datei
              operationMode = OPERATION_MODE_TEST;
              break;
            case 'k': // Wenn das Argument 'k'
              // behalte die ursprüngliche Datei
              keepInputFiles = True;
              break;
            case 's': // Wenn das Argument 's' ist
              // benutze wenig Speicherplatz
              smallMode = True;
              break;
            case 'q': // Wenn das Argument 'q'
              // gebe möglichst wenig Informationen auf der Kommandozeile aus
              quiet = True;
              break;
            case '1': // Wenn das Argument '1' ist
              // setze die Blockgrößenfaktor auf 1
              blockSize100k    = 1;
              break;
            case '2': // Wenn das Argument '2' ist
              // setze die Blockgrößenfaktor auf 2
              blockSize100k    = 2;
              break;
            case '3': // Wenn das Argument '3' ist
              // setze die Blockgrößenfaktor auf 3
              blockSize100k    = 3;
              break;
            case '4': // Wenn das Argument '4' ist
              // setze die Blockgrößenfaktor auf 4
              blockSize100k    = 4;
              break;
            case '5': // Wenn das Argument '5' ist
              // setze die Blockgrößenfaktor auf 5
              blockSize100k    = 5;
              break;
            case '6': // Wenn das Argument '6' ist
              // setze die Blockgrößenfaktor auf 6
              blockSize100k    = 6;
              break;
            case '7': // Wenn das Argument '7' ist
              // setze die Blockgrößenfaktor auf 7
              blockSize100k    = 7;
              break;
            case '8': // Wenn das Argument '8' ist
              // setze die Blockgrößenfaktor auf 8
              blockSize100k    = 8;
              break;
            case '9': // Wenn das Argument '9' ist
              // setze die Blockgrößenfaktor auf 9
              blockSize100k    = 9;
              break;
            case 'V': // Wenn das Argument 'V' oder ...
            case 'L': // ...wenn das Argument 'L' ist
              // zeige den Lizenztext an
              printLicenseOnStandardOutputStream();
              // beende die Anwendung mit dem Rückkehrcode fehlerfrei
              exit ( 0 );
              break;
            case 'h':  // Wenn das Argument 'h'
              // zeige die Information zur Nutzung des Programms an
              printUsageInformationOnStandardErrorStream ( progName ); // MARK: Logikfehler, schreiben auf stderr aber RC=0
              // beende die Anwendung mit dem Rückkehrcode fehlerfrei
              exit ( 0 );
              break;
            default: // Wenn ein anderes, also unbekanntes, Argument angegeben ist
              // gebe eine Fehlermeldung mit Programmnamen und Argument auf dem Standard-Fehler-Datenstrom aus
              fprintf ( stderr, "%s: Bad flag `%s'\n", progName, argument->name );
              // zeige die Information zur Nutzung des Programms an
              printUsageInformationOnStandardErrorStream ( progName );
              // beende die Anwendung mit dem Rückkehrcode Fehler Nr. 1
              exit ( 1 );
              break;
          }
        }
      }
    }
  }
  
  // Wiederhole die Auswertung der Argumente für die POSIX Langform (--argument)
  for (argument = argumentList; argument != NULL; argument = argument->next) {
    // Wenn das Argument "--" ist
    if (ISFLAG(argument,"--")) {
      // beende die Auswertung, so wie es POSIX vorsieht
      break;
    }
    else {
      if (ISFLAG(argument,"--stdout")) {
        srcMode = SourceMode_File2StandardOutput;
      }
      else {
        if (ISFLAG(argument,"--decompress")) {
          operationMode = OPERATION_MODE_DECOMPRESS;
        }
        else {
          if (ISFLAG(argument,"--compress"))          {
            operationMode = OPERATION_MODE_COMPRESS;
          }
          else {
            if (ISFLAG(argument,"--force"))             {
              forceOverwrite = True;
            }
            else {
              if (ISFLAG(argument,"--test"))              {
                operationMode = OPERATION_MODE_TEST;
              }
              else {
                if (ISFLAG(argument,"--keep"))              {
                  keepInputFiles = True;
                }
                else {
                  if (ISFLAG(argument,"--small"))             {
                    smallMode = True;
                  }
                  else {
                    if (ISFLAG(argument,"--quiet"))             {
                      quiet = True;
                    }
                    else {
                      if (ISFLAG(argument,"--version"))           {
                        printLicenseOnStandardOutputStream();
                        exit ( 0 );
                      }
                      else {
                        if (ISFLAG(argument,"--license"))           {
                          printLicenseOnStandardOutputStream();
                          exit ( 0 );
                        }
                        else {
                          if (ISFLAG(argument,"--exponential"))       {
                            workFactor = 1;
                          }
                          else {
                            if (ISFLAG(argument,"--repetitive-best"))   {
                              redundant(argument->name);
                            }
                            else {
                              if (ISFLAG(argument,"--repetitive-fast"))   {
                                redundant(argument->name);
                              }
                              else {
                                if (ISFLAG(argument,"--fast"))              {
                                  blockSize100k = 1;
                                }
                                else {
                                  if (ISFLAG(argument,"--best"))              {
                                    blockSize100k = 9;
                                  }
                                  else {
                                    if (ISFLAG(argument,"--help"))              {
                                      printUsageInformationOnStandardErrorStream ( progName );
                                      exit ( 0 );
                                    }
                                    else {
                                      if (strncmp ( argument->name, "--", 2) == 0) {
                                        fprintf ( stderr, "%s: Bad flag `%s'\n", progName, argument->name );
                                        printUsageInformationOnStandardErrorStream ( progName );
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
  
  // Prüfe, ob der smallMode gesetzt ist und komprimiert werden soll und die Blockgröße > 2 ist
  if (smallMode && operationMode == OPERATION_MODE_COMPRESS && blockSize100k > 2) {
    // setze die Blockgröße auf 2 (da ja smallMode = TRUE)
    blockSize100k = 2;
  }
  // Prüfe ob getestet werden soll und der Modus aber auf File2StandardOutput gesetzt ist
  if (operationMode == OPERATION_MODE_TEST && srcMode == SourceMode_File2StandardOutput) {
    // gebe eine Fehlermeldung aus
    fprintf ( stderr, "%s: -c and -t cannot be used together.\n", progName );
    // beende die Anwendung mit Fehlercode 0
    exit ( 1 );
  }
  // Prüfe ob der Modus auf File2StandardOutput gesetzt ist, die Anzahl der Dateinamen aber 0 ist
  if (srcMode == SourceMode_File2StandardOutput && numFileNames == 0) {
    // setze den Modus auf StandardInput2StandardOutput
    srcMode = SourceMode_StandardInput2StandardOutput;
  }
  // Wenn der Modus nicht komprimieren ist
  if (operationMode != OPERATION_MODE_COMPRESS) {
    // setze die Blockgröße auf 0
    blockSize100k = 0;
  }
  // Wenn der Modus auf File2File gesetzt ist
  if (srcMode == SourceMode_File2File) {
    registerSignalHandlers4File2FileOperation();
  }

  // Halte die Dateien in einer eigenen Liste
  LinkedListElementOfStrings* fileList = NULL;
  // Trenne die Argumente in Flags und Dateien
  getFilenames(argumentList, &fileList);
  // Führe die eigentliche Programmaufgabe (komprimieren, dekomprimieren oder testen aus)
  operate(fileList);

  // Räume auf
  freeList(argumentList, True);
  freeList(fileList, False);
  
  return exitReturnCode;
}


/*-----------------------------------------------------------*/
/*--- end                                         bzip2.c ---*/
/*-----------------------------------------------------------*/
