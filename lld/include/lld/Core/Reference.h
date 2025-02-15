//===- Core/References.h - A Reference to Another Atom --------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_REFERENCES_H
#define LLD_CORE_REFERENCES_H

#include "lld/Core/LLVM.h"
#include "llvm/ADT/StringSwitch.h"

namespace lld {
class Atom;

///
/// The linker has a Graph Theory model of linking. An object file is seen
/// as a set of Atoms with References to other Atoms.  Each Atom is a node
/// and each Reference is an edge.
///
/// For example if a function contains a call site to "malloc" 40 bytes into
/// the Atom, then the function Atom will have a Reference of: offsetInAtom=40,
/// kind=callsite, target=malloc, addend=0.
///
/// Besides supporting traditional "relocations", References are also used
/// grouping atoms (group comdat), forcing layout (one atom must follow
/// another), marking data-in-code (jump tables or ARM constants), etc.
///
/// The "kind" of a reference is a tuple of <namespace, arch, value>.  This
/// enable us to re-use existing relocation types definded for various
/// file formats and architectures.  For instance, in ELF the relocation type 10
/// means R_X86_64_32 for x86_64, and R_386_GOTPC for i386. For PE/COFF
/// relocation 10 means IMAGE_REL_AMD64_SECTION.
///
/// References and atoms form a directed graph. The dead-stripping pass
/// traverses them starting from dead-strip root atoms to garbage collect
/// unreachable ones.
///
/// References of any kind are considered as directed edges. In addition to
/// that, references of some kind is considered as bidirected edges.
class Reference {
public:
  /// Which universe defines the kindValue().
  enum class KindNamespace {
    all     = 0,
    testing = 1,
    ELF     = 2,
    COFF    = 3,
    mach_o  = 4,
  };

  KindNamespace kindNamespace() const { return (KindNamespace)_kindNamespace; }
  void setKindNamespace(KindNamespace ns) { _kindNamespace = (uint8_t)ns; }

  // Which architecture the kind value is for.
  enum class KindArch {
    all     = 0,
    x86_64  = 1,
    x86     = 2,
    ARM     = 3,
    PowerPC = 4,
    Hexagon = 5,
    Mips    = 6,
    AArch64 = 7
  };

  KindArch kindArch() const { return (KindArch)_kindArch; }
  void setKindArch(KindArch a) { _kindArch = (uint8_t)a; }

  typedef uint16_t KindValue;

  KindValue kindValue() const { return _kindValue; }

  /// setKindValue() is needed because during linking, some optimizations may
  /// change the codegen and hence the reference kind.
  void setKindValue(KindValue value) {
    _kindValue = value;
  }

  /// KindValues used with KindNamespace::all and KindArch::all.
  enum {
    kindInGroup = 1,
    // kindLayoutAfter is treated as a bidirected edge by the dead-stripping
    // pass.
    kindLayoutAfter = 2,
    // kindLayoutBefore is currently used only by PECOFF port, and will
    // be removed soon. To enforce layout, use kindLayoutAfter instead.
    kindLayoutBefore = 3,
    // kindGroupChild is treated as a bidirected edge too.
    kindGroupChild = 4,
    kindAssociate = 5,
  };

  // A value to be added to the value of a target
  typedef int64_t Addend;

  /// If the reference is a fixup in the Atom, then this returns the
  /// byte offset into the Atom's content to do the fix up.
  virtual uint64_t offsetInAtom() const = 0;

  /// Returns the atom this reference refers to.
  virtual const Atom *target() const = 0;

  /// During linking, the linker may merge graphs which coalesces some nodes
  /// (i.e. Atoms).  To switch the target of a reference, this method is called.
  virtual void setTarget(const Atom *) = 0;

  /// Some relocations require a symbol and a value (e.g. foo + 4).
  virtual Addend addend() const = 0;

  /// During linking, some optimzations may change addend value.
  virtual void setAddend(Addend) = 0;

protected:
  /// Reference is an abstract base class.  Only subclasses can use constructor.
  Reference(KindNamespace ns, KindArch a, KindValue value)
      : _kindValue(value), _kindNamespace((uint8_t)ns), _kindArch((uint8_t)a) {}

  /// The memory for Reference objects is always managed by the owning File
  /// object.  Therefore, no one but the owning File object should call
  /// delete on an Reference.  In fact, some File objects may bulk allocate
  /// an array of References, so they cannot be individually deleted by anyone.
  virtual ~Reference() {}

  KindValue  _kindValue;
  uint8_t    _kindNamespace;
  uint8_t    _kindArch;
};

} // namespace lld

#endif // LLD_CORE_REFERENCES_H
