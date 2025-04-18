//
//  BZipFromGitlab-Bridging-Header.h.h
//  bzip2
//
//  Created by Sebastian Ritter on 06.04.25.
//

#ifndef BZipFromGitlab_Bridging_Header_h
#define BZipFromGitlab_Bridging_Header_h

#include "bzlib.h"
#include "bzlib_private.h"

FILE    *outputHandleJustInCase;

/**
 Flag, ob die Eingabedateien beahlten werden sollen. Per default `false`
 */
extern Bool keepInputFiles;
/**
 Flag, ob wenig Speicher verbraucht werden soll. Erzwingt Blockgröße kleiner als 3
 */
extern Bool smallMode;
Bool    deleteOutputOnInterrupt;
/**
 Flag, ob die Datei(en) überschrieben werden sollen.
 */
extern Bool forceOverwrite;
/**
 Flag, ob es Fehler beim Testen der Datei(en) gab.
 */
Bool    testFailsExist;
/**
 Flag, ob es Fehler beim Dekomprimieren der Datei(en) gab.
 */
Bool    decompressFailsExist;
/**
  Flag, ob möglichst wenig Ausgaben erfolgen sollen
 */
extern Bool    quiet;
Int32   numFileNames, numFilesProcessed;
/**
 Flag, welches den Kompressionsfaktor angibt denn größere Blöcke bedeuten bessere Komprimierung
 */
extern Int32   blockSize100k;

extern Int32   operationMode;
/*-- operation modes --*/
const int OPERATION_MODE_COMPRESS = 1;
const int OPERATION_MODE_DECOMPRESS = 2;
const int OPERATION_MODE_TEST = 3;


Int32   workFactor;
Int32   exitReturnCode;


// Deklariere eine Variable für einen Zeiger auf Zeichenkette, die den Programmnamen ohne Pfadangaben enthalten soll
Char*   progName;

void printConfigErrorAndExitApplication (void);
void registerSignalHandlers4MemErrors (void);

Bool isCTypeSizesFits2BZip(void);
int cMain ( int argc, char *argv[] );

#endif /* BZipFromGitlab_Bridging_Header_h_h */
