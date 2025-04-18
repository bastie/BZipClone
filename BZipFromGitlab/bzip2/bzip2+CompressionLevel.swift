//
//  bzip2+CompressionLevel.swift
//  bzip2
//
//  Created by Sebastian Ritter on 18.04.25.
//

import ArgumentParser

/// Der Kompressionslevel als abgeschlossenes enum
enum CompressionLevel: Int, EnumerableFlag {
  case level9 = 9
  case level8 = 8
  case level7 = 7
  case level6 = 6
  case level5 = 5
  case level4 = 4
  case level3 = 3
  case level2 = 2
  case level1 = 1
  
  static func name(for value: Self) -> NameSpecification {
    switch value {
    case .level9: return [.customShort("9"), .customLong("best")]
    case .level8: return [.customShort("8")]
    case .level7: return [.customShort("7")]
    case .level6: return [.customShort("6")]
    case .level5: return [.customShort("5")]
    case .level4: return [.customShort("4")]
    case .level3: return [.customShort("3")]
    case .level2: return [.customShort("2")]
    case .level1: return [.customShort("1"), .customLong("fast")]
      //default: return .shortAndLong
    }
  }
}
