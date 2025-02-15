//===- lib/ReaderWriter/MachO/MachONormalizedFileBinaryUtils.h ------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "MachONormalizedFile.h"
#include "lld/Core/Error.h"
#include "lld/Core/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MachO.h"
#include <system_error>

#ifndef LLD_READER_WRITER_MACHO_NORMALIZED_FILE_BINARY_UTILS_H
#define LLD_READER_WRITER_MACHO_NORMALIZED_FILE_BINARY_UTILS_H

namespace lld {
namespace mach_o {
namespace normalized {

using llvm::sys::getSwappedBytes;

 using llvm::support::ubig16_t;
 using llvm::support::ubig32_t;
 using llvm::support::ubig64_t;

 using llvm::support::ulittle16_t;
 using llvm::support::ulittle32_t;
 using llvm::support::ulittle64_t;

template<typename T>
static inline uint16_t read16(const T *loc, bool isBig) {
  assert((uint64_t)loc % llvm::alignOf<T>() == 0 &&
         "invalid pointer alignment");
  if (isBig)
    return *(const ubig16_t *)loc;
  return *(const ulittle16_t *)loc;
}

template<typename T>
static inline uint32_t read32(const T *loc, bool isBig) {
  assert((uint64_t)loc % llvm::alignOf<T>() == 0 &&
         "invalid pointer alignment");
  if (isBig)
    return *(const ubig32_t *)loc;
  return *(const ulittle32_t *)loc;
}

template<typename T>
static inline uint64_t read64(const T *loc, bool isBig) {
  assert((uint64_t)loc % llvm::alignOf<T>() == 0 &&
         "invalid pointer alignment");
  if (isBig)
    return *(const ubig64_t *)loc;
  return *(const ulittle64_t *)loc;
}

inline void write16(uint8_t *loc, uint16_t value, bool isBig) {
  if (isBig)
    *(ubig16_t *)loc = value;
  else
    *(ulittle16_t *)loc = value;
}

inline void write32(uint8_t *loc, uint32_t value, bool isBig) {
  if (isBig)
    *(ubig32_t *)loc = value;
  else
    *(ulittle32_t *)loc = value;
}

inline void write64(uint8_t *loc, uint64_t value, bool isBig) {
  if (isBig)
    *(ubig64_t *)loc = value;
  else
    *(ulittle64_t *)loc = value;
}

inline uint32_t
bitFieldExtract(uint32_t value, bool isBigEndianBigField, uint8_t firstBit,
                                                          uint8_t bitCount) {
  const uint32_t mask = ((1<<bitCount)-1);
  const uint8_t shift = isBigEndianBigField ? (32-firstBit-bitCount) : firstBit;
  return (value >> shift) & mask;
}

inline void
bitFieldSet(uint32_t &bits, bool isBigEndianBigField, uint32_t newBits,
                            uint8_t firstBit, uint8_t bitCount) {
  const uint32_t mask = ((1<<bitCount)-1);
  assert((newBits & mask) == newBits);
  const uint8_t shift = isBigEndianBigField ? (32-firstBit-bitCount) : firstBit;
  bits &= ~(mask << shift);
  bits |= (newBits << shift);
}

inline Relocation unpackRelocation(const llvm::MachO::any_relocation_info &r,
                                   bool isBigEndian) {
  uint32_t r0 = read32(&r.r_word0, isBigEndian);
  uint32_t r1 = read32(&r.r_word1, isBigEndian);

  Relocation result;
  if (r0 & llvm::MachO::R_SCATTERED) {
    // scattered relocation record always laid out like big endian bit field
    result.offset     = bitFieldExtract(r0, true, 8, 24);
    result.scattered  = true;
    result.type       = (RelocationInfoType)
                        bitFieldExtract(r0, true, 4, 4);
    result.length     = bitFieldExtract(r0, true, 2, 2);
    result.pcRel      = bitFieldExtract(r0, true, 1, 1);
    result.isExtern   = false;
    result.value      = r1;
    result.symbol     = 0;
  } else {
    result.offset     = r0;
    result.scattered  = false;
    result.type       = (RelocationInfoType)
                        bitFieldExtract(r1, isBigEndian, 28, 4);
    result.length     = bitFieldExtract(r1, isBigEndian, 25, 2);
    result.pcRel      = bitFieldExtract(r1, isBigEndian, 24, 1);
    result.isExtern   = bitFieldExtract(r1, isBigEndian, 27, 1);
    result.value      = 0;
    result.symbol     = bitFieldExtract(r1, isBigEndian, 0, 24);
  }
  return result;
}


inline llvm::MachO::any_relocation_info
packRelocation(const Relocation &r, bool swap, bool isBigEndian) {
  uint32_t r0 = 0;
  uint32_t r1 = 0;

  if (r.scattered) {
    r1 = r.value;
    bitFieldSet(r0, true, r.offset,    8, 24);
    bitFieldSet(r0, true, r.type,      4, 4);
    bitFieldSet(r0, true, r.length,    2, 2);
    bitFieldSet(r0, true, r.pcRel,     1, 1);
    bitFieldSet(r0, true, r.scattered, 0, 1); // R_SCATTERED
  } else {
    r0 = r.offset;
    bitFieldSet(r1, isBigEndian, r.type,     28, 4);
    bitFieldSet(r1, isBigEndian, r.isExtern, 27, 1);
    bitFieldSet(r1, isBigEndian, r.length,   25, 2);
    bitFieldSet(r1, isBigEndian, r.pcRel,    24, 1);
    bitFieldSet(r1, isBigEndian, r.symbol,   0,  24);
  }

  llvm::MachO::any_relocation_info result;
  result.r_word0 = swap ? getSwappedBytes(r0) : r0;
  result.r_word1 = swap ? getSwappedBytes(r1) : r1;
  return result;
}

inline StringRef getString16(const char s[16]) {
  StringRef x = s;
  if ( x.size() > 16 )
    return x.substr(0, 16);
  else
    return x;
}

inline void setString16(StringRef str, char s[16]) {
  memset(s, 0, 16);
  memcpy(s, str.begin(), (str.size() > 16) ? 16: str.size());
}

// Implemented in normalizedToAtoms() and used by normalizedFromAtoms() so
// that the same table can be used to map mach-o sections to and from
// DefinedAtom::ContentType.
void relocatableSectionInfoForContentType(DefinedAtom::ContentType atomType,
                                          StringRef &segmentName,
                                          StringRef &sectionName,
                                          SectionType &sectionType,
                                          SectionAttr &sectionAttrs);

} // namespace normalized
} // namespace mach_o
} // namespace lld

#endif // LLD_READER_WRITER_MACHO_NORMALIZED_FILE_BINARY_UTILS_H
