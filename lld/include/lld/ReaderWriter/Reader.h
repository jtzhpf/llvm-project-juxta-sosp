//===- lld/ReaderWriter/Reader.h - Abstract File Format Reading Interface -===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_READER_H
#define LLD_READER_WRITER_READER_H

#include "lld/Core/LLVM.h"
#include "lld/Core/Reference.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/YAMLTraits.h"
#include <functional>
#include <memory>
#include <vector>

using llvm::sys::fs::file_magic;

namespace llvm {
namespace yaml {
class IO;
}
}

namespace lld {
class ELFLinkingContext;
class File;
class LinkingContext;
class PECOFFLinkingContext;
class TargetHandlerBase;
class MachOLinkingContext;

/// \brief An abstract class for reading object files, library files, and
/// executable files.
///
/// Each file format (e.g. ELF, mach-o, PECOFF, native, etc) have a concrete
/// subclass of Reader.
class Reader {
public:
  virtual ~Reader() {}

  /// Sniffs the file to determine if this Reader can parse it.
  /// The method is called with:
  /// 1) the file_magic enumeration returned by identify_magic()
  /// 2) the file extension (e.g. ".obj")
  /// 3) the whole file content buffer if the above is not enough.
  virtual bool canParse(file_magic magic, StringRef fileExtension,
                        const MemoryBuffer &mb) const = 0;

  /// \brief Parse a supplied buffer (already filled with the contents of a
  /// file) and create a File object.
  ///
  /// The resulting File object may take ownership of the MemoryBuffer.
  virtual std::error_code
  parseFile(std::unique_ptr<MemoryBuffer> &mb, const class Registry &,
            std::vector<std::unique_ptr<File>> &result) const = 0;
};


/// \brief An abstract class for handling alternate yaml representations
/// of object files.
///
/// The YAML syntax allows "tags" which are used to specify the type of
/// the YAML node.  In lld, top level YAML documents can be in many YAML
/// representations (e.g mach-o encoded as yaml, etc).  A tag is used to
/// specify which representation is used in the following YAML document.
/// To work, there must be a YamlIOTaggedDocumentHandler registered that
/// handles each tag type.
class YamlIOTaggedDocumentHandler {
public:
  virtual ~YamlIOTaggedDocumentHandler();

  /// This method is called on each registered YamlIOTaggedDocumentHandler
  /// until one returns true.  If the subclass handles tag type !xyz, then
  /// this method should call io.mapTag("!xzy") to see if that is the current
  /// document type, and if so, process the rest of the document using
  /// YAML I/O, then convert the result into an lld::File* and return it.
  virtual bool handledDocTag(llvm::yaml::IO &io, const lld::File *&f) const = 0;
};


/// A registry to hold the list of currently registered Readers and
/// tables which map Reference kind values to strings.
/// The linker does not directly invoke Readers.  Instead, it registers
/// Readers based on it configuration and command line options, then calls
/// the Registry object to parse files.
class Registry {
public:
  Registry();

  /// Walk the list of registered Readers and find one that can parse the
  /// supplied file and parse it.
  std::error_code parseFile(std::unique_ptr<MemoryBuffer> &mb,
                            std::vector<std::unique_ptr<File>> &result) const;

  /// Walk the list of registered kind tables to convert a Reference Kind
  /// name to a value.
  bool referenceKindFromString(StringRef inputStr, Reference::KindNamespace &ns,
                               Reference::KindArch &a,
                               Reference::KindValue &value) const;

  /// Walk the list of registered kind tables to convert a Reference Kind
  /// value to a string.
  bool referenceKindToString(Reference::KindNamespace ns, Reference::KindArch a,
                             Reference::KindValue value, StringRef &) const;

  /// Walk the list of registered tag handlers and have the one that handles
  /// the current document type process the yaml into an lld::File*.
  bool handleTaggedDoc(llvm::yaml::IO &io, const lld::File *&file) const;

  // These methods are called to dynamically add support for various file
  // formats. The methods are also implemented in the appropriate lib*.a
  // library, so that the code for handling a format is only linked in, if this
  // method is used.  Any options that a Reader might need must be passed
  // as parameters to the addSupport*() method.
  void addSupportArchives(bool logLoading);
  void addSupportYamlFiles();
  void addSupportNativeObjects();
  void addSupportCOFFObjects(PECOFFLinkingContext &);
  void addSupportCOFFImportLibraries(PECOFFLinkingContext &);
  void addSupportMachOObjects(MachOLinkingContext &);
  void addSupportELFObjects(bool atomizeStrings, TargetHandlerBase *handler);
  void addSupportELFDynamicSharedObjects(bool useShlibUndefines,
                                         TargetHandlerBase *handler);

  /// To convert between kind values and names, the registry walks the list
  /// of registered kind tables. Each table is a zero terminated array of
  /// KindStrings elements.
  struct KindStrings {
    Reference::KindValue  value;
    StringRef             name;
  };

  /// A Reference Kind value is a tuple of <namespace, arch, value>.  All
  /// entries in a conversion table have the same <namespace, arch>.  The
  /// array then contains the value/name pairs.
  void addKindTable(Reference::KindNamespace ns, Reference::KindArch arch,
                    const KindStrings array[]);


private:
  struct KindEntry {
    Reference::KindNamespace  ns;
    Reference::KindArch       arch;
    const KindStrings        *array;
  };

  void add(std::unique_ptr<Reader>);
  void add(std::unique_ptr<YamlIOTaggedDocumentHandler>);

  std::vector<std::unique_ptr<Reader>>                       _readers;
  std::vector<std::unique_ptr<YamlIOTaggedDocumentHandler>>  _yamlHandlers;
  std::vector<KindEntry>                                     _kindEntries;
};

// Utilities for building a KindString table.  For instance:
//   static const Registry::KindStrings table[] = {
//      LLD_KIND_STRING_ENTRY(R_VAX_ADDR16),
//      LLD_KIND_STRING_ENTRY(R_VAX_DATA16),
//      LLD_KIND_STRING_END
//   };
#define LLD_KIND_STRING_ENTRY(name) { name, #name }
#define LLD_KIND_STRING_END         { 0,    "" }

} // end namespace lld

#endif
