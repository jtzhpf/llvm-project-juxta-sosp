//===-- AppleObjCRuntimeV2.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AppleObjCRuntimeV2_h_
#define liblldb_AppleObjCRuntimeV2_h_

// C Includes
// C++ Includes

#include <map>
#include <memory>

// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "AppleObjCRuntime.h"

class RemoteNXMapTable;

namespace lldb_private {

class AppleObjCRuntimeV2 :
        public AppleObjCRuntime
{
public:
    virtual ~AppleObjCRuntimeV2();
    
    // These are generic runtime functions:
    virtual bool
    GetDynamicTypeAndAddress (ValueObject &in_value, 
                              lldb::DynamicValueType use_dynamic, 
                              TypeAndOrName &class_type_or_name, 
                              Address &address);
    
    virtual ClangUtilityFunction *
    CreateObjectChecker (const char *);


    //------------------------------------------------------------------
    // Static Functions
    //------------------------------------------------------------------
    static void
    Initialize();
    
    static void
    Terminate();
    
    static lldb_private::LanguageRuntime *
    CreateInstance (Process *process, lldb::LanguageType language);
    
    static lldb_private::ConstString
    GetPluginNameStatic();
    
    //------------------------------------------------------------------
    // PluginInterface protocol
    //------------------------------------------------------------------
    virtual ConstString
    GetPluginName();
    
    virtual uint32_t
    GetPluginVersion();
    
    virtual ObjCRuntimeVersions
    GetRuntimeVersion ()
    {
        return eAppleObjC_V2;
    }

    virtual size_t
    GetByteOffsetForIvar (ClangASTType &parent_qual_type, const char *ivar_name);

    virtual void
    UpdateISAToDescriptorMapIfNeeded();
    
    // none of these are valid ISAs - we use them to infer the type
    // of tagged pointers - if we have something meaningful to say
    // we report an actual type - otherwise, we just say tagged
    // there is no connection between the values here and the tagged pointers map
    static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA = 1;
    static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSAtom = 2;
    static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSNumber = 3;
    static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSDateTS = 4;
    static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSManagedObject = 5;
    static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSDate = 6;

    virtual ConstString
    GetActualTypeName(ObjCLanguageRuntime::ObjCISA isa);
    
    virtual ClassDescriptorSP
    GetClassDescriptor (ValueObject& in_value);
    
    virtual ClassDescriptorSP
    GetClassDescriptorFromISA (ObjCISA isa);
    
    virtual TypeVendor *
    GetTypeVendor();
    
    virtual lldb::addr_t
    LookupRuntimeSymbol (const ConstString &name);
    
    virtual EncodingToTypeSP
    GetEncodingToType ();
    
protected:
    virtual lldb::BreakpointResolverSP
    CreateExceptionResolver (Breakpoint *bkpt, bool catch_bp, bool throw_bp);

private:
    
    class HashTableSignature
    {
    public:
        HashTableSignature ();

        bool
        NeedsUpdate (Process *process,
                     AppleObjCRuntimeV2 *runtime,
                     RemoteNXMapTable &hash_table);
        
        void
        UpdateSignature (const RemoteNXMapTable &hash_table);
    protected:
        uint32_t m_count;
        uint32_t m_num_buckets;
        lldb::addr_t m_buckets_ptr;
    };

    class NonPointerISACache
    {
    public:
        static NonPointerISACache*
        CreateInstance (AppleObjCRuntimeV2& runtime,
                        const lldb::ModuleSP& objc_module_sp);
        

        ObjCLanguageRuntime::ClassDescriptorSP
        GetClassDescriptor (ObjCISA isa);
    private:
        NonPointerISACache (AppleObjCRuntimeV2& runtime,
                            uint64_t objc_debug_isa_class_mask,
                            uint64_t objc_debug_isa_magic_mask,
                            uint64_t objc_debug_isa_magic_value);
        
        bool
        EvaluateNonPointerISA (ObjCISA isa, ObjCISA& ret_isa);
        
        AppleObjCRuntimeV2&                                         m_runtime;
        std::map<ObjCISA,ObjCLanguageRuntime::ClassDescriptorSP>    m_cache;
        uint64_t                                                    m_objc_debug_isa_class_mask;
        uint64_t                                                    m_objc_debug_isa_magic_mask;
        uint64_t                                                    m_objc_debug_isa_magic_value;

        friend class AppleObjCRuntimeV2;
        
        DISALLOW_COPY_AND_ASSIGN(NonPointerISACache);
    };
    
    class TaggedPointerVendor
    {
    public:
        static TaggedPointerVendor*
        CreateInstance (AppleObjCRuntimeV2& runtime,
                        const lldb::ModuleSP& objc_module_sp);
        
        virtual bool
        IsPossibleTaggedPointer (lldb::addr_t ptr) = 0;
        
        virtual ObjCLanguageRuntime::ClassDescriptorSP
        GetClassDescriptor (lldb::addr_t ptr) = 0;
        
        virtual
        ~TaggedPointerVendor () { }
    protected:
        AppleObjCRuntimeV2&                                         m_runtime;
        
        TaggedPointerVendor (AppleObjCRuntimeV2& runtime) :
        m_runtime(runtime)
        {
        }
    private:
        
        DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendor);
    };
    
    class TaggedPointerVendorRuntimeAssisted : public TaggedPointerVendor
    {
    public:
        virtual bool
        IsPossibleTaggedPointer (lldb::addr_t ptr);
        
        virtual ObjCLanguageRuntime::ClassDescriptorSP
        GetClassDescriptor (lldb::addr_t ptr);
    protected:
        TaggedPointerVendorRuntimeAssisted (AppleObjCRuntimeV2& runtime,
                                             uint64_t objc_debug_taggedpointer_mask,
                                             uint32_t objc_debug_taggedpointer_slot_shift,
                                             uint32_t objc_debug_taggedpointer_slot_mask,
                                             uint32_t objc_debug_taggedpointer_payload_lshift,
                                             uint32_t objc_debug_taggedpointer_payload_rshift,
                                             lldb::addr_t objc_debug_taggedpointer_classes);
        
        typedef std::map<uint8_t,ObjCLanguageRuntime::ClassDescriptorSP> Cache;
        typedef Cache::iterator CacheIterator;
        Cache                                                       m_cache;
        uint64_t                                                    m_objc_debug_taggedpointer_mask;
        uint32_t                                                    m_objc_debug_taggedpointer_slot_shift;
        uint32_t                                                    m_objc_debug_taggedpointer_slot_mask;
        uint32_t                                                    m_objc_debug_taggedpointer_payload_lshift;
        uint32_t                                                    m_objc_debug_taggedpointer_payload_rshift;
        lldb::addr_t                                                m_objc_debug_taggedpointer_classes;
        
        friend class AppleObjCRuntimeV2::TaggedPointerVendor;
        
        DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendorRuntimeAssisted);
    };
    
    class TaggedPointerVendorLegacy : public TaggedPointerVendor
    {
    public:
        virtual bool
        IsPossibleTaggedPointer (lldb::addr_t ptr);
        
        virtual ObjCLanguageRuntime::ClassDescriptorSP
        GetClassDescriptor (lldb::addr_t ptr);
    protected:
        TaggedPointerVendorLegacy (AppleObjCRuntimeV2& runtime) :
        TaggedPointerVendor (runtime)
        {
        }
        
        friend class AppleObjCRuntimeV2::TaggedPointerVendor;
        
        DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendorLegacy);
    };
    
    AppleObjCRuntimeV2 (Process *process,
                        const lldb::ModuleSP &objc_module_sp);
    
    ObjCISA
    GetPointerISA (ObjCISA isa);
    
    bool
    IsTaggedPointer(lldb::addr_t ptr);
    
    lldb::addr_t
    GetISAHashTablePointer ();

    bool
    UpdateISAToDescriptorMapFromMemory (RemoteNXMapTable &hash_table);
    
    bool
    UpdateISAToDescriptorMapDynamic(RemoteNXMapTable &hash_table);
    
    void
    ParseClassInfoArray (const lldb_private::DataExtractor &data,
                         uint32_t num_class_infos);
    
    bool
    UpdateISAToDescriptorMapSharedCache ();

    lldb::addr_t
    GetSharedCacheReadOnlyAddress();
    
    friend class ClassDescriptorV2;

    std::unique_ptr<ClangFunction>            m_get_class_info_function;
    std::unique_ptr<ClangUtilityFunction>     m_get_class_info_code;
    lldb::addr_t                            m_get_class_info_args;
    Mutex                                   m_get_class_info_args_mutex;

    std::unique_ptr<ClangFunction>            m_get_shared_cache_class_info_function;
    std::unique_ptr<ClangUtilityFunction>     m_get_shared_cache_class_info_code;
    lldb::addr_t                            m_get_shared_cache_class_info_args;
    Mutex                                   m_get_shared_cache_class_info_args_mutex;

    std::unique_ptr<TypeVendor>               m_type_vendor_ap;
    lldb::addr_t                            m_isa_hash_table_ptr;
    HashTableSignature                      m_hash_signature;
    bool                                    m_has_object_getClass;
    bool                                    m_loaded_objc_opt;
    std::unique_ptr<NonPointerISACache>       m_non_pointer_isa_cache_ap;
    std::unique_ptr<TaggedPointerVendor>    m_tagged_pointer_vendor_ap;
    EncodingToTypeSP                        m_encoding_to_type_sp;
};
    
} // namespace lldb_private

#endif  // liblldb_AppleObjCRuntimeV2_h_
