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

#include "precompiled.hpp"
//#include "interpreter/bootstrapInfo.hpp"
//#include "logging/log.hpp"
//#include "logging/logStream.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/metadataFactory.hpp"
#include "memory/metaspaceClosure.hpp"
#include "memory/oopFactory.hpp"
#include "memory/resourceArea.hpp"
#include "oops/array.hpp"
#include "oops/cpSegment.hpp"
#include "oops/cpCache.inline.hpp"
#include "runtime/handles.inline.hpp"


// This block holds all constructor arguments and derived sizing information.
// It is a temporary in the allocator and the sole operand to the constructor.
// Young knave, I hereby dub thee "Setup Pattern".
struct CPSegmentInfo::Setup {
  ClassLoaderData* _loader_data;
  ConstantPool* _pool;
  int _segnum;
  int _parameter_index;
  int _param_kind;
  CPSegmentInfo* _include_class;
  GrowableArray<int>* _fields;
  GrowableArray<const Method*>* _methods;
  GrowableArray<int>* _constants;
  GrowableArray<int> _indexes_and_tags;

  // accumulated sizing information:
  size_t _info_offset_in_bytes;
  size_t _info_size_in_bytes;
  size_t _segment_offset_in_bytes;
  size_t _segment_size_in_bytes;
  size_t _refs_offset;
  size_t _refs_length;

  void reset_offsets() {
    _info_offset_in_bytes = _segment_offset_in_bytes = _refs_offset = 0;
  }
  void copy_offsets_to_sizes() {
    _info_size_in_bytes = _info_offset_in_bytes;
    _segment_size_in_bytes = _segment_offset_in_bytes;
    _refs_length = _refs_offset;
  }

  // substructures
  CPSegmentInfo* _info;
  OopHandle _refs;

#ifdef ASSERT
  bool same_offsets_as(Setup& that) {
    return ((that._info_offset_in_bytes == _info_offset_in_bytes) &&
            (that._segment_offset_in_bytes == _segment_offset_in_bytes) &&
            (that._refs_offset == _refs_offset));
  }
#endif //ASSERT

  Setup() {
    // No, we're not going to thread the arguments through here,
    // because it's a local handshake.  But we do need to zero the
    // counters.
    _info = NULL;
    assert(_refs.resolve() == NULL, "");
    reset_offsets();
    DEBUG_ONLY(_info_size_in_bytes = _segment_size_in_bytes = _refs_length = (size_t)-1);
  }

  size_t info_size_in_words() { return byte_size_to_word_size(_info_size_in_bytes); }
  size_t segment_size_in_words() { return byte_size_to_word_size(_segment_size_in_bytes); }
  int refs_length_as_int() { return size_to_int(_refs_length); }

  // should there be a utility function for this?
  static size_t byte_size_to_word_size(size_t size_in_bytes) {
    return align_up(size_in_bytes, wordSize) / wordSize;
  }
  static int size_to_int(size_t size) {
    int result = (int) size;
    return ((size_t)result == size) ? result : -1;
  }
};

inline int CPSegmentInfo::CInfo::compare_index_and_tag(int it1, int it2) {
  assert(it1 > 0 && it2 > 0, "");
  int t1 = (it1 & CInfo::_tag_mask), t2 = (it2 & 0xFF);
  if (t1 == t2) {
    return it1 - it2;
  }
  switch (t1) {
  case JVM_CONSTANT_Parameter:  return -1;
  case JVM_CONSTANT_Linkage:    t1 = 0; break;
  }
  switch (t2) {
  case JVM_CONSTANT_Parameter:  return 1;
  case JVM_CONSTANT_Linkage:    t2 = 0; break;
  }
  return t1 - t2;
}
static int compare_index_and_tag(int* it1p, int* it2p) {
  return CPSegmentInfo::CInfo::compare_index_and_tag(*it1p, *it2p);
}

inline void CPSegmentInfo::size_and_initialize_passes(bool initialize_pass,
                                                      Setup& s,
                                                      TRAPS) {
  ConstantPool*     cp              = s._pool;
  CPSegmentInfo*    info            = s._info;
  OopHandle&        refs            = s._refs;
  size_t&           info_offset     = s._info_offset_in_bytes;
  size_t&           segment_offset  = s._segment_offset_in_bytes;
  size_t&           refs_offset     = s._refs_offset;

  info_offset    = CPSegmentInfo::header_size() * wordSize;
  segment_offset = ConstantPoolSegment::header_size() * wordSize;
  refs_offset    = ConstantPoolSegment::_fixed_ref_limit;

  int constant_count = s._constants->length();
  if (!initialize_pass) {
    // sort the constants
    for (int i = 0; i < constant_count; i++) {
      int index = s._constants->at(i);
      int tag = cp->tag_at(index).value();
      s._indexes_and_tags.push(CInfo::index_and_tag(index, tag));
    }
    s._indexes_and_tags.sort(compare_index_and_tag);
    int it0 = s._indexes_and_tags.at(0);
    assert(it0 == CInfo::index_and_tag(s._parameter_index, JVM_CONSTANT_Parameter),
           "correct sort");
  } else {
    assert(info->_constant_count == constant_count, "already done");
  }

  // size each constant
  for (int i = 0; i < constant_count; i++) {
    int it = s._indexes_and_tags.at(i);
    int ssize = 0, nrefs = 0;
    switch (it & CInfo::_tag_mask) {
    case JVM_CONSTANT_Parameter:
      assert(i == 0, "must be");
      break;

    case JVM_CONSTANT_Linkage:
    case JVM_CONSTANT_InvokeDynamic:
    case JVM_CONSTANT_Dynamic:
    case JVM_CONSTANT_MethodHandle:
      ssize += wordSize;
      nrefs += 1;
      break;
      
    default:
      ShouldNotReachHere();
    }
    if (initialize_pass) {
      CInfo* ci = info->constant_info_at(i);
      ci->_index_and_tag = it;
      if (ssize != 0)  ci->_offset_in_meta = segment_offset;
      ci->_offset_in_meta = segment_offset;
      if (nrefs != 0)  ci->_offset_in_refs = refs_offset;
    }
    segment_offset += ssize;
    refs_offset    += nrefs;
  }
  info_offset += sizeof(CInfo) * constant_count;

  // (Do we need more data after the CInfo?)
}

inline CPSegmentInfo::CPSegmentInfo(CPSegmentInfo::Setup& s, TRAPS) {
  _pool = s._pool;
  _segnum = s._segnum;
  _flags = s._param_kind;
  assert(param_kind() == s._param_kind, "");

  // record sizing:
  _reflen                = s.refs_length_as_int();
  _info_size_in_words    = s.info_size_in_words();
  _segment_size_in_words = s.segment_size_in_words();
  _constant_count        = s._indexes_and_tags.length();

  assert(_reflen >= ConstantPoolSegment::_fixed_ref_limit, "");
}

ConstantPoolSegment::ConstantPoolSegment(CPSegmentInfo* info,
                                         ConstantPoolSegment* cseg,
                                         TRAPS) {
  _info = info;
  if (info->is_class()) {
    // For fast access to the class from every 'has_class' segment,
    // we plug in the class segment at a known offset.
    // For the 'is_class' segment itself, we plug in a self-loop.
    assert(cseg == NULL, "");
    cseg = this;
  }
  _cseg = cseg;   // NULL, or caller-supplied parent, or this segment
  assert(this->param_kind() == info->param_kind(), "properly encoded in cseg");
  assert(_segment_list_next == NULL, "0-init");
}

CPSegmentInfo* CPSegmentInfo::allocate(ClassLoaderData* loader_data,
                                       ConstantPool* pool,
                                       int segnum,
                                       int parameter_index,
                                       int param_kind,
                                       CPSegmentInfo* include_class,
                                       GrowableArray<int>* fields,
                                       GrowableArray<const Method*>* methods,
                                       GrowableArray<int>* constants,
                                       TRAPS) {
  assert(param_kind >= ConstantPool::JVM_PARAM_MIN &&
         param_kind <= ConstantPool::JVM_PARAM_MAX, "invalid kind");
  Setup s;
  // (does C++ have a trick to capture the whole argument list?)
  s._loader_data = loader_data;
  s._pool = pool;
  s._segnum = segnum;
  s._parameter_index = parameter_index;
  s._param_kind = param_kind;
  s._include_class = include_class;
  s._fields = fields;
  s._methods = methods;
  s._constants = constants;

  size_and_initialize_passes(false, s, CHECK_NULL);
  s.copy_offsets_to_sizes();


  // Sizing complete, now allocate metadata blocks and other resources.
  s._info = new(s._loader_data, s.info_size_in_words(),
                CPSegmentInfo::type(), THREAD) CPSegmentInfo(s, CHECK_NULL);

  DEBUG_ONLY(Setup saved = s);
  s.reset_offsets();
  size_and_initialize_passes(true, s, THREAD);
  assert(saved.same_offsets_as(s), "the passes have to agree");
  return s._info;
}

ConstantPoolSegment* CPSegmentInfo::create_segment(Handle parameter,
                                                   ConstantPoolSegment* cseg,
                                                   TRAPS) {
  ClassLoaderData* loader_data = this->loader_data();

  // do the heap allocation first, then the metadata allocation
  OopHandle refs;
  {
    objArrayOop refs_oop = oopFactory::new_objArray(SystemDictionary::Object_klass(),
                                                    _reflen, CHECK_NULL);
    assert(refs_oop->length() == _reflen, "");
    refs_oop->obj_at_put(ConstantPoolSegment::_argument_ref_index, parameter());
    Handle refs_handle(THREAD, (oop)refs_oop);  // must handleize.
    refs = loader_data->add_handle(refs_handle);
  }

  // do the metaspace allocation second, undoing the first if the second fails
  ConstantPoolSegment* seg
    = new(loader_data, _segment_size_in_words,
          ConstantPoolSegment::type(), THREAD) ConstantPoolSegment(this, cseg, THREAD);
  if (HAS_PENDING_EXCEPTION) {
    loader_data->remove_handle(refs);
    return NULL;
  }
  seg->_refs = refs;
  assert(seg->size() == (size_t)_segment_size_in_words, "");

  objArrayHandle cplock(Thread::current(), _pool->resolved_references());
  // Use the shared resolved_references() lock for my linked list.
  assert(cplock() != NULL, "");
  {
    ObjectLocker ol(cplock, THREAD);
    // link it in, so we can find it to clean it up if necessary
    ConstantPoolSegment* &head = segment_list_head();
    seg->_segment_list_next = head;
    head = seg;
  }

  return seg;
}


void ConstantPoolSegment::metaspace_pointers_do(MetaspaceClosure* it) {
  log_trace(cds)("Iter(ConstantPoolSegment): %p", this);
  it->push(&_info);
  it->push(&_segment_list_next);
  it->push(&_cseg);
}

ClassLoaderData* CPSegmentInfo::loader_data() const {
  assert(_pool->pool_holder() != NULL, "class must be properly initialized");
  return _pool->pool_holder()->class_loader_data();
}

void CPSegmentInfo::metaspace_pointers_do(MetaspaceClosure* it) {
  log_trace(cds)("Iter(CPSegmentInfo): %p", this);
  it->push(&_pool);
}

