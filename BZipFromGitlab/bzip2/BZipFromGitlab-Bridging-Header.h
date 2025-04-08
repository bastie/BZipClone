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

Int32   workFactor;
Int32   exitReturnCode;


void printConfigErrorAndExitApplication (void);
void registerSignalHandlers4MemErrors (void);

Bool isCTypeSizesFits2BZip(void);
int cMain ( int argc, char *argv[] );

#endif /* BZipFromGitlab_Bridging_Header_h_h */
