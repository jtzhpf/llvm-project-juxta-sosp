//===-- DataExtractor.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DataExtractor_h_
#define liblldb_DataExtractor_h_
#if defined (__cplusplus)


#include "lldb/lldb-private.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class DataExtractor DataExtractor.h "lldb/Core/DataExtractor.h"
/// @brief An data extractor class.
///
/// DataExtractor is a class that can extract data (swapping if needed)
/// from a data buffer. The data buffer can be caller owned, or can be
/// shared data that can be shared between multiple DataExtractor
/// instances. Multiple DataExtractor objects can share the same data,
/// yet extract values in different address sizes and byte order modes.
/// Each object can have a unique position in the shared data and extract
/// data from different offsets.
///
/// @see DataBuffer
//----------------------------------------------------------------------
class DataExtractor
{
public:
    //------------------------------------------------------------------
    /// @typedef DataExtractor::Type
    /// @brief Type enumerations used in the dump routines.
    /// @see DataExtractor::Dump()
    /// @see DataExtractor::DumpRawHexBytes()
    //------------------------------------------------------------------
    typedef enum
    {
        TypeUInt8,      ///< Format output as unsigned 8 bit integers
        TypeChar,       ///< Format output as characters
        TypeUInt16,     ///< Format output as unsigned 16 bit integers
        TypeUInt32,     ///< Format output as unsigned 32 bit integers
        TypeUInt64,     ///< Format output as unsigned 64 bit integers
        TypePointer,    ///< Format output as pointers
        TypeULEB128,    ///< Format output as ULEB128 numbers
        TypeSLEB128     ///< Format output as SLEB128 numbers
    } Type;

    static void
    DumpHexBytes (Stream *s, 
                  const void *src, 
                  size_t src_len, 
                  uint32_t bytes_per_line,
                  lldb::addr_t base_addr); // Pass LLDB_INVALID_ADDRESS to not show address at start of line
    //------------------------------------------------------------------
    /// Default constructor.
    ///
    /// Initialize all members to a default empty state.
    //------------------------------------------------------------------
    DataExtractor ();

    //------------------------------------------------------------------
    /// Construct with a buffer that is owned by the caller.
    ///
    /// This constructor allows us to use data that is owned by the
    /// caller. The data must stay around as long as this object is
    /// valid.
    ///
    /// @param[in] data
    ///     A pointer to caller owned data.
    ///
    /// @param[in] data_length
    ///     The length in bytes of \a data.
    ///
    /// @param[in] byte_order
    ///     A byte order of the data that we are extracting from.
    ///
    /// @param[in] addr_size
    ///     A new address byte size value.
    ///
    /// @param[in] target_byte_size
    ///     A size of a target byte in 8-bit host bytes
    //------------------------------------------------------------------
    DataExtractor (const void* data, lldb::offset_t data_length, lldb::ByteOrder byte_order, uint32_t addr_size, uint32_t target_byte_size = 1);

    //------------------------------------------------------------------
    /// Construct with shared data.
    ///
    /// Copies the data shared pointer which adds a reference to the
    /// contained in \a data_sp. The shared data reference is reference
    /// counted to ensure the data lives as long as anyone still has a
    /// valid shared pointer to the data in \a data_sp.
    ///
    /// @param[in] data_sp
    ///     A shared pointer to data.
    ///
    /// @param[in] byte_order
    ///     A byte order of the data that we are extracting from.
    ///
    /// @param[in] addr_size
    ///     A new address byte size value.
    ///
    /// @param[in] target_byte_size
    ///     A size of a target byte in 8-bit host bytes
    //------------------------------------------------------------------
    DataExtractor (const lldb::DataBufferSP& data_sp, lldb::ByteOrder byte_order, uint32_t addr_size, uint32_t target_byte_size = 1);

    //------------------------------------------------------------------
    /// Construct with a subset of \a data.
    ///
    /// Initialize this object with a subset of the data bytes in \a
    /// data. If \a data contains shared data, then a reference to the
    /// shared data will be added to ensure the shared data stays around
    /// as long as any objects have references to the shared data. The
    /// byte order value and the address size settings are copied from \a
    /// data. If \a offset is not a valid offset in \a data, then no
    /// reference to the shared data will be added. If there are not
    /// \a length bytes available in \a data starting at \a offset,
    /// the length will be truncated to contain as many bytes as
    /// possible.
    ///
    /// @param[in] data
    ///     Another DataExtractor object that contains data.
    ///
    /// @param[in] offset
    ///     The offset into \a data at which the subset starts.
    ///
    /// @param[in] length
    ///     The length in bytes of the subset of data.
    ///
    /// @param[in] target_byte_size
    ///     A size of a target byte in 8-bit host bytes
    //------------------------------------------------------------------
    DataExtractor (const DataExtractor& data, lldb::offset_t offset, lldb::offset_t length, uint32_t target_byte_size = 1);

    DataExtractor (const DataExtractor& rhs);
    //------------------------------------------------------------------
    /// Assignment operator.
    ///
    /// Copies all data, byte order and address size settings from \a rhs into
    /// this object. If \a rhs contains shared data, a reference to that
    /// shared data will be added.
    ///
    /// @param[in] rhs
    ///     Another DataExtractor object to copy.
    ///
    /// @return
    ///     A const reference to this object.
    //------------------------------------------------------------------
    const DataExtractor&
    operator= (const DataExtractor& rhs);

    //------------------------------------------------------------------
    /// Destructor
    ///
    /// If this object contains a valid shared data reference, the
    /// reference count on the data will be decremented, and if zero,
    /// the data will be freed.
    //------------------------------------------------------------------
    ~DataExtractor ();

    //------------------------------------------------------------------
    /// Clears the object state.
    ///
    /// Clears the object contents back to a default invalid state, and
    /// release any references to shared data that this object may
    /// contain.
    //------------------------------------------------------------------
    void
    Clear ();

    //------------------------------------------------------------------
    /// Dumps the binary data as \a type objects to stream \a s (or to
    /// Log() if \a s is NULL) starting \a offset bytes into the data
    /// and stopping after dumping \a length bytes. The offset into the
    /// data is displayed at the beginning of each line and can be
    /// offset by base address \a base_addr. \a num_per_line objects
    /// will be displayed on each line.
    ///
    /// @param[in] s
    ///     The stream to dump the output to. If NULL the output will
    ///     be dumped to Log().
    ///
    /// @param[in] offset
    ///     The offset into the data at which to start dumping.
    ///
    /// @param[in] length
    ///     The number of bytes to dump.
    ///
    /// @param[in] base_addr
    ///     The base address that gets added to the offset displayed on
    ///     each line.
    ///
    /// @param[in] num_per_line
    ///     The number of \a type objects to display on each line.
    ///
    /// @param[in] type
    ///     The type of objects to use when dumping data from this
    ///     object. See DataExtractor::Type.
    ///
    /// @param[in] type_format
    ///     The optional format to use for the \a type objects. If this
    ///     is NULL, the default format for the \a type will be used.
    ///
    /// @return
    ///     The offset at which dumping ended.
    //------------------------------------------------------------------
    lldb::offset_t
    PutToLog (Log *log,
              lldb::offset_t offset,
              lldb::offset_t length,
              uint64_t base_addr,
              uint32_t num_per_line,
              Type type,
              const char *type_format = NULL) const;

    //------------------------------------------------------------------
    /// Dumps \a item_count objects into the stream \a s.
    ///
    /// Dumps \a item_count objects using \a item_format, each of which
    /// are \a item_byte_size bytes long starting at offset \a offset
    /// bytes into the contained data, into the stream \a s. \a
    /// num_per_line objects will be dumped on each line before a new
    /// line will be output. If \a base_addr is a valid address, then
    /// each new line of output will be prededed by the address value
    /// plus appropriate offset, and a colon and space. Bitfield values
    /// can be dumped by calling this function multiple times with the
    /// same start offset, format and size, yet differing \a
    /// item_bit_size and \a item_bit_offset values.
    ///
    /// @param[in] s
    ///     The stream to dump the output to. This value can not be NULL.
    ///
    /// @param[in] offset
    ///     The offset into the data at which to start dumping.
    ///
    /// @param[in] item_format
    ///     The format to use when dumping each item.
    ///
    /// @param[in] item_byte_size
    ///     The byte size of each item.
    ///
    /// @param[in] item_count
    ///     The number of items to dump.
    ///
    /// @param[in] num_per_line
    ///     The number of items to display on each line.
    ///
    /// @param[in] base_addr
    ///     The base address that gets added to the offset displayed on
    ///     each line if the value is valid. Is \a base_addr is
    ///     LLDB_INVALID_ADDRESS then no address values will be prepended
    ///     to any lines.
    ///
    /// @param[in] item_bit_size
    ///     If the value to display is a bitfield, this value should
    ///     be the number of bits that the bitfield item has within the
    ///     item's byte size value. This function will need to be called
    ///     multiple times with identical \a offset and \a item_byte_size
    ///     values in order to display multiple bitfield values that
    ///     exist within the same integer value. If the items being
    ///     displayed are not bitfields, this value should be zero.
    ///
    /// @param[in] item_bit_offset
    ///     If the value to display is a bitfield, this value should
    ///     be the offset in bits, or shift right amount, that the
    ///     bitfield item occupies within the item's byte size value.
    ///     This function will need to be called multiple times with
    ///     identical \a offset and \a item_byte_size values in order
    ///     to display multiple bitfield values that exist within the
    ///     same integer value. If the items being displayed are not
    ///     bitfields, this value should be zero.
    ///
    /// @return
    ///     The offset at which dumping ended.
    //------------------------------------------------------------------
    lldb::offset_t
    Dump (Stream *s,
          lldb::offset_t offset,
          lldb::Format item_format,
          size_t item_byte_size,
          size_t item_count,
          size_t num_per_line,
          uint64_t base_addr,
          uint32_t item_bit_size,
          uint32_t item_bit_offset,
          ExecutionContextScope *exe_scope = NULL) const;

    //------------------------------------------------------------------
    /// Dump a UUID value at \a offset.
    ///
    /// Dump a UUID starting at \a offset bytes into this object's data.
    /// If the stream \a s is NULL, the output will be sent to Log().
    ///
    /// @param[in] s
    ///     The stream to dump the output to. If NULL the output will
    ///     be dumped to Log().
    ///
    /// @param[in] offset
    ///     The offset into the data at which to extract and dump a
    ///     UUID value.
    //------------------------------------------------------------------
    void
    DumpUUID (Stream *s, lldb::offset_t offset) const;

    //------------------------------------------------------------------
    /// Extract an arbitrary number of bytes in the specified byte
    /// order.
    ///
    /// Attemps to extract \a length bytes starting at \a offset bytes
    /// into this data in the requested byte order (\a dst_byte_order)
    /// and place the results in \a dst. \a dst must be at least \a
    /// length bytes long.
    ///
    /// @param[in] offset
    ///     The offset in bytes into the contained data at which to
    ///     start extracting.
    ///
    /// @param[in] length
    ///     The number of bytes to extract.
    ///
    /// @param[in] dst_byte_order
    ///     A byte order of the data that we want when the value in
    ///     copied to \a dst.
    ///
    /// @param[out] dst
    ///     The buffer that will receive the extracted value if there
    ///     are enough bytes available in the current data.
    ///
    /// @return
    ///     The number of bytes that were extracted which will be \a
    ///     length when the value is successfully extracted, or zero
    ///     if there aren't enough bytes at the specified offset.
    //------------------------------------------------------------------
    size_t
    ExtractBytes (lldb::offset_t offset, lldb::offset_t length, lldb::ByteOrder dst_byte_order, void *dst) const;

    //------------------------------------------------------------------
    /// Extract an address from \a *offset_ptr.
    ///
    /// Extract a single address from the data and update the offset
    /// pointed to by \a offset_ptr. The size of the extracted address
    /// comes from the \a m_addr_size member variable and should be
    /// set correctly prior to extracting any address values.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted address value.
    //------------------------------------------------------------------
    uint64_t
    GetAddress (lldb::offset_t *offset_ptr) const;
    
    uint64_t
    GetAddress_unchecked (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Get the current address size.
    ///
    /// Return the size in bytes of any address values this object will
    /// extract.
    ///
    /// @return
    ///     The size in bytes of address values that will be extracted.
    //------------------------------------------------------------------
    uint32_t
    GetAddressByteSize () const
    {
        return m_addr_size;
    }

    //------------------------------------------------------------------
    /// Get the number of bytes contained in this object.
    ///
    /// @return
    ///     The total number of bytes of data this object refers to.
    //------------------------------------------------------------------
    uint64_t
    GetByteSize () const
    {
        return m_end - m_start;
    }

    //------------------------------------------------------------------
    /// Extract a C string from \a *offset_ptr.
    ///
    /// Returns a pointer to a C String from the data at the offset
    /// pointed to by \a offset_ptr. A variable length NULL terminated C
    /// string will be extracted and the \a offset_ptr will be
    /// updated with the offset of the byte that follows the NULL
    /// terminator byte.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     A pointer to the C string value in the data. If the offset
    ///     pointed to by \a offset_ptr is out of bounds, or if the
    ///     offset plus the length of the C string is out of bounds,
    ///     NULL will be returned.
    //------------------------------------------------------------------
    const char *
    GetCStr (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Extract a C string from \a *offset_ptr with field size \a len.
    ///
    /// Returns a pointer to a C String from the data at the offset
    /// pointed to by \a offset_ptr, with a field length of \a len.
    /// A NULL terminated C string will be extracted and the \a offset_ptr
    /// will be updated with the offset of the byte that follows the fixed
    /// length field.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     A pointer to the C string value in the data. If the offset
    ///     pointed to by \a offset_ptr is out of bounds, or if the
    ///     offset plus the length of the field is out of bounds, or if
    ///     the field does not contain a NULL terminator byte, NULL will
    ///     be returned.
    const char *
    GetCStr (lldb::offset_t *offset_ptr, lldb::offset_t len) const;

    //------------------------------------------------------------------
    /// Extract \a length bytes from \a *offset_ptr.
    ///
    /// Returns a pointer to a bytes in this object's data at the offset
    /// pointed to by \a offset_ptr. If \a length is zero or too large,
    /// then the offset pointed to by \a offset_ptr will not be updated
    /// and NULL will be returned.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] length
    ///     The optional length of a string to extract. If the value is
    ///     zero, a NULL terminated C string will be extracted.
    ///
    /// @return
    ///     A pointer to the bytes in this object's data if the offset
    ///     and length are valid, or NULL otherwise.
    //------------------------------------------------------------------
    const void*
    GetData (lldb::offset_t *offset_ptr, lldb::offset_t length) const
    {
        const uint8_t *ptr = PeekData (*offset_ptr, length);
        if (ptr)
            *offset_ptr += length;
        return ptr;
    }
    
    //------------------------------------------------------------------
    /// Copy \a length bytes from \a *offset, without swapping bytes.
    ///
    /// @param[in] offset
    ///     The offset into this data from which to start copying
    ///
    /// @param[in] length
    ///     The length of the data to copy from this object
    ///
    /// @param[out] dst
    ///     The buffer to place the output data.
    ///
    /// @return
    ///     Returns the number of bytes that were copied, or zero if 
    ///     anything goes wrong.
    //------------------------------------------------------------------
    lldb::offset_t
    CopyData (lldb::offset_t offset,
              lldb::offset_t length,
              void *dst) const;

    //------------------------------------------------------------------
    /// Copy \a dst_len bytes from \a *offset_ptr and ensure the copied
    /// data is treated as a value that can be swapped to match the
    /// specified byte order.
    ///
    /// For values that are larger than the supported integer sizes,
    /// this function can be used to extract data in a specified byte
    /// order. It can also be used to copy a smaller integer value from
    /// to a larger value. The extra bytes left over will be padded
    /// correctly according to the byte order of this object and the
    /// \a dst_byte_order. This can be very handy when say copying a 
    /// partial data value into a register.
    ///
    /// @param[in] src_offset
    ///     The offset into this data from which to start copying an
    ///     endian entity
    ///
    /// @param[in] src_len
    ///     The length of the endian data to copy from this object
    ///     into the \a dst object
    ///
    /// @param[out] dst
    ///     The buffer where to place the endian data. The data might 
    ///     need to be byte swapped (and appropriately padded with 
    ///     zeroes if \a src_len != \a dst_len) if \a dst_byte_order 
    ///     does not match the byte order in this object.
    ///
    /// @param[in] dst_len
    ///     The length number of bytes that the endian value will 
    ///     occupy is \a dst.
    ///
    /// @param[in] byte_order
    ///     The byte order that the endian value should be in the \a dst
    ///     buffer.
    ///
    /// @return
    ///     Returns the number of bytes that were copied, or zero if 
    ///     anything goes wrong.
    //------------------------------------------------------------------
    lldb::offset_t
    CopyByteOrderedData (lldb::offset_t src_offset,
                         lldb::offset_t src_len,
                         void *dst, 
                         lldb::offset_t dst_len,
                         lldb::ByteOrder dst_byte_order) const;

    //------------------------------------------------------------------
    /// Get the data end pointer.
    ///
    /// @return
    ///     Returns a pointer to the next byte contained in this
    ///     object's data, or NULL of there is no data in this object.
    //------------------------------------------------------------------
    const uint8_t *
    GetDataEnd () const
    {
        return m_end;
    }

    //------------------------------------------------------------------
    /// Get the shared data offset.
    ///
    /// Get the offset of the first byte of data in the shared data (if
    /// any).
    ///
    /// @return
    ///     If this object contains shared data, this function returns
    ///     the offset in bytes into that shared data, zero otherwise.
    //------------------------------------------------------------------
    size_t
    GetSharedDataOffset () const;

    //------------------------------------------------------------------
    /// Get a the data start pointer.
    ///
    /// @return
    ///     Returns a pointer to the first byte contained in this
    ///     object's data, or NULL of there is no data in this object.
    //------------------------------------------------------------------
    const uint8_t *
    GetDataStart () const
    {
        return m_start;
    }


    //------------------------------------------------------------------
    /// Extract a float from \a *offset_ptr.
    ///
    /// Extract a single float value.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The floating value that was extracted, or zero on failure.
    //------------------------------------------------------------------
    float
    GetFloat (lldb::offset_t *offset_ptr) const;

    double
    GetDouble (lldb::offset_t *offset_ptr) const;

    long double
    GetLongDouble (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Extract a GNU encoded pointer value from \a *offset_ptr.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] eh_ptr_enc
    ///     The GNU pointer encoding type.
    ///
    /// @param[in] pc_rel_addr
    ///     The PC relative address to use when the encoding is
    ///     \c DW_GNU_EH_PE_pcrel.
    ///
    /// @param[in] text_addr
    ///     The text (code) relative address to use when the encoding is
    ///     \c DW_GNU_EH_PE_textrel.
    ///
    /// @param[in] data_addr
    ///     The data relative address to use when the encoding is
    ///     \c DW_GNU_EH_PE_datarel.
    ///
    /// @return
    ///     The extracted GNU encoded pointer value.
    //------------------------------------------------------------------
    uint64_t
    GetGNUEHPointer (lldb::offset_t *offset_ptr,
                     uint32_t eh_ptr_enc,
                     lldb::addr_t pc_rel_addr,
                     lldb::addr_t text_addr,
                     lldb::addr_t data_addr);

    //------------------------------------------------------------------
    /// Extract an integer of size \a byte_size from \a *offset_ptr.
    ///
    /// Extract a single integer value and update the offset pointed to
    /// by \a offset_ptr. The size of the extracted integer is specified
    /// by the \a byte_size argument. \a byte_size should have a value
    /// >= 1 and <= 4 since the return value is only 32 bits wide. Any
    /// \a byte_size values less than 1 or greater than 4 will result in
    /// nothing being extracted, and zero being returned.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] byte_size
    ///     The size in byte of the integer to extract.
    ///
    /// @return
    ///     The integer value that was extracted, or zero on failure.
    //------------------------------------------------------------------
    uint32_t
    GetMaxU32 (lldb::offset_t *offset_ptr, size_t byte_size) const;

    //------------------------------------------------------------------
    /// Extract an unsigned integer of size \a byte_size from \a
    /// *offset_ptr.
    ///
    /// Extract a single unsigned integer value and update the offset
    /// pointed to by \a offset_ptr. The size of the extracted integer
    /// is specified by the \a byte_size argument. \a byte_size should
    /// have a value greater than or equal to one and less than or equal
    /// to eight since the return value is 64 bits wide. Any
    /// \a byte_size values less than 1 or greater than 8 will result in
    /// nothing being extracted, and zero being returned.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] byte_size
    ///     The size in byte of the integer to extract.
    ///
    /// @return
    ///     The unsigned integer value that was extracted, or zero on
    ///     failure.
    //------------------------------------------------------------------
    uint64_t
    GetMaxU64 (lldb::offset_t *offset_ptr, size_t byte_size) const;

    uint64_t
    GetMaxU64_unchecked (lldb::offset_t *offset_ptr, size_t byte_size) const;

    //------------------------------------------------------------------
    /// Extract an signed integer of size \a byte_size from \a *offset_ptr.
    ///
    /// Extract a single signed integer value (sign extending if required)
    /// and update the offset pointed to by \a offset_ptr. The size of
    /// the extracted integer is specified by the \a byte_size argument.
    /// \a byte_size should have a value greater than or equal to one
    /// and less than or equal to eight since the return value is 64
    /// bits wide. Any \a byte_size values less than 1 or greater than
    /// 8 will result in nothing being extracted, and zero being returned.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] byte_size
    ///     The size in byte of the integer to extract.
    ///
    /// @return
    ///     The sign extended signed integer value that was extracted,
    ///     or zero on failure.
    //------------------------------------------------------------------
    int64_t
    GetMaxS64 (lldb::offset_t *offset_ptr, size_t size) const;

    //------------------------------------------------------------------
    /// Extract an unsigned integer of size \a byte_size from \a
    /// *offset_ptr, then extract the bitfield from this value if
    /// \a bitfield_bit_size is non-zero.
    ///
    /// Extract a single unsigned integer value and update the offset
    /// pointed to by \a offset_ptr. The size of the extracted integer
    /// is specified by the \a byte_size argument. \a byte_size should
    /// have a value greater than or equal to one and less than or equal
    /// to 8 since the return value is 64 bits wide. Any
    /// \a byte_size values less than 1 or greater than 8 will result in
    /// nothing being extracted, and zero being returned.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] byte_size
    ///     The size in byte of the integer to extract.
    ///
    /// @param[in] bitfield_bit_size
    ///     The size in bits of the bitfield value to extract, or zero
    ///     to just extract the entire integer value.
    ///
    /// @param[in] bitfield_bit_offset
    ///     The bit offset of the bitfield value in the extracted
    ///     integer (the number of bits to shift the integer to the
    ///     right).
    ///
    /// @return
    ///     The unsigned bitfield integer value that was extracted, or
    ///     zero on failure.
    //------------------------------------------------------------------
    uint64_t
    GetMaxU64Bitfield (lldb::offset_t *offset_ptr,
                       size_t size,
                       uint32_t bitfield_bit_size,
                       uint32_t bitfield_bit_offset) const;

    //------------------------------------------------------------------
    /// Extract an signed integer of size \a byte_size from \a
    /// *offset_ptr, then extract and signe extend the bitfield from
    /// this value if \a bitfield_bit_size is non-zero.
    ///
    /// Extract a single signed integer value (sign extending if required)
    /// and update the offset pointed to by \a offset_ptr. The size of
    /// the extracted integer is specified by the \a byte_size argument.
    /// \a byte_size should have a value greater than or equal to one
    /// and less than or equal to eight since the return value is 64
    /// bits wide. Any \a byte_size values less than 1 or greater than
    /// 8 will result in nothing being extracted, and zero being returned.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[in] byte_size
    ///     The size in bytes of the integer to extract.
    ///
    /// @param[in] bitfield_bit_size
    ///     The size in bits of the bitfield value to extract, or zero
    ///     to just extract the entire integer value.
    ///
    /// @param[in] bitfield_bit_offset
    ///     The bit offset of the bitfield value in the extracted
    ///     integer (the number of bits to shift the integer to the
    ///     right).
    ///
    /// @return
    ///     The signed bitfield integer value that was extracted, or
    ///     zero on failure.
    //------------------------------------------------------------------
    int64_t
    GetMaxS64Bitfield (lldb::offset_t *offset_ptr,
                       size_t size,
                       uint32_t bitfield_bit_size,
                       uint32_t bitfield_bit_offset) const;

    //------------------------------------------------------------------
    /// Extract an pointer from \a *offset_ptr.
    ///
    /// Extract a single pointer from the data and update the offset
    /// pointed to by \a offset_ptr. The size of the extracted pointer
    /// comes from the \a m_addr_size member variable and should be
    /// set correctly prior to extracting any pointer values.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted pointer value as a 64 integer.
    //------------------------------------------------------------------
    uint64_t
    GetPointer (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Get the current byte order value.
    ///
    /// @return
    ///     The current byte order value from this object's internal
    ///     state.
    //------------------------------------------------------------------
    lldb::ByteOrder
    GetByteOrder() const
    {
        return m_byte_order;
    }

    //------------------------------------------------------------------
    /// Extract a uint8_t value from \a *offset_ptr.
    ///
    /// Extract a single uint8_t from the binary data at the offset
    /// pointed to by \a offset_ptr, and advance the offset on success.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted uint8_t value.
    //------------------------------------------------------------------
    uint8_t
    GetU8 ( lldb::offset_t *offset_ptr) const;

    uint8_t
    GetU8_unchecked (lldb::offset_t *offset_ptr) const
    {
        uint8_t val = m_start[*offset_ptr];
        *offset_ptr += 1;
        return val;
    }

    uint16_t
    GetU16_unchecked (lldb::offset_t *offset_ptr) const;

    uint32_t
    GetU32_unchecked (lldb::offset_t *offset_ptr) const;

    uint64_t
    GetU64_unchecked (lldb::offset_t *offset_ptr) const;
    //------------------------------------------------------------------
    /// Extract \a count uint8_t values from \a *offset_ptr.
    ///
    /// Extract \a count uint8_t values from the binary data at the
    /// offset pointed to by \a offset_ptr, and advance the offset on
    /// success. The extracted values are copied into \a dst.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[out] dst
    ///     A buffer to copy \a count uint8_t values into. \a dst must
    ///     be large enough to hold all requested data.
    ///
    /// @param[in] count
    ///     The number of uint8_t values to extract.
    ///
    /// @return
    ///     \a dst if all values were properly extracted and copied,
    ///     NULL otherise.
    //------------------------------------------------------------------
    void *
    GetU8 (lldb::offset_t *offset_ptr, void *dst, uint32_t count) const;

    //------------------------------------------------------------------
    /// Extract a uint16_t value from \a *offset_ptr.
    ///
    /// Extract a single uint16_t from the binary data at the offset
    /// pointed to by \a offset_ptr, and update the offset on success.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted uint16_t value.
    //------------------------------------------------------------------
    uint16_t
    GetU16 (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Extract \a count uint16_t values from \a *offset_ptr.
    ///
    /// Extract \a count uint16_t values from the binary data at the
    /// offset pointed to by \a offset_ptr, and advance the offset on
    /// success. The extracted values are copied into \a dst.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[out] dst
    ///     A buffer to copy \a count uint16_t values into. \a dst must
    ///     be large enough to hold all requested data.
    ///
    /// @param[in] count
    ///     The number of uint16_t values to extract.
    ///
    /// @return
    ///     \a dst if all values were properly extracted and copied,
    ///     NULL otherise.
    //------------------------------------------------------------------
    void *
    GetU16 (lldb::offset_t *offset_ptr, void *dst, uint32_t count) const;

    //------------------------------------------------------------------
    /// Extract a uint32_t value from \a *offset_ptr.
    ///
    /// Extract a single uint32_t from the binary data at the offset
    /// pointed to by \a offset_ptr, and update the offset on success.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted uint32_t value.
    //------------------------------------------------------------------
    uint32_t
    GetU32 (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Extract \a count uint32_t values from \a *offset_ptr.
    ///
    /// Extract \a count uint32_t values from the binary data at the
    /// offset pointed to by \a offset_ptr, and advance the offset on
    /// success. The extracted values are copied into \a dst.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[out] dst
    ///     A buffer to copy \a count uint32_t values into. \a dst must
    ///     be large enough to hold all requested data.
    ///
    /// @param[in] count
    ///     The number of uint32_t values to extract.
    ///
    /// @return
    ///     \a dst if all values were properly extracted and copied,
    ///     NULL otherise.
    //------------------------------------------------------------------
    void *
    GetU32 (lldb::offset_t *offset_ptr, void *dst, uint32_t count) const;

    //------------------------------------------------------------------
    /// Extract a uint64_t value from \a *offset_ptr.
    ///
    /// Extract a single uint64_t from the binary data at the offset
    /// pointed to by \a offset_ptr, and update the offset on success.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted uint64_t value.
    //------------------------------------------------------------------
    uint64_t
    GetU64 (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Extract \a count uint64_t values from \a *offset_ptr.
    ///
    /// Extract \a count uint64_t values from the binary data at the
    /// offset pointed to by \a offset_ptr, and advance the offset on
    /// success. The extracted values are copied into \a dst.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @param[out] dst
    ///     A buffer to copy \a count uint64_t values into. \a dst must
    ///     be large enough to hold all requested data.
    ///
    /// @param[in] count
    ///     The number of uint64_t values to extract.
    ///
    /// @return
    ///     \a dst if all values were properly extracted and copied,
    ///     NULL otherise.
    //------------------------------------------------------------------
    void *
    GetU64 ( lldb::offset_t *offset_ptr, void *dst, uint32_t count) const;

    //------------------------------------------------------------------
    /// Extract a signed LEB128 value from \a *offset_ptr.
    ///
    /// Extracts an signed LEB128 number from this object's data
    /// starting at the offset pointed to by \a offset_ptr. The offset
    /// pointed to by \a offset_ptr will be updated with the offset of
    /// the byte following the last extracted byte.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted signed integer value.
    //------------------------------------------------------------------
    int64_t
    GetSLEB128 (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Extract a unsigned LEB128 value from \a *offset_ptr.
    ///
    /// Extracts an unsigned LEB128 number from this object's data
    /// starting at the offset pointed to by \a offset_ptr. The offset
    /// pointed to by \a offset_ptr will be updated with the offset of
    /// the byte following the last extracted byte.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    ///     The extracted unsigned integer value.
    //------------------------------------------------------------------
    uint64_t
    GetULEB128 (lldb::offset_t *offset_ptr) const;

    lldb::DataBufferSP &
    GetSharedDataBuffer ()
    {
        return m_data_sp;
    }

    //------------------------------------------------------------------
    /// Peek at a C string at \a offset.
    ///
    /// Peeks at a string in the contained data. No verification is done
    /// to make sure the entire string lies within the bounds of this
    /// object's data, only \a offset is verified to be a valid offset.
    ///
    /// @param[in] offset
    ///     An offset into the data.
    ///
    /// @return
    ///     A non-NULL C string pointer if \a offset is a valid offset,
    ///     NULL otherwise.
    //------------------------------------------------------------------
    const char *
    PeekCStr (lldb::offset_t offset) const;

    //------------------------------------------------------------------
    /// Peek at a bytes at \a offset.
    ///
    /// Returns a pointer to \a length bytes at \a offset as long as
    /// there are \a length bytes available starting at \a offset.
    ///
    /// @return
    ///     A non-NULL data pointer if \a offset is a valid offset and
    ///     there are \a length bytes available at that offset, NULL
    ///     otherwise.
    //------------------------------------------------------------------
    const uint8_t*
    PeekData (lldb::offset_t offset, lldb::offset_t length) const
    {
        if (length > 0 && ValidOffsetForDataOfSize(offset, length))
            return m_start + offset;
        return NULL;
    }

    //------------------------------------------------------------------
    /// Set the address byte size.
    ///
    /// Set the size in bytes that will be used when extracting any
    /// address and pointer values from data contained in this object.
    ///
    /// @param[in] addr_size
    ///     The size in bytes to use when extracting addresses.
    //------------------------------------------------------------------
    void
    SetAddressByteSize (uint32_t addr_size)
    {
        m_addr_size = addr_size;
    }

    //------------------------------------------------------------------
    /// Set data with a buffer that is caller owned.
    ///
    /// Use data that is owned by the caller when extracting values.
    /// The data must stay around as long as this object, or any object
    /// that copies a subset of this object's data, is valid. If \a
    /// bytes is NULL, or \a length is zero, this object will contain
    /// no data.
    ///
    /// @param[in] bytes
    ///     A pointer to caller owned data.
    ///
    /// @param[in] length
    ///     The length in bytes of \a bytes.
    ///
    /// @param[in] byte_order
    ///     A byte order of the data that we are extracting from.
    ///
    /// @return
    ///     The number of bytes that this object now contains.
    //------------------------------------------------------------------
    lldb::offset_t
    SetData (const void *bytes, lldb::offset_t length, lldb::ByteOrder byte_order);

    //------------------------------------------------------------------
    /// Adopt a subset of \a data.
    ///
    /// Set this object's data to be a subset of the data bytes in \a
    /// data. If \a data contains shared data, then a reference to the
    /// shared data will be added to ensure the shared data stays around
    /// as long as any objects have references to the shared data. The
    /// byte order and the address size settings are copied from \a
    /// data. If \a offset is not a valid offset in \a data, then no
    /// reference to the shared data will be added. If there are not
    /// \a length bytes available in \a data starting at \a offset,
    /// the length will be truncated to contains as many bytes as
    /// possible.
    ///
    /// @param[in] data
    ///     Another DataExtractor object that contains data.
    ///
    /// @param[in] offset
    ///     The offset into \a data at which the subset starts.
    ///
    /// @param[in] length
    ///     The length in bytes of the subset of \a data.
    ///
    /// @return
    ///     The number of bytes that this object now contains.
    //------------------------------------------------------------------
    lldb::offset_t
    SetData (const DataExtractor& data, lldb::offset_t offset, lldb::offset_t length);

    //------------------------------------------------------------------
    /// Adopt a subset of shared data in \a data_sp.
    ///
    /// Copies the data shared pointer which adds a reference to the
    /// contained in \a data_sp. The shared data reference is reference
    /// counted to ensure the data lives as long as anyone still has a
    /// valid shared pointer to the data in \a data_sp. The byte order
    /// and address byte size settings remain the same. If
    /// \a offset is not a valid offset in \a data_sp, then no reference
    /// to the shared data will be added. If there are not \a length
    /// bytes available in \a data starting at \a offset, the length
    /// will be truncated to contains as many bytes as possible.
    ///
    /// @param[in] data_sp
    ///     A shared pointer to data.
    ///
    /// @param[in] offset
    ///     The offset into \a data_sp at which the subset starts.
    ///
    /// @param[in] length
    ///     The length in bytes of the subset of \a data_sp.
    ///
    /// @return
    ///     The number of bytes that this object now contains.
    //------------------------------------------------------------------
    lldb::offset_t
    SetData (const lldb::DataBufferSP& data_sp, lldb::offset_t offset = 0, lldb::offset_t length = LLDB_INVALID_OFFSET);

    //------------------------------------------------------------------
    /// Set the byte_order value.
    ///
    /// Sets the byte order of the data to extract. Extracted values
    /// will be swapped if necessary when decoding.
    ///
    /// @param[in] byte_order
    ///     The byte order value to use when extracting data.
    //------------------------------------------------------------------
    void
    SetByteOrder (lldb::ByteOrder byte_order)
    {
        m_byte_order = byte_order;
    }

    //------------------------------------------------------------------
    /// Skip an LEB128 number at \a *offset_ptr.
    ///
    /// Skips a LEB128 number (signed or unsigned) from this object's
    /// data starting at the offset pointed to by \a offset_ptr. The
    /// offset pointed to by \a offset_ptr will be updated with the
    /// offset of the byte following the last extracted byte.
    ///
    /// @param[in,out] offset_ptr
    ///     A pointer to an offset within the data that will be advanced
    ///     by the appropriate number of bytes if the value is extracted
    ///     correctly. If the offset is out of bounds or there are not
    ///     enough bytes to extract this value, the offset will be left
    ///     unmodified.
    ///
    /// @return
    //      The number of bytes consumed during the extraction.
    //------------------------------------------------------------------
    uint32_t
    Skip_LEB128 (lldb::offset_t *offset_ptr) const;

    //------------------------------------------------------------------
    /// Test the validity of \a offset.
    ///
    /// @return
    ///     \b true if \a offset is a valid offset into the data in this
    ///     object, \b false otherwise.
    //------------------------------------------------------------------
    bool
    ValidOffset (lldb::offset_t offset) const
    {
        return offset < GetByteSize();
    }

    //------------------------------------------------------------------
    /// Test the availability of \a length bytes of data from \a offset.
    ///
    /// @return
    ///     \b true if \a offset is a valid offset and there are \a
    ///     length bytes available at that offset, \b false otherwise.
    //------------------------------------------------------------------
    bool
    ValidOffsetForDataOfSize (lldb::offset_t offset, lldb::offset_t length) const
    {
        return length <= BytesLeft (offset);
    }

    size_t
    Copy (DataExtractor& dest_data) const;
    
    bool
    Append (DataExtractor& rhs);
    
    bool
    Append (void* bytes, lldb::offset_t length);

    lldb::offset_t
    BytesLeft (lldb::offset_t offset) const
    {
        const lldb::offset_t size = GetByteSize();
        if (size > offset)
            return size - offset;
        return 0;
    }

protected:
    
    //------------------------------------------------------------------
    // Member variables
    //------------------------------------------------------------------
    const uint8_t * m_start;        ///< A pointer to the first byte of data.
    const uint8_t * m_end;          ///< A pointer to the byte that is past the end of the data.
    lldb::ByteOrder m_byte_order;   ///< The byte order of the data we are extracting from.
    uint32_t m_addr_size;           ///< The address size to use when extracting pointers or addresses
    mutable lldb::DataBufferSP m_data_sp; ///< The shared pointer to data that can be shared among multilple instances
    const uint32_t m_target_byte_size;
};

} // namespace lldb_private

#endif  // #if defined (__cplusplus)
#endif  // #ifndef liblldb_DataExtractor_h_
