//
//  helper.swift
//  bzip2
//
//  Created by Sebastian Ritter on 08.04.25.
//

func letCHandleMemoryFromSwiftString(_ swiftString: String) -> UnsafeMutablePointer<CChar>? {
  return UnsafeMutablePointer(mutating: strdup(swiftString.cString(using: .utf8)))
}

func letCHandleMemoryFromSwiftString(_ substring: Substring?) -> UnsafeMutablePointer<CChar>? {
  return substring.flatMap {
    strdup(String($0).cString(using: .utf8))
  }.map {
    UnsafeMutablePointer(mutating: $0)
  }
}
