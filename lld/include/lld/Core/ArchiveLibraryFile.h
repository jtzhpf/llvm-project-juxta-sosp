//===- Core/ArchiveLibraryFile.h - Models static library ------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_ARCHIVE_LIBRARY_FILE_H
#define LLD_CORE_ARCHIVE_LIBRARY_FILE_H

#include "lld/Core/File.h"
#include <set>

namespace lld {

///
/// The ArchiveLibraryFile subclass of File is used to represent unix
/// static library archives.  These libraries provide no atoms to the
/// initial set of atoms linked.  Instead, when the Resolver will query
/// ArchiveLibraryFile instances for specific symbols names using the
/// find() method.  If the archive contains an object file which has a
/// DefinedAtom whose scope is not translationUnit, then that entire
/// object file File is returned.
///
class ArchiveLibraryFile : public File {
public:
  static inline bool classof(const File *f) {
    return f->kind() == kindArchiveLibrary;
  }

  /// Check if any member of the archive contains an Atom with the
  /// specified name and return the File object for that member, or nullptr.
  virtual const File *find(StringRef name, bool dataSymbolOnly) const = 0;

  virtual std::error_code
  parseAllMembers(std::vector<std::unique_ptr<File>> &result) const = 0;

  /// Returns a set of all defined symbols in the archive, i.e. all
  /// resolvable symbol using this file.
  virtual std::set<StringRef> getDefinedSymbols() const {
    return std::set<StringRef>();
  }

protected:
  /// only subclasses of ArchiveLibraryFile can be instantiated
  ArchiveLibraryFile(StringRef path) : File(path, kindArchiveLibrary) {}
};

} // namespace lld

#endif // LLD_CORE_ARCHIVE_LIBRARY_FILE_H
