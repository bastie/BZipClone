//
//  bzip2 Swift struct
//
//  Created by Sͬeͥbͭaͭsͤtͬian on 06.04.25.
//

import Foundation
import ArgumentParser

@main
struct bzip2 : ParsableCommand {

// MARK: flags, options, arguments of program
  
  @Flag(name: [.customShort("k"), .customLong("keep")],
        help: "keep (don't delete) input files")
  var _keepInputFiles = false
  
  @Flag(name: [.customShort("f"), .customLong("force")],
        help: "overwrite existing output files")
  var _forceOverwrite = false
  
  @Flag(name: [.customShort("s"), .customLong("small")],
        help: "use less memory (at most 2500k)")
  var _smallMode = false
  
  @Flag(name: [.customShort("V"), .customLong("version")],
        help: "display software version & license")
  var _version = false
  @Flag(name: [.customShort("L"), .customLong("license")],
        help: "display software version & license")
  var _license = false
  
  @Flag(name: [.customShort("q"), .customLong("quiet")],
        help: "suppress noncritical error messages")
  var _quiet = false
  
  @Flag(name: [.customShort("c"), .customLong("stdout")],
        help: "output to standard out")
  var _stdout = false
  @Flag(name: [.customShort("t"), .customLong("test")],
        help: "test compressed file integrity")
  var _test = false
  @Flag(name: [.customShort("z"), .customLong("compress")],
        help: "force compression")
  var _compress = false
  @Flag(name: [.customShort("d"), .customLong("decompress")],
        help: "force decompression")
  var _decompress = false
  
  // alle restlichen Argumente
  @Argument(help: "input files")
  var fileArguments: [String] = []
    
  @Flag(exclusivity: .exclusive,
        help: ArgumentHelp(
    """
      set block size to 100k .. 900k
      --fast alias for -1
      --best alias for -9
    """))
  var _blockSize100k: CompressionLevel = .level9
  
  /// Startet nach Ausführung des Swift-Argument-Parser die weitere Anwendung
  func run() throws {
    // Stelle sicher, dass die Größe der Typen für den Algorithmus stimmen.
    // 1. Prüfe die C-Typen (über Bridging Header)
    if 0 != isCTypeSizesFits2BZip() {
      printConfigErrorAndExitApplication()
    }
    
    // 2. Prüfe die Swift-Typen
    if MemoryLayout<Int32>.size != 4 || MemoryLayout<UInt32>.size != 4 ||
        MemoryLayout<Int16>.size != 2 || MemoryLayout<UInt16>.size != 2 ||
        MemoryLayout<Int8>.size != 1 || MemoryLayout<UInt8>.size != 1 {
      printConfigErrorAndExitApplication()
    }
    
    /*-- Set up signal handlers for mem access errors --*/
    registerSignalHandlers4MemErrors();
    
    // Initialisiere die Variablen mit Standardwerten
    outputHandleJustInCase  = nil
    testFailsExist          = 0 // C-Bool = FALSE
    decompressFailsExist    = 0 // C-Bool = FALSE
    numFileNames            = 0
    numFilesProcessed       = 0
    workFactor              = 30
    deleteOutputOnInterrupt = 0 // C-Bool = FALSE
    exitReturnCode          = 0
    operationMode = 1 /* OPERATION_MODE_COMPRESS */
    

    progName = letCHandleMemoryFromSwiftString(
      CommandLine.arguments[0].split(separator: "/").last
    )
    
    // Prüfe, ob der smallMode gesetzt ist und komprimiert werden soll und die Blockgröße > 2 ist
    if _smallMode
        && operationMode == 1 /*nutze 1 statt OPERATION_MODE_COMPRESS solange bis der Operation Modus komplett auf Swift läuft - sonst müsste die const auch noch extern deklariert werden*/
        && blockSize100k > 2 {
      // setze die Blockgröße auf 2 (da ja smallMode = TRUE)
      blockSize100k = 2
      smallMode = True // da beim Dekomprimieren noch der smallMode direkt genutzt wird
    }

    cMain(CommandLine.argc, CommandLine.unsafeArgv)
  }
  
  /// Spezielle Auswertung der flags, options und arguments der Anwendung
  mutating func validate() throws {
    if _version || _license {
      printLicenseOnStandardOutputStream();
      Foundation.exit (0);
    }
    
    if _keepInputFiles {
      keepInputFiles = True
    }
    blockSize100k = Int32(_blockSize100k.rawValue)
    if _forceOverwrite {
      forceOverwrite = True
    }
    if _quiet {
      quiet = True
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
  func printLicenseOnStandardOutputStream () {
    let version = "1.1.0"
    print ("""
      bzip2, a block-sorting file compressor.  
      Version \(version).
         
         Copyright (C) 1996-2010 by Julian Seward.
         
         This program is free software; you can redistribute it and/or modify
         it under the terms set out in the LICENSE file, which is included
         in the bzip2-1.0.6 source distribution.
         
         This program is distributed in the hope that it will be useful,
         but WITHOUT ANY WARRANTY; without even the implied warranty of
         MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
         LICENSE file for more details.
      """
    )
  }
}

