//===-- LibCxxVector.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-python.h"

#include "lldb/DataFormatters/CXXFormatterFunctions.h"

#include "lldb/Core/ConstString.h"
#include "lldb/Core/ValueObject.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
    namespace formatters {
        class LibcxxStdVectorSyntheticFrontEnd : public SyntheticChildrenFrontEnd
        {
        public:
            LibcxxStdVectorSyntheticFrontEnd (lldb::ValueObjectSP valobj_sp);
            
            virtual size_t
            CalculateNumChildren ();
            
            virtual lldb::ValueObjectSP
            GetChildAtIndex (size_t idx);
            
            virtual bool
            Update();
            
            virtual bool
            MightHaveChildren ();
            
            virtual size_t
            GetIndexOfChildWithName (const ConstString &name);
            
            virtual
            ~LibcxxStdVectorSyntheticFrontEnd ();
        private:
            ValueObject* m_start;
            ValueObject* m_finish;
            ClangASTType m_element_type;
            uint32_t m_element_size;
            std::map<size_t,lldb::ValueObjectSP> m_children;
        };
    }
}

lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::LibcxxStdVectorSyntheticFrontEnd (lldb::ValueObjectSP valobj_sp) :
SyntheticChildrenFrontEnd(*valobj_sp.get()),
m_start(NULL),
m_finish(NULL),
m_element_type(),
m_element_size(0),
m_children()
{
    if (valobj_sp)
        Update();
}

size_t
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::CalculateNumChildren ()
{
    if (!m_start || !m_finish)
        return 0;
    uint64_t start_val = m_start->GetValueAsUnsigned(0);
    uint64_t finish_val = m_finish->GetValueAsUnsigned(0);
    
    if (start_val == 0 || finish_val == 0)
        return 0;
    
    if (start_val >= finish_val)
        return 0;
    
    size_t num_children = (finish_val - start_val);
    if (num_children % m_element_size)
        return 0;
    return num_children/m_element_size;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::GetChildAtIndex (size_t idx)
{
    if (!m_start || !m_finish)
        return lldb::ValueObjectSP();
    
    auto cached = m_children.find(idx);
    if (cached != m_children.end())
        return cached->second;
    
    uint64_t offset = idx * m_element_size;
    offset = offset + m_start->GetValueAsUnsigned(0);
    StreamString name;
    name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    ValueObjectSP child_sp = ValueObject::CreateValueObjectFromAddress(name.GetData(), offset, m_backend.GetExecutionContextRef(), m_element_type);
    m_children[idx] = child_sp;
    return child_sp;
}

bool
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::Update()
{
    m_start = m_finish = NULL;
    m_children.clear();
    ValueObjectSP data_type_finder_sp(m_backend.GetChildMemberWithName(ConstString("__end_cap_"),true));
    if (!data_type_finder_sp)
        return false;
    data_type_finder_sp = data_type_finder_sp->GetChildMemberWithName(ConstString("__first_"),true);
    if (!data_type_finder_sp)
        return false;
    m_element_type = data_type_finder_sp->GetClangType().GetPointeeType();
    m_element_size = m_element_type.GetByteSize();
    
    if (m_element_size > 0)
    {
        // store raw pointers or end up with a circular dependency
        m_start = m_backend.GetChildMemberWithName(ConstString("__begin_"),true).get();
        m_finish = m_backend.GetChildMemberWithName(ConstString("__end_"),true).get();
    }
    return false;
}

bool
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::MightHaveChildren ()
{
    return true;
}

size_t
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::GetIndexOfChildWithName (const ConstString &name)
{
    if (!m_start || !m_finish)
        return UINT32_MAX;
    return ExtractIndexFromString(name.GetCString());
}

lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::~LibcxxStdVectorSyntheticFrontEnd ()
{
    // these need to stay around because they are child objects who will follow their parent's life cycle
    // delete m_start;
    // delete m_finish;
}

lldb_private::SyntheticChildrenFrontEnd*
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEndCreator (CXXSyntheticChildren*, lldb::ValueObjectSP valobj_sp)
{
    if (!valobj_sp)
        return NULL;
    return (new LibcxxStdVectorSyntheticFrontEnd(valobj_sp));
}

