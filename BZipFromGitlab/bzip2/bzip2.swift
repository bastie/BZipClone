//
//  bzip2 Swift struct
//
//  Created by Sͬeͥbͭaͭsͤtͬian on 06.04.25.
//

import Foundation

@main
struct BZip2 {

  public static func main() {
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
    

    cMain(CommandLine.argc, CommandLine.unsafeArgv)
  }
}

