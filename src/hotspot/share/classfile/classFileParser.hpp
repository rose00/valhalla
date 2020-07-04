/*
 * Copyright (c) 1997, 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_CLASSFILE_CLASSFILEPARSER_HPP
#define SHARE_CLASSFILE_CLASSFILEPARSER_HPP

#include "memory/referenceType.hpp"
#include "oops/annotations.hpp"
#include "oops/constantPool.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/typeArrayOop.hpp"
#include "utilities/accessFlags.hpp"

class Annotations;
template <typename T>
class Array;
class ClassFileStream;
class ClassLoaderData;
class ClassLoadInfo;
class ClassInstanceInfo;
class CompressedLineNumberWriteStream;
class ConstMethod;
class FieldInfo;
template <typename T>
class GrowableArray;
class InstanceKlass;
class RecordComponent;
class Symbol;
class TempNewSymbol;
class FieldLayoutBuilder;

// Utility to collect and compact oop maps during layout
class OopMapBlocksBuilder : public ResourceObj {
 public:
  OopMapBlock* _nonstatic_oop_maps;
  unsigned int _nonstatic_oop_map_count;
  unsigned int _max_nonstatic_oop_maps;

  OopMapBlocksBuilder(unsigned int  max_blocks);
  OopMapBlock* last_oop_map() const;
  void initialize_inherited_blocks(OopMapBlock* blocks, unsigned int nof_blocks);
  void add(int offset, int count);
  void copy(OopMapBlock* dst);
  void compact();
  void print_on(outputStream* st) const;
  void print_value_on(outputStream* st) const;
};

// Values needed for oopmap and InstanceKlass creation
class FieldLayoutInfo : public ResourceObj {
 public:
  OopMapBlocksBuilder* oop_map_blocks;
  int _instance_size;
  int _nonstatic_field_size;
  int _static_field_size;
  bool  _has_nonstatic_fields;
  bool  _is_naturally_atomic;
  bool _has_inline_fields;
};

// Parser for for .class files
//
// The bytes describing the class file structure is read from a Stream object

class ClassFileParser {
  friend class FieldLayoutBuilder;
  friend class FieldLayout;
  friend class SegmentInfo;

  class ClassAnnotationCollector;
  class FieldAllocationCount;
  class FieldAnnotationCollector;
  class SegmentInfo;

 public:
  // The ClassFileParser has an associated "publicity" level
  // It is used to control which subsystems (if any)
  // will observe the parsing (logging, events, tracing).
  // Default level is "BROADCAST", which is equivalent to
  // a "public" parsing attempt.
  //
  // "INTERNAL" level should be entirely private to the
  // caller - this allows for internal reuse of ClassFileParser
  //
  enum Publicity {
    INTERNAL,
    BROADCAST
  };

  enum { LegalClass, LegalField, LegalMethod }; // used to verify unqualified names

 private:
  // Potentially unaligned pointer to various 16-bit entries in the class file
  typedef void unsafe_u2;

  const ClassFileStream* _stream; // Actual input stream
  Symbol* _class_name;
  mutable ClassLoaderData* _loader_data;
  const InstanceKlass* _unsafe_anonymous_host;
  GrowableArray<Handle>* _cp_patches; // overrides for CP entries
  const bool _is_hidden;
  const bool _can_access_vm_annotations;
  int _num_patched_klasses;
  int _max_num_patched_klasses;
  int _orig_cp_size;
  int _first_patched_klass_resolved_index;

  // Metadata created before the instance klass is created.  Must be deallocated
  // if not transferred to the InstanceKlass upon successful class loading
  // in which case these pointers have been set to NULL.
  const InstanceKlass* _super_klass;
  ConstantPool* _cp;
  Array<u2>* _fields;
  Array<Method*>* _methods;
  Array<u2>* _inner_classes;
  Array<u2>* _nest_members;
  u2 _nest_host;
  Array<u2>* _permitted_subclasses;
  Array<RecordComponent*>* _record_components;
  GrowableArray<InstanceKlass*>* _temp_local_interfaces;
  Array<InstanceKlass*>* _local_interfaces;
  Array<InstanceKlass*>* _transitive_interfaces;
  Annotations* _combined_annotations;
  AnnotationArray* _class_annotations;
  AnnotationArray* _class_type_annotations;
  Array<AnnotationArray*>* _fields_annotations;
  Array<AnnotationArray*>* _fields_type_annotations;
  InstanceKlass* _klass;  // InstanceKlass* once created.
  InstanceKlass* _klass_to_deallocate; // an InstanceKlass* to be destroyed

  ClassAnnotationCollector* _parsed_annotations;
  FieldAllocationCount* _fac;
  FieldLayoutInfo* _field_info;
  const intArray* _method_ordering;
  GrowableArray<Method*>* _all_mirandas;

  enum { fixed_buffer_size = 128 };
  u_char _linenumbertable_buffer[fixed_buffer_size];

  // Size of Java vtable (in words)
  int _vtable_size;
  int _itable_size;

  int _num_miranda_methods;

  int _alignment;
  int _first_field_offset;
  int _exact_size_in_bytes;

  ReferenceType _rt;
  Handle _protection_domain;
  AccessFlags _access_flags;

  // for tracing and notifications
  Publicity _pub_level;

  // Used to keep track of whether a constant pool item 19 or 20 is found.  These
  // correspond to CONSTANT_Module and CONSTANT_Package tags and are not allowed
  // in regular class files.  For class file version >= 53, a CFE cannot be thrown
  // immediately when these are seen because a NCDFE must be thrown if the class's
  // access_flags have ACC_MODULE set.  But, the access_flags haven't been looked
  // at yet.  So, the bad constant pool item is cached here.  A value of zero
  // means that no constant pool item 19 or 20 was found.
  short _bad_constant_seen;

  // class attributes parsed before the instance klass is created:
  bool _synthetic_flag;
  int _sde_length;
  const char* _sde_buffer;
  u2 _sourcefile_index;
  u2 _generic_signature_index;

  u2 _major_version;
  u2 _minor_version;
  u2 _this_class_index;
  u2 _super_class_index;
  u2 _itfs_len;
  u2 _java_fields_count;

  bool _need_verify;
  bool _relax_verify;

  bool _has_nonstatic_concrete_methods;
  bool _declares_nonstatic_concrete_methods;
  bool _has_final_method;
  bool _has_contended_fields;

  bool _has_inline_type_fields;
  bool _has_nonstatic_fields;
  bool _is_empty_inline_type;
  bool _is_naturally_atomic;
  bool _is_declared_atomic;
  bool _invalid_inline_super;   // if true, invalid super type for an inline type.
  bool _invalid_identity_super; // if true, invalid super type for an identity type.
  bool _implements_identityObject;
  bool _has_injected_identityObject;

  // precomputed flags
  bool _has_finalizer;
  bool _has_empty_finalizer;
  bool _has_vanilla_constructor;

  // segmentation logic for generic specializaiton
  int _segment_count;      // count of CONSTANT_Parameter entries
  int* _segment_to_constant_map;   // tentatively ordered list of CONSTANT_Parameters
  int* _constant_to_segment_map;  // non-null if there are segments
  int* _field_parameter_indexes;  // temporary list of Parametric values for fields
  int _class_parametric_constant_index;  // this class's Parametric attribute if any
  SegmentInfo** _segments;  // one per CONSTANT_Parameter entry
  SegmentInfo* segment_info(int segnum) const {
    assert(is_valid_segment_number(segnum, true), "oob");
    assert(_segments[segnum] != NULL, "must be populated");
    return _segments[segnum];
  }
  int segment_for_constant(int cp_index, bool allow_tbd = false) const {
    assert(_segment_count > 0 && _constant_to_segment_map != NULL, "init");
    assert(allow_tbd || _segments != NULL, "do not report numbers prematurely");
    assert(cp_index > 0 && cp_index < _cp->length(), "oob");
    int segnum = _constant_to_segment_map[cp_index];
    assert(allow_tbd || is_valid_segment_number(segnum), "_constant_to_segment_map not ready");
    return segnum;
  }
  void set_segment_for_constant(int cp_index, int segnum) {
    int prev = segment_for_constant(cp_index, true);  // trigger asserts
    // Note:  segnum might be "colored" as a negative number
    assert(segnum < 0 || is_valid_segment_number(segnum), "oob");
    _constant_to_segment_map[cp_index] = segnum;
  }
  static const int SEG_NONE    = 0;    // default entry in _constant_to_segment_map
  static const int SEG_MIN     = 1;    // first segment index (they are one-based!)
  static const int SEG_TBD     = -1;   // sentinel *value* to initiate computation
  static const int SEG_WORKING = -5 << 24;  // sentinel *bits* for circularity check
  bool is_valid_segment_number(int segnum, bool reject_zero = false) const {
    assert(has_segments(), "");
    return (segnum >= (reject_zero ? SEG_MIN : SEG_NONE) && segnum <= segment_max());
  }
  bool has_segments() const {
    assert(_segment_count >= 0, "we have not looked for segments yet");
    return _segment_count > 0;
  }
  int segment_max() const {
    assert(has_segments(), "");
    return _segment_count;
  }

  int _max_bootstrap_specifier_index;  // detects BSS values

  void parse_stream(const ClassFileStream* const stream, TRAPS);

  void mangle_hidden_class_name(InstanceKlass* const ik);

  void post_parse_processing(TRAPS);

  void setup_segment_maps(int cp_length, TRAPS);
  void find_constant_pool_segments(TRAPS);
  void check_constant_pool_segments(TRAPS);

  void prepend_host_package_name(const InstanceKlass* unsafe_anonymous_host, TRAPS);
  void fix_unsafe_anonymous_class_name(TRAPS);

  void fill_instance_klass(InstanceKlass* ik, bool cf_changed_in_CFLH,
                           const ClassInstanceInfo& cl_inst_info, TRAPS);

  void set_klass(InstanceKlass* instance);

  void set_class_bad_constant_seen(short bad_constant);
  short class_bad_constant_seen() { return  _bad_constant_seen; }
  void set_class_synthetic_flag(bool x)        { _synthetic_flag = x; }
  void set_class_sourcefile_index(u2 x)        { _sourcefile_index = x; }
  void set_class_generic_signature_index(u2 x) { _generic_signature_index = x; }
  void set_class_parametric_constant_index(u2 x) { _class_parametric_constant_index = x; }
  void set_class_sde_buffer(const char* x, int len)  { _sde_buffer = x; _sde_length = len; }

  void create_combined_annotations(TRAPS);
  void apply_parsed_class_attributes(InstanceKlass* k);  // update k
  void apply_parsed_class_metadata(InstanceKlass* k, int fields_count, TRAPS);
  void clear_class_metadata();

  // Constant pool parsing
  void parse_constant_pool_entries(const ClassFileStream* const stream,
                                   ConstantPool* cp,
                                   const int length,
                                   TRAPS);

  void parse_constant_pool(const ClassFileStream* const cfs,
                           ConstantPool* const cp,
                           const int length,
                           TRAPS);

  // Interface parsing
  void parse_interfaces(const ClassFileStream* const stream,
                        const int itfs_len,
                        ConstantPool* const cp,
                        bool is_inline_type,
                        bool* has_nonstatic_concrete_methods,
                        bool* is_declared_atomic,
                        TRAPS);

  const InstanceKlass* parse_super_class(ConstantPool* const cp,
                                         const int super_class_index,
                                         const bool need_verify,
                                         TRAPS);

  // Field parsing
  void parse_field_attributes(const ClassFileStream* const cfs,
                              u2 attributes_count,
                              bool is_static,
                              u2 signature_index,
                              u2* const constantvalue_index_addr,
                              bool* const is_synthetic_addr,
                              u2* const generic_signature_index_addr,
                              u2* const parametric_index_addr,
                              FieldAnnotationCollector* parsed_annotations,
                              TRAPS);

  void parse_fields(const ClassFileStream* const cfs,
                    bool is_interface,
                    bool is_inline_type,
                    FieldAllocationCount* const fac,
                    ConstantPool* cp,
                    const int cp_size,
                    u2* const java_fields_count_ptr,
                    TRAPS);

  // Method parsing
  Method* parse_method(const ClassFileStream* const cfs,
                       bool is_interface,
                       bool is_inline_type,
                       const ConstantPool* cp,
                       AccessFlags* const promoted_flags,
                       TRAPS);

  void parse_methods(const ClassFileStream* const cfs,
                     bool is_interface,
                     bool is_inline_type,
                     AccessFlags* const promoted_flags,
                     bool* const has_final_method,
                     bool* const declares_nonstatic_concrete_methods,
                     TRAPS);

  const unsafe_u2* parse_exception_table(const ClassFileStream* const stream,
                                         u4 code_length,
                                         u4 exception_table_length,
                                         TRAPS);

  void parse_linenumber_table(u4 code_attribute_length,
                              u4 code_length,
                              CompressedLineNumberWriteStream**const write_stream,
                              TRAPS);

  const unsafe_u2* parse_localvariable_table(const ClassFileStream* const cfs,
                                             u4 code_length,
                                             u2 max_locals,
                                             u4 code_attribute_length,
                                             u2* const localvariable_table_length,
                                             bool isLVTT,
                                             TRAPS);

  const unsafe_u2* parse_checked_exceptions(const ClassFileStream* const cfs,
                                            u2* const checked_exceptions_length,
                                            u4 method_attribute_length,
                                            TRAPS);

  // Classfile attribute parsing
  u2 parse_generic_signature_attribute(const ClassFileStream* const cfs, TRAPS);
  u2 parse_parametric_attribute(const ClassFileStream* const cfs, TRAPS);
  void parse_classfile_sourcefile_attribute(const ClassFileStream* const cfs, TRAPS);
  void parse_classfile_source_debug_extension_attribute(const ClassFileStream* const cfs,
                                                        int length,
                                                        TRAPS);

  void parse_parametric_attribute(const ClassFileStream* const cfs,
                                  const char* where,
                                  u4 attribute_length,
                                  u2 *parametric_attribute_addr,
                                  TRAPS);

  u2   parse_classfile_inner_classes_attribute(const ClassFileStream* const cfs,
                                               const u1* const inner_classes_attribute_start,
                                               bool parsed_enclosingmethod_attribute,
                                               u2 enclosing_method_class_index,
                                               u2 enclosing_method_method_index,
                                               TRAPS);

  u2 parse_classfile_nest_members_attribute(const ClassFileStream* const cfs,
                                            const u1* const nest_members_attribute_start,
                                            TRAPS);

  u2 parse_classfile_permitted_subclasses_attribute(const ClassFileStream* const cfs,
                                                    const u1* const permitted_subclasses_attribute_start,
                                                    TRAPS);

  u2 parse_classfile_record_attribute(const ClassFileStream* const cfs,
                                      const ConstantPool* cp,
                                      const u1* const record_attribute_start,
                                      TRAPS);

  bool supports_sealed_types();
  bool supports_records();

  void parse_classfile_attributes(const ClassFileStream* const cfs,
                                  ConstantPool* cp,
                                  ClassAnnotationCollector* parsed_annotations,
                                  TRAPS);

  void parse_classfile_synthetic_attribute(TRAPS);
  void parse_classfile_signature_attribute(const ClassFileStream* const cfs, TRAPS);
  void parse_classfile_bootstrap_methods_attribute(const ClassFileStream* const cfs,
                                                   ConstantPool* cp,
                                                   u4 attribute_length,
                                                   TRAPS);

  // Annotations handling
  AnnotationArray* assemble_annotations(const u1* const runtime_visible_annotations,
                                        int runtime_visible_annotations_length,
                                        const u1* const runtime_invisible_annotations,
                                        int runtime_invisible_annotations_length,
                                        TRAPS);

  void set_precomputed_flags(InstanceKlass* k);

  // Format checker methods
  void classfile_parse_error(const char* msg, TRAPS) const;
  void classfile_parse_error(const char* msg, int index, TRAPS) const;
  void classfile_parse_error(const char* msg, const char *name, TRAPS) const;
  void classfile_parse_error(const char* msg,
                             int index,
                             const char *name,
                             TRAPS) const;
  void classfile_parse_error(const char* msg,
                             const char* name,
                             const char* signature,
                             TRAPS) const;

  inline void guarantee_property(bool b, const char* msg, TRAPS) const {
    if (!b) { classfile_parse_error(msg, CHECK); }
  }

  void report_assert_property_failure(const char* msg, TRAPS) const PRODUCT_RETURN;
  void report_assert_property_failure(const char* msg, int index, TRAPS) const PRODUCT_RETURN;

  inline void assert_property(bool b, const char* msg, TRAPS) const {
#ifdef ASSERT
    if (!b) {
      report_assert_property_failure(msg, THREAD);
    }
#endif
  }

  inline void assert_property(bool b, const char* msg, int index, TRAPS) const {
#ifdef ASSERT
    if (!b) {
      report_assert_property_failure(msg, index, THREAD);
    }
#endif
  }

  inline void check_property(bool property,
                             const char* msg,
                             int index,
                             TRAPS) const {
    if (_need_verify) {
      guarantee_property(property, msg, index, CHECK);
    } else {
      assert_property(property, msg, index, CHECK);
    }
  }

  inline void check_property(bool property, const char* msg, TRAPS) const {
    if (_need_verify) {
      guarantee_property(property, msg, CHECK);
    } else {
      assert_property(property, msg, CHECK);
    }
  }

  inline void guarantee_property(bool b,
                                 const char* msg,
                                 int index,
                                 TRAPS) const {
    if (!b) { classfile_parse_error(msg, index, CHECK); }
  }

  inline void guarantee_property(bool b,
                                 const char* msg,
                                 const char *name,
                                 TRAPS) const {
    if (!b) { classfile_parse_error(msg, name, CHECK); }
  }

  inline void guarantee_property(bool b,
                                 const char* msg,
                                 int index,
                                 const char *name,
                                 TRAPS) const {
    if (!b) { classfile_parse_error(msg, index, name, CHECK); }
  }

  void throwIllegalSignature(const char* type,
                             const Symbol* name,
                             const Symbol* sig,
                             TRAPS) const;

  void throwInlineTypeLimitation(THREAD_AND_LOCATION_DECL,
                                 const char* msg,
                                 const Symbol* name = NULL,
                                 const Symbol* sig  = NULL) const;

  void verify_constantvalue(const ConstantPool* const cp,
                            int constantvalue_index,
                            int signature_index,
                            TRAPS) const;

  void verify_legal_utf8(const unsigned char* buffer, int length, TRAPS) const;
  void verify_legal_class_name(const Symbol* name, TRAPS) const;
  void verify_legal_field_name(const Symbol* name, TRAPS) const;
  void verify_legal_method_name(const Symbol* name, TRAPS) const;

  void verify_legal_field_signature(const Symbol* fieldname,
                                    const Symbol* signature,
                                    TRAPS) const;
  int  verify_legal_method_signature(const Symbol* methodname,
                                     const Symbol* signature,
                                     TRAPS) const;

  void verify_legal_class_modifiers(jint flags, TRAPS) const;
  void verify_legal_field_modifiers(jint flags,
                                    bool is_interface,
                                    bool is_inline_type,
                                    TRAPS) const;
  void verify_legal_method_modifiers(jint flags,
                                     bool is_interface,
                                     bool is_inline_type,
                                     const Symbol* name,
                                     TRAPS) const;

  const char* skip_over_field_signature(const char* signature,
                                        bool void_ok,
                                        unsigned int length,
                                        TRAPS) const;

  bool has_cp_patch_at(int index) const {
    assert(index >= 0, "oob");
    return (_cp_patches != NULL
            && index < _cp_patches->length()
            && _cp_patches->adr_at(index)->not_null());
  }

  Handle cp_patch_at(int index) const {
    assert(has_cp_patch_at(index), "oob");
    return _cp_patches->at(index);
  }

  Handle clear_cp_patch_at(int index);

  void patch_class(ConstantPool* cp, int class_index, Klass* k, Symbol* name);
  void patch_constant_pool(ConstantPool* cp,
                           int index,
                           Handle patch,
                           TRAPS);

  // Wrapper for constantTag.is_klass_[or_]reference.
  // In older versions of the VM, Klass*s cannot sneak into early phases of
  // constant pool construction, but in later versions they can.
  // %%% Let's phase out the old is_klass_reference.
  bool valid_klass_reference_at(int index) const {
    return _cp->is_within_bounds(index) &&
             _cp->tag_at(index).is_klass_or_reference();
  }

  // Checks that the cpool index is in range and is a utf8
  bool valid_symbol_at(int cpool_index) const {
    return _cp->is_within_bounds(cpool_index) &&
             _cp->tag_at(cpool_index).is_utf8();
  }

  void copy_localvariable_table(const ConstMethod* cm,
                                int lvt_cnt,
                                u2* const localvariable_table_length,
                                const unsafe_u2** const localvariable_table_start,
                                int lvtt_cnt,
                                u2* const localvariable_type_table_length,
                                const unsafe_u2** const localvariable_type_table_start,
                                TRAPS);

  void copy_method_annotations(ConstMethod* cm,
                               const u1* runtime_visible_annotations,
                               int runtime_visible_annotations_length,
                               const u1* runtime_invisible_annotations,
                               int runtime_invisible_annotations_length,
                               const u1* runtime_visible_parameter_annotations,
                               int runtime_visible_parameter_annotations_length,
                               const u1* runtime_invisible_parameter_annotations,
                               int runtime_invisible_parameter_annotations_length,
                               const u1* runtime_visible_type_annotations,
                               int runtime_visible_type_annotations_length,
                               const u1* runtime_invisible_type_annotations,
                               int runtime_invisible_type_annotations_length,
                               const u1* annotation_default,
                               int annotation_default_length,
                               TRAPS);

  // lays out fields in class and returns the total oopmap count
  void layout_fields(ConstantPool* cp,
                     const FieldAllocationCount* fac,
                     const ClassAnnotationCollector* parsed_annotations,
                     FieldLayoutInfo* info,
                     TRAPS);

  void update_class_name(Symbol* new_name);

  // Check if the class file supports inline types
  bool supports_inline_types() const;

 public:
  ClassFileParser(ClassFileStream* stream,
                  Symbol* name,
                  ClassLoaderData* loader_data,
                  const ClassLoadInfo* cl_info,
                  Publicity pub_level,
                  TRAPS);

  ~ClassFileParser();

  InstanceKlass* create_instance_klass(bool cf_changed_in_CFLH, const ClassInstanceInfo& cl_inst_info, TRAPS);

  const ClassFileStream* clone_stream() const;

  void set_klass_to_deallocate(InstanceKlass* klass);

  int static_field_size() const;
  int total_oop_map_count() const;
  jint layout_size() const;

  int vtable_size() const { return _vtable_size; }
  int itable_size() const { return _itable_size; }

  u2 this_class_index() const { return _this_class_index; }

  bool is_unsafe_anonymous() const { return _unsafe_anonymous_host != NULL; }
  bool is_hidden() const { return _is_hidden; }
  bool is_interface() const { return _access_flags.is_interface(); }
  bool is_inline_type() const { return _access_flags.is_inline_type(); }
  bool is_value_capable_class() const;
  bool has_inline_fields() const { return _has_inline_type_fields; }
  bool invalid_inline_super() const { return _invalid_inline_super; }
  void set_invalid_inline_super() { _invalid_inline_super = true; }
  bool invalid_identity_super() const { return _invalid_identity_super; }
  void set_invalid_identity_super() { _invalid_identity_super = true; }
  bool is_invalid_super_for_inline_type();

  u2 java_fields_count() const { return _java_fields_count; }

  const InstanceKlass* unsafe_anonymous_host() const { return _unsafe_anonymous_host; }
  const GrowableArray<Handle>* cp_patches() const { return _cp_patches; }
  ClassLoaderData* loader_data() const { return _loader_data; }
  const Symbol* class_name() const { return _class_name; }
  const InstanceKlass* super_klass() const { return _super_klass; }

  ReferenceType reference_type() const { return _rt; }
  AccessFlags access_flags() const { return _access_flags; }

  bool is_internal() const { return INTERNAL == _pub_level; }

  static bool verify_unqualified_name(const char* name, unsigned int length, int type);

#ifdef ASSERT
  static bool is_internal_format(Symbol* class_name);
#endif

};

#endif // SHARE_CLASSFILE_CLASSFILEPARSER_HPP
