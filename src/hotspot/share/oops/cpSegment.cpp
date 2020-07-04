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
#include "jvm.h"
#include "classfile/classLoaderData.hpp"  //@@needed?
#include "classfile/javaClasses.inline.hpp"  //@@needed?
#include "classfile/metadataOnStackMark.hpp"  //@@needed?
#include "classfile/stringTable.hpp"  //@@needed?
#include "classfile/systemDictionary.hpp"  //@@needed?
#include "classfile/vmSymbols.hpp"  //@@needed?
#include "interpreter/bootstrapInfo.hpp"  //@@needed?
#include "interpreter/linkResolver.hpp"  //@@needed?
#include "logging/log.hpp"  //@@needed?
#include "logging/logStream.hpp"  //@@needed?
#include "memory/allocation.inline.hpp"  //@@needed?
#include "memory/heapShared.hpp"  //@@needed?
#include "memory/metadataFactory.hpp"  //@@needed?
#include "memory/metaspaceClosure.hpp"  //@@needed?
#include "memory/metaspaceShared.hpp"  //@@needed?
#include "memory/oopFactory.hpp"  //@@needed?
#include "memory/resourceArea.hpp"  //@@needed?
#include "memory/universe.hpp"  //@@needed?
#include "oops/array.hpp"  //@@needed?
#include "oops/constantPool.inline.hpp"  //@@needed?

#include "oops/cpSegment.hpp"  //@@inline?

#include "oops/cpCache.inline.hpp"  //@@needed?
#include "oops/instanceKlass.hpp"  //@@needed?
#include "oops/objArrayKlass.hpp"  //@@needed?
#include "oops/objArrayOop.inline.hpp"  //@@needed?
#include "oops/oop.inline.hpp"  //@@needed?
#include "oops/typeArrayOop.inline.hpp"  //@@needed?
#include "oops/valueArrayKlass.hpp"  //@@needed?
#include "runtime/atomic.hpp"  //@@needed?
#include "runtime/handles.inline.hpp"  //@@needed?
#include "runtime/init.hpp"  //@@needed?
#include "runtime/javaCalls.hpp"  //@@needed?
#include "runtime/signature.hpp"  //@@needed?
#include "runtime/thread.inline.hpp"  //@@needed?
#include "runtime/vframe.inline.hpp"  //@@needed?
#include "utilities/copy.hpp"  //@@needed?


// This block holds all constructor arguments and derived sizing information.
// It is a temporary in the allocator and the sole operand to the constructor.
// Young knave, I hereby dub thee "Setup Pattern".
struct CPSegmentInfo::Setup {
  ConstantPool* _pool;
  int _segnum;
  bool _is_class;
  CPSegmentInfo* _include_class;
  GrowableArray<int>* _fields;
  GrowableArray<Method*>* _methods;
  GrowableArray<int>* _constants;

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
  ConstantPoolSegment* _original;
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
    _original = NULL;
    assert(_refs.resolve() == NULL, "");
    reset_offsets();
    DEBUG_ONLY(_info_size_in_bytes = _segment_size_in_bytes = _refs_length = (size_t)-1);
  }

  ClassLoaderData* loader_data() {
    return _pool->pool_holder()->class_loader_data();
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

inline void CPSegmentInfo::size_and_initialize_passes(bool initialize_pass,
                                                      Setup& s,
                                                      TRAPS) {
  //@@
}

inline CPSegmentInfo::CPSegmentInfo(CPSegmentInfo::Setup& s, TRAPS) {
  _pool = s._pool;
  _segnum = s._segnum;
  _reflen = s.refs_length_as_int();
  _flags = (s._is_class         ? _has_class_entries
            : !s._include_class ? _has_method_entries
            :                     _has_both);
  _size_in_words = s.info_size_in_words();
  _original = s._original;
  _segs_list = NULL;  // no segs in this list, yet, just the blank original

  assert(_reflen >= ConstantPoolSegment::_fixed_ref_limit, "");
}

inline ConstantPoolSegment::ConstantPoolSegment(CPSegmentInfo::Setup& s, TRAPS) {
  _info = s._info;
  _cseg = NULL;  // may be initialized after clone
  assert(_refs.resolve() == NULL, ""); // initialized after clone
  _size_in_words = s.segment_size_in_words();
  _segs_next = NULL;
}

ConstantPoolSegment::ConstantPoolSegment(const ConstantPoolSegment* original, TRAPS) {
  assert(original != NULL, "");
  _info = original->_info;
  _cseg = NULL;
  // _cseg, _refs filled in later
  _size_in_words = original->_size_in_words;
  _segs_next = NULL;
  assert(wordSize == sizeof(HeapWord), "");
  Copy::disjoint_words((HeapWord*) original + header_size(),
                       (HeapWord*) this     + header_size(),
                       _size_in_words       - header_size());
}

CPSegmentInfo* CPSegmentInfo::allocate(ConstantPool* pool,
                                       int segnum,
                                       bool is_class,
                                       CPSegmentInfo* include_class,
                                       GrowableArray<int>* fields,
                                       GrowableArray<Method*>* methods,
                                       GrowableArray<int>* constants,
                                       TRAPS) {
  Setup s;
  // (does C++ have a trick to capture the whole argument list?)
  s._pool = pool;
  s._segnum = segnum;
  s._is_class = is_class;
  s._include_class = include_class;
  s._fields = fields;
  s._methods = methods;
  s._constants = constants;

  size_and_initialize_passes(false, s, CHECK_NULL);
  s.copy_offsets_to_sizes();

  ClassLoaderData* loader_data = s.loader_data();

  // Sizing complete, now allocate metadata blocks and other resources.
  s._info = new(loader_data, s.info_size_in_words(),
                CPSegmentInfo::type(), THREAD) CPSegmentInfo(s, CHECK_NULL);
  s._original = new(loader_data, s.segment_size_in_words(),
                    ConstantPoolSegment::type(), THREAD) ConstantPoolSegment(s, CHECK_NULL);

  DEBUG_ONLY(Setup saved = s);
  s.reset_offsets();
  size_and_initialize_passes(true, s, THREAD);
  assert(saved.same_offsets_as(s), "the passes have to agree");
  assert(!s._info->_original->is_active(), "");
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
    = new(loader_data, _original->_size_in_words,
          ConstantPoolSegment::type(), THREAD) ConstantPoolSegment(_original, THREAD);
  if (HAS_PENDING_EXCEPTION) {
    loader_data->remove_handle(refs);
    return NULL;
  }
  seg->_refs = refs;

  // fill in _cseg
  if (is_class()) {
    assert(cseg == NULL, "");
    seg->_cseg = seg;  // self-loop, to make fast access from any "has_class" segment
  } else if (cseg != NULL) {
    assert(has_class_entries(), "");
    assert(cseg->is_active(), "");
    seg->_cseg = cseg;
  } else {
    assert(!has_class_entries(), "");
  }

  assert(seg->is_active(), "");

  objArrayHandle cplock(Thread::current(), _pool->resolved_references());
  // Use the shared resolved_references() lock for my linked list.
  assert(cplock() != NULL, "");
  {
    ObjectLocker ol(cplock, THREAD);
    // link it in, so we can find it to clean it up if necessary
    seg->_segs_next = this->_segs_list;
    this->_segs_list = seg;
  }

  return seg;
}


void ConstantPoolSegment::metaspace_pointers_do(MetaspaceClosure* it) {
  log_trace(cds)("Iter(ConstantPoolSegment): %p", this);
  it->push(&_info);
  it->push(&_segs_next);
  it->push(&_cseg);
}

void CPSegmentInfo::metaspace_pointers_do(MetaspaceClosure* it) {
  log_trace(cds)("Iter(CPSegmentInfo): %p", this);
  it->push(&_pool);
  it->push(&_segs_list);
  it->push(&_original);
}

