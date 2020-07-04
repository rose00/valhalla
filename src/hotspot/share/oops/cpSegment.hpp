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

//@@ #include "oops/constantPool.hpp"
//@@ the following lines are copied from constantPool.hpp:
#include "memory/allocation.hpp"  //@@needed?
#include "oops/arrayOop.hpp"  //@@needed?
#include "oops/cpCache.hpp"  //@@needed?
#include "oops/objArrayOop.hpp"  //@@needed?
#include "oops/oopHandle.hpp"  //@@needed?
#include "oops/symbol.hpp"  //@@needed?
#include "oops/typeArrayOop.hpp"  //@@needed?
#include "runtime/handles.hpp"  //@@needed?
#include "utilities/align.hpp"  //@@needed?
#include "utilities/bytes.hpp"  //@@needed?
#include "utilities/constantTag.hpp"  //@@needed?

class ConstantPoolSegment;

// These are built once at class load time, one per variant segment,
// to record decisions about the population of each segment.
// They do not contain constants, but rather provide templates for any
// variant segments that may be created in the future.
class CPSegmentInfo : public MetaspaceObj {
  // If you add a new field that points to any metaspace object, you
  // must add this field to CPSegmentInfo::metaspace_pointers_do().

  ConstantPool*         _pool;      // back pointer to my owner
  int                   _segnum;    // 1-based index identifying this kind of segment
  int                   _reflen;    // length of _refs array for this segment kind
  int                   _flags;     // (see below)
  size_t                _size_in_words;
  ConstantPoolSegment*  _original;  // blank copy, to be cloned by create
  ConstantPoolSegment*  _segs_list; // list of active instances (of this info/shape)

  friend class ConstantPoolSegment;

  enum {      // for _flags field
    _has_class_entries   = 1,
    _has_method_entries  = 2,
    _has_both            = (_has_class_entries | _has_method_entries)
  };

  ConstantPoolSegment* create_segment(Handle parameter,
                                      ConstantPoolSegment* cseg,
                                      TRAPS);

 public:

  ConstantPool* pool() const              { return _pool; }
  int segnum() const                      { return _segnum; }
  bool has_class_entries() const          { return (_flags & _has_class_entries) != 0; }
  bool has_method_entries() const         { return (_flags & _has_method_entries) != 0; }
  bool has_both_kinds_of_entry() const    { return (_flags & _has_both) == _has_both; }
  bool is_method() const                  { return has_method_entries(); }
  bool is_class() const                   { return !has_method_entries(); }
  ClassLoaderData* loader_data() const    { return _pool->pool_holder()->class_loader_data(); }

  // Segment creation:
  ConstantPoolSegment* new_class_segment(Handle parameter,
                                         TRAPS) {
    assert(is_class(), "must match");
    return create_segment(parameter, NULL, THREAD);
  }
  ConstantPoolSegment* new_method_segment(Handle parameter,
                                          ConstantPoolSegment* cseg,
                                          TRAPS) {
    assert(is_method(), "must match");
    assert((cseg != NULL) == has_both_kinds_of_entry(), "must match");
    return create_segment(parameter, cseg, THREAD);
  }

  // Iteration support:
  ConstantPoolSegment* first_seg() const {
    return _segs_list;
  }
  inline ConstantPoolSegment* next_seg(ConstantPoolSegment* seg) const;

  // MetaspaceObj functions
  void metaspace_pointers_do(MetaspaceClosure* iter);
  static MetaspaceObj::Type type()        { return ConstantPoolSegmentInfoType; }
  size_t size() const                     { return _size_in_words; }
  size_t header_size() { return align_up(sizeof(CPSegmentInfo), wordSize) / wordSize; }
  static CPSegmentInfo* allocate(ConstantPool* pool,
                                 int segnum,
                                 bool is_class,
                                 CPSegmentInfo* include_class,
                                 GrowableArray<int>* fields,
                                 GrowableArray<Method*>* methods,
                                 GrowableArray<int>* constants,
                                 TRAPS);
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
  OopHandle             _refs;      // array containing all oops stored for this segment
  size_t                _size_in_words;
  ConstantPoolSegment*  _segs_next; // next in list of active instances (of same info/shape)

  // Note: We could cache more stuff here, but it would directly
  // increase footprint.  Don't cache any derived values here unless
  // there is a performance problem being solved.  The interpreter
  // can handle the extra indirections.

  enum {   // fixed offsets in the refs array, of required oops
    _argument_ref_index         = 0,   // binding of CONSTANT_Parameter
    _reflector_ref_index        = 1,   // segment reflector object (if different)
    _fixed_ref_limit            = 2
  };

  // local handshakes with CPSegmentInfo()
  inline ConstantPoolSegment(CPSegmentInfo::Setup& setup, TRAPS);
  inline ConstantPoolSegment(const ConstantPoolSegment* original, TRAPS);

 public:
  bool is_class_segment() const           { return _cseg == this; }
  bool is_method_segment() const          { return _cseg != this; }
  bool has_class_segment() const          { return _cseg != NULL; }
  ConstantPoolSegment* class_segment()    { assert(has_class_segment(), ""); return _cseg; }
  bool is_active() const                  { return _refs.peek() != NULL; }

  CPSegmentInfo* info() const             { return _info; }
  ConstantPool* pool() const              { return _info->pool(); }
  int segnum() const                      { return _info->segnum(); }
  ClassLoaderData* loader_data() const    { return _info->loader_data(); }

  objArrayOop refs() const                { return objArrayOop(_refs.resolve()); }
  oop ref_at(int i) const                 { return refs()->obj_at(i); }
  void ref_at_put(int i, oop obj) const   { refs()->obj_at_put(i, obj); }

  oop argument() const {  //binding for this segment's CONSTANT_Parameter
    return ref_at(_argument_ref_index);
  }
  oop reflector() const {  //reflector object for this segment (may be same as argument)
    return ref_at(_reflector_ref_index);
  }

  // MetaspaceObj functions
  void metaspace_pointers_do(MetaspaceClosure* iter);
  static MetaspaceObj::Type type()        { return ConstantPoolSegmentType; }
  size_t size() const                     { return _size_in_words; }
  size_t header_size() { return align_up(sizeof(ConstantPoolSegment), wordSize) / wordSize; }
  static ConstantPoolSegment* allocate(TRAPS);

  // Assembly code support
  static int info_offset_in_bytes()       { return offset_of(ConstantPoolSegment, _info); }
  static int cseg_offset_in_bytes()       { return offset_of(ConstantPoolSegment, _cseg); }
  static int refs_offset_in_bytes()       { return offset_of(ConstantPoolSegment, _refs); }
};

inline ConstantPoolSegment* CPSegmentInfo::next_seg(ConstantPoolSegment* seg) const {
  assert(seg != NULL && seg->_info == this, "");
  return seg->_segs_next;
}

#endif // SHARE_OOPS_CPSEGMENT_HPP
