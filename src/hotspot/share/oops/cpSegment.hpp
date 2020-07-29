/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_OOPS_CPSEGMENT_HPP
#define SHARE_OOPS_CPSEGMENT_HPP

#include "oops/constantPool.hpp"

class ConstantPool;
class ConstantPoolSegment;

// These are built once at class load time, one per variant segment,
// to record decisions about the population of each segment.
// They do not contain constants, but rather provide templates for any
// variant segments that may be created in the future.
class CPSegmentInfo : public MetaspaceObj {
 public:
  struct CInfo;
 private:

  // If you add a new field that points to any metaspace object, you
  // must add this field to CPSegmentInfo::metaspace_pointers_do().

  ConstantPool* _pool;                  // back pointer to my owner
  int           _segnum;                // 1-based index identifying this kind of segment
  int           _flags;                 // bits in JVM_PARAM_MASK
  int           _reflen;                // length of _refs array for this segment kind
  int           _info_size_in_words;    // my own size
  int           _segment_size_in_words; // size of each of my segments
  int           _constant_count;        // number of constants in this segment

  friend class ConstantPoolSegment;

  //intptr_t* base() const { return (intptr_t*) (((char*) this) + sizeof(CPSegmentInfo)); }
  intptr_t* base() const { return (intptr_t*)this + header_size(); }

  ConstantPoolSegment* create_segment(Handle parameter,
                                      ConstantPoolSegment* cseg,
                                      TRAPS);

 public:

  ConstantPool* pool() const            { return _pool; }
  int segnum() const                    { return _segnum; }
  int param_kind() const                { return (_flags & ConstantPool::JVM_PARAM_MASK); }
  bool is_class() const                 { return param_kind() == JVM_PARAM_Class; }
  bool is_method_only() const           { return param_kind() == JVM_PARAM_MethodOnly; }
  bool is_method_and_class() const      { return param_kind() == JVM_PARAM_MethodAndClass; }
  // Plain but ambiguous 'is_method' is omitted, to avoid bugs.  Use 'has_method'.
  bool has_class() const                { return !is_method_only(); }
  bool has_method() const               { return !is_class(); }
  bool has_both() const                 { return is_method_and_class(); }
  ClassLoaderData* loader_data() const; // = _pool->pool_holder()->class_loader_data();

  int    constant_info_count() const { return _constant_count; }
  CInfo* constant_info_base() const  { return (CInfo*)base(); }
  CInfo* constant_info_end() const   { return constant_info_base() + constant_info_count(); }
  CInfo* constant_info_at(int subidx) const {
    assert(subidx >= 0 && subidx < constant_info_count(), "oob");
    return &constant_info_base()[subidx];
  }
  static const int _parameter_subindex = 0;  // fixed sub-index for lead parameter in CInfo array

  // Info about one constant in this segment:
  struct CInfo {
    int _index_and_tag;       // (index << 8) | tag
    int _offset_in_meta;      // location of data in the ConstantPoolSegment
    int _offset_in_refs;      // location in ConstantPoolSegment::_refs

    static const int _index_shift = 8, _tag_mask = (1 << _index_shift) - 1;
    static int index_and_tag(int index, int tag) {
      assert((tag & 0xFF) == tag, "");
      assert((index & ((unsigned int)-1 >> (_index_shift+1))) == index, "");
      return (index << _index_shift) | tag;
    }
    int index() const { return _index_and_tag >> _index_shift; }
    int tag() const   { return _index_and_tag & _tag_mask; }
    static inline int compare_index_and_tag(int it1, int it2);
  };

  // Segment creation:
  ConstantPoolSegment* new_class_segment(Handle parameter,
                                         TRAPS) {
    assert(is_class(), "must match");
    return create_segment(parameter, NULL, THREAD);
  }
  ConstantPoolSegment* new_method_segment(Handle parameter,
                                          ConstantPoolSegment* cseg,
                                          TRAPS) {
    assert(has_method(), "must match");
    assert((cseg != NULL) == has_both(), "must match");
    return create_segment(parameter, cseg, THREAD);
  }

  // Iteration support:
  ConstantPoolSegment*& segment_list_head() const {
    return _pool->segment_list_head_at(_segnum);
  }
  ConstantPoolSegment* first_seg() const {
    return segment_list_head();
  }
  inline ConstantPoolSegment* next_seg(ConstantPoolSegment* seg) const;

  // MetaspaceObj functions
  void metaspace_pointers_do(MetaspaceClosure* iter);
  static MetaspaceObj::Type type()        { return ConstantPoolSegmentInfoType; }
  size_t size() const                     { return _info_size_in_words; }
  DEBUG_ONLY(bool on_stack() { return false; })
  static size_t header_size() { return align_up(sizeof(CPSegmentInfo), wordSize) / wordSize; }
  static CPSegmentInfo* allocate(ClassLoaderData* loader_data,
                                 ConstantPool* pool,
                                 int segnum,
                                 int parameter_index,
                                 int param_kind,
                                 CPSegmentInfo* include_class,
                                 GrowableArray<int>* fields,
                                 GrowableArray<const Method*>* methods,
                                 GrowableArray<int>* constants,
                                 TRAPS);
  // used via the _info link from ConstantPoolSegment:
  size_t segment_size_in_words() const    { return _segment_size_in_words; }

 private:
  struct Setup;  // local scratch in same file as allocate();
  inline CPSegmentInfo(Setup& setup, TRAPS);  // local handshake in same file as allocate()
  inline static void size_and_initialize_passes(bool initialize_pass, Setup& setup, TRAPS);

 public:
  // Assembly code support
  static int pool_offset_in_bytes()       { return offset_of(CPSegmentInfo, _pool); }
  static int segnum_offset_in_bytes()     { return offset_of(CPSegmentInfo, _segnum); }
};

class ConstantPoolSegment : public MetaspaceObj {
  friend class VMStructs; //@@TODO
  friend class JVMCIVMStructs; //@@TODO
  //friend class MetadataFactory; //@@needed?
  friend class ConstantPool;
  friend class CPSegmentInfo;

  // If you add a new field that points to any metaspace object, you
  // must add this field to ConstantPoolSegment::metaspace_pointers_do().
  CPSegmentInfo*        _info;      // description of this segment's shape
  ConstantPoolSegment*  _cseg;      // anywhere inside Foo<x>, points to Foo<x>
  OopHandle             _refs;      // array containing all resolved oops; specialized segments only
  ConstantPoolSegment*  _segment_list_next; // next in list of active instances (of same info/shape)

  // Note: We could cache more stuff here, but it would directly
  // increase footprint.  Don't cache any derived values here unless
  // there is a performance problem being solved.  The interpreter
  // can handle the extra indirections.

  enum {   // fixed offsets in the refs array, of required oops
    _argument_ref_index         = 0,   // binding of CONSTANT_Parameter (any object)
    _handle_ref_index           = 1,   // jli.SegmentHandle reflecting this CP segment
    _cseg_refs_ref_index        = 2,   // refs array for enclosing class segment (or null if none)
    _fixed_ref_limit            = 3
  };

  // local handshakes with CPSegmentInfo()
  inline ConstantPoolSegment(CPSegmentInfo* info, ConstantPoolSegment* cseg, TRAPS);

 public:
  int param_kind() const                  { return (is_class()  ? JVM_PARAM_Class :
                                                    has_class() ? JVM_PARAM_MethodAndClass :
                                                    JVM_PARAM_MethodOnly); }
  bool is_class() const                   { return _cseg == this; }
  bool has_method() const                 { return _cseg != this; }
  bool has_class() const                  { return _cseg != NULL; }
  ConstantPoolSegment* class_segment()    { assert(has_class(), ""); return _cseg; }

  CPSegmentInfo* info() const             { return _info; }
  ConstantPool* pool() const              { return _info->pool(); }
  int segnum() const                      { return _info->segnum(); }
  ClassLoaderData* loader_data() const    { return _info->loader_data(); }

  objArrayOop refs() const                { return objArrayOop(_refs.resolve()); }
  bool has_refs() const                   { return refs() != NULL; }
  oop  ref_at(int i) const                { assert(has_refs(), ""); return refs()->obj_at(i); }
  void ref_at_put(int i, oop obj) const   { assert(has_refs(), ""); refs()->obj_at_put(i, obj); }

  bool is_specialized() const             { return has_refs(); }

#if 0
  // The following accesses would only apply to specialized segments.
  // Use jli.SegmentHandle for uniform access to both kinds.
  oop argument() const { // binding of CONSTANT_Parameter (any object)
    return ref_at(_argument_ref_index);
  }
  oop handle() const {   // jli.SegmentHandle reflecting this CP segment
    return ref_at(_handle_ref_index);
  }
  objArrayOop cseg_refs() const {  // bindings for enclosing class segment, or NULL
    oop cseg_refs = ref_at(_cseg_refs_ref_index);
    if (cseg_refs == NULL)  return NULL;
    assert(cseg_refs->is_objArray(), "");
    return objArrayOop(cseg_refs);
  }
  oop cseg_handle() const {  // binding for parent segment's CONSTANT_Parameter
    objArrayOop refs = cseg_refs();
    if (refs == NULL)  return NULL;
    return refs->obj_at(_handle_ref_index);
  }
#endif

  // MetaspaceObj functions
  void metaspace_pointers_do(MetaspaceClosure* iter);
  static MetaspaceObj::Type type()        { return ConstantPoolSegmentType; }
  size_t size() const                     { return _info->_segment_size_in_words; }
  static size_t header_size() { return align_up(sizeof(ConstantPoolSegment), wordSize) / wordSize; }
  DEBUG_ONLY(bool on_stack() { return false; })
  static ConstantPoolSegment* allocate(TRAPS);

  // Assembly code support
  static int info_offset_in_bytes()       { return offset_of(ConstantPoolSegment, _info); }
  static int cseg_offset_in_bytes()       { return offset_of(ConstantPoolSegment, _cseg); }
  static int refs_offset_in_bytes()       { return offset_of(ConstantPoolSegment, _refs); }
};

inline ConstantPoolSegment* CPSegmentInfo::next_seg(ConstantPoolSegment* seg) const {
  assert(seg != NULL && seg->_info == this, "");
  return seg->_segment_list_next;
}

#endif // SHARE_OOPS_CPSEGMENT_HPP
