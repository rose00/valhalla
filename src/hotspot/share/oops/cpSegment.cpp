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
}

