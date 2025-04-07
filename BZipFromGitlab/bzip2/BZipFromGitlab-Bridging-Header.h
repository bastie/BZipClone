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

void printConfigErrorAndExitApplication (void);
void registerSignalHandlers4MemErrors (void);

Bool isCTypeSizesFits2BZip(void);
int cMain ( int argc, char *argv[] );

#endif /* BZipFromGitlab_Bridging_Header_h_h */
