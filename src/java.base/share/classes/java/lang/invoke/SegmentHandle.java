/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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
 */

package java.lang.invoke;

import jdk.internal.misc.Unsafe;
import jdk.internal.vm.annotation.Stable;

import java.lang.invoke.MethodHandles.Lookup;

/**
 * A {@code SegmentHandle} is a compact object providing full access
 * to a constant pool segment, containing one a parametrically varying
 * constant, perhaps with additional constants depending on it.  If a
 * segment is of kind {@code JVM_PARAM_MethodAndClass}, then it is
 * contained in a parent segment of kind {@code JVM_PARAM_Class}, and
 * this segment is independently reflected by a second instance of
 * {@code SegmentHandle}.
 *
 * Every segment is private to the class which defines it.  Normally,
 * only bootstrap methods will work with segment handles directly.
 * The VM also consumes them directly via {@code CONSTANT_Linkage}
 * constant pool entries which are in turn used by certain bytecodes
 * such as {@code invokestatic}, {@code new}, and {@code getfield}.
 *
 * A {@code SegmentHandle} can be <em>specialized</em> to a particular
 * binding of its parameter, and its refs array will track the
 * resolution state for each dependent constant that might be derived
 * from the parameter.  The JVM is encouraged to use extra resources
 * to optimize methods and classes that make use of specialized
 * segments.
 *
 * Alternatively, a {@code SegmentHandle} can be unspecialized,
 * meaning that can will be used by the JVM to process multiple
 * species of methods or "raw species" of classes, with fewer
 * JVM resources consumed but (perhaps) less performance.
 *
 * An unspecialized segment handle is equipped with a distinct copy of
 * resolution state for a particular use of a parameter value.  As
 * with a specialized segment, the resolution state is stored in a
 * plain Java object array.  But while a specialied segment is in
 * one-to-one relation with its backing reference array, for an
 * unspecialized segment the metadata is in a one-to-many relation
 * with all of the backing reference arrays corresponding to various
 * bindings of parameter values.  Each such binding is connected to
 * a {@code SegmentHandle} instance, one-to-one with its own refs
 * array.
 * 
 * The resolution of dependent constants (if any) is tracked relative
 * to this parameter value.  To use fewer resources, the JVM uses a
 * shared metadata structure (and shared JIT code) for all such
 * states.  Thus, the decision to specialize or not can be left to
 * runtime, in BSM logic supplied by a translation strategy, and the
 * same bytecodes can execute in a mix of specialized and
 * unspecialized segment handles, with JVM metadata and JIT code
 * expended for some parameters but not others.
 */

public final class SegmentHandle {
    private @Stable final Info     info;            // layout and type information
    private @Stable final Object[] refs;            // resolved references for this segment
    private @Stable final long     vmsegment;       // metaspace address of a ConstantPoolSegment

    /**
     * Whether this segment is associated only with a class, not a method.
     *
     * @return Whether this segment is associated only with a class, not a method.
     */
    boolean isClass() {
        return !hasMethod();
    }

    /**
     * Whether this segment has constants for a variant class.
     * (It may also have constants for a method.)
     *
     * @return Whether this segment has constants for a variant class.
     */
    boolean hasClass() {
        return info.parameterKindCode() != Meta.CON_JVM_PARAM_MethodOnly;
    }

    /**
     * Whether this segment has constants for a variant method.
     * (It may also have constants for a class.)
     *
     * @return Whether this segment has constants for a variant method.
     */
    boolean hasMethod() {
        return info.parameterKindCode() != Meta.CON_JVM_PARAM_Class;
    }

    /**
     * Whether this segment needs a parent segment.
     * (i.e., it is a method segment with an enclosing class.)
     *
     * @return Whether this segment needs a parent segment.
     */
    boolean hasParent() {
        return info.parameterKindCode() == Meta.CON_JVM_PARAM_MethodAndClass;
    }

    /**
     * The class which declared this segment.
     *
     * @return The class which declared this segment.
     */
    public Class<?> declaringClass() {
        return info.declaringClass();
    }

    /**
     * The index of this segment's lead parameter, in the constant pool
     * of the declaring class.
     *
     * @return The index of this segment's lead parameter.
     */
    public int parameterIndex() {
        return info.parameterIndex();
    }

    /**
     * The bound parameter value (the "actual argument" to the CONSTANT_Parameter).
     *
     * @return The bound parameter value.
     */
    public Object parameterBinding() {
        return refs[Meta.CON_ConstantPoolSegment_argument_ref_index];
    }

    /**
     * The class segment which corresponds to this segment.
     * Returns {@code this} if this segment is already a class segment.
     * Returns {@code null} if this is a "method-only" segment.
     *
     * @return The class segment which corresponds to this segment.
     */
    public SegmentHandle classSegment() {
        return (SegmentHandle) refs[Meta.CON_ConstantPoolSegment_cseg_refs_ref_index];
    }

    /**
     * Reports if this segment is specialized.
     *
     * This is true if there is a 1-1 relation between my metadata
     * {@code ConstantPoolSegment} and my {@code refs} array.
     *
     * @return Whether this segment is specialized.
     */
    public boolean isSpecialized() {
        return Meta.segmentRefs(vmsegment) != null;
    }

    /**
     * Reports if this segment is valid for the given VM segment.
     *
     * @param info a descriptor of the required parameter and class
     * @return Whether this segment is valid for the given VM segment.
     */
    boolean isValidFor(Info info) {
        return this.info == info && this.refs != null;
    }

    /**
     * Reports if this segment is valid for the given VM segment.  The
     * VM segment (i.e., the parameter and class) are determined by a
     * previously created segment on the same parameter and class.
     *
     * The JVM is careful to enforce that any segment passed into any
     * use of a method or class is valid for that method or class's
     * parameterization.  Random unresolved values (not segment
     * handles) are also allowed to be passed in, but are processed
     * by the parameter's bootstrap method when needed.  The result
     * returned by the bootstrap method must be a {@code SegmentHandle}
     * that is valid for the appropriate parameter and class.
     *
     * @param prototype a prototype segment on the required parameter and class
     * @return Whether this segment is valid for the given VM segment.
     */
    public boolean isValidFor(SegmentHandle prototype) {
        return isValidFor(prototype.info);
    }

    /**
     * Create a new "blank" prototype segment of the given parameter
     * and class.
     *
     * It will be specialized if requested, in which case
     * a new vmsegment will be created.  Otherwise, the
     * prototype's vmsegment is recycled.
     *
     * In either case, a new references array is allocated,
     * and will be ready to hold resolution state for this
     * instance of the CP segment.
     */
    private SegmentHandle(Info info, long vmsegment) {
        this.info = info;
        this.refs = null;  // this is a dummy prototype; it holds no bindings
        this.vmsegment = vmsegment;
    }

    /**
     * Create a new segment of the same parameter and class
     * as the prototype.
     *
     * It will be specialized if requested, in which case
     * a new vmsegment will be created.  Otherwise, the
     * prototype's vmsegment is recycled.
     *
     * In either case, a new references array is allocated,
     * and will be ready to hold resolution state for this
     * instance of the CP segment.
     */
    private SegmentHandle(SegmentHandle prototype,
                          boolean specialized,
                          Object parameterBinding,
                          SegmentHandle parent) {
        this.info = prototype.info;
        long pseg = Meta.NULL;
        if (parent != null) {
            assert(this.hasParent() && parent.isClass());
            pseg = parent.vmsegment;
        } else {
            assert(!this.hasParent());
        }

        this.refs = initialRefs(parameterBinding, parent);
        if (specialized) {
            this.vmsegment = vmsegmentAllocate(info.vmseginfo, pseg, refs);
            assert(this.isSpecialized() && Meta.segmentRefs(vmsegment) == refs);
        } else {
            this.vmsegment = prototype.vmsegment;
            assert(!this.isSpecialized());
        }

        // ensure that I am consistent with what is in metaspace:
        assert(parameterBinding() == parameterBinding &&
               refs[Meta.CON_ConstantPoolSegment_handle_ref_index] == this &&
               refs[Meta.CON_ConstantPoolSegment_cseg_refs_ref_index]
               == (isClass() ? this : hasClass() ? parent : null) &&
               assertSegmentOK(vmsegment, info.vmseginfo, parent));
    }

    /**
     * Allocate a new array to hold resolution state for this instance
     * of a CP segment.  The array is of this form:
     *
     * {@code { parameterBinding, ThisSH, ClassSH, RS3, RS4...}}
     *
     * where {@code ThisSH} is a back-pointer to this {@code
     * SegmentHandle} and {@code ClassSH} is a back-pointer to the
     * relevant {@code SegmentHandle} for the enclosing class, or
     * {@code null} if there is no such segment.  The other array
     * elements are initially null and implement the resolution states
     * of any constants directly derived from the parameter binding.
     */
    private Object[] initialRefs(Object parameterBinding, SegmentHandle parent) {
        int reflen = Meta.infoReflen(info.vmseginfo);
        assert(reflen >= Meta.CON_ConstantPoolSegment_fixed_ref_limit);
        Object[] refs = new Object[reflen];
        refs[Meta.CON_ConstantPoolSegment_argument_ref_index] = parameterBinding;
        refs[Meta.CON_ConstantPoolSegment_handle_ref_index] = this;
        refs[Meta.CON_ConstantPoolSegment_cseg_refs_ref_index] = (parent != null ? parent : isClass() ? this : null);
        return refs;
    }

    /** (Called only when assertions are enabled.) */
    private static boolean assertSegmentOK(long vmsegment, long vmseginfo, SegmentHandle parent) {
        assert(Meta.segmentInfo(vmsegment) == vmseginfo);
        int kind = Meta.infoParamKind(vmseginfo);
        long cseg = Meta.segmentCseg(vmsegment);
        if (parent != null) {
            assert(kind == Meta.CON_JVM_PARAM_MethodAndClass);
            assert(cseg != Meta.NULL && cseg == parent.vmsegment);  // up pointer, to class segment
        } else if (kind == Meta.CON_JVM_PARAM_Class) {
            assert(cseg != Meta.NULL && cseg == vmsegment);  // self pointer, to own segment
        } else {
            assert(kind == Meta.CON_JVM_PARAM_MethodOnly);
            assert(cseg == Meta.NULL);
        }
        return true;
    }

    /**
     * Create a fresh segment bound to a given parameter in a given class.
     * The parameter and class are determined by a previously created segment
     * on the same parameter and class.
     *
     * The parent segment is the segment for the enclosing variant
     * class (of type JVM_PARAM_Class), in which case the current
     * segment had better be of type JVM_PARAM_MethodAndClass.
     * Otherwise, the parent segment must be null, since that is
     * the only possible nesting relationship for segments.
     *
     * A specialized segment uses more JVM metaspace resources.
     * An unspecialized segment just uses a few words of Java eap,
     * but cannot help the JIT create type-specialized code
     * paths.
     *
     * @param prototype a previous instance of a segment with the same parameter and class
     * @param specialized whether to make a specialized segment
     * @param parameterBinding the value to assign to the segment's parameter
     * @param parent the enclosing parent segment, if any
     * @return a fresh segment, specialized to the given parameter
     */
    public SegmentHandle makeSegment(SegmentHandle prototype,
                                     boolean specialized,
                                     Object parameterBinding,
                                     SegmentHandle parent) {
        String badarg = null;
        if (parameterBinding == null) {
            badarg = "null parameter binding";
        } else if (parent == null) {
            if (prototype.hasParent())
                badarg = "null parent for JVM_PARAM_MethodAndClass";
        } else if (!parent.isClass()) {
            badarg = "bad parent (not JVM_PARAM_Class)";
        } else if (parent.declaringClass() != prototype.declaringClass()) {
            badarg = "bad parent (wrong class)";
        } else if (!prototype.hasParent()) {
            badarg = "bad parent (child must be JVM_PARAM_MethodAndClass)";
        }
        if (badarg != null) {
            throw new IllegalArgumentException(badarg);
        }
        return new SegmentHandle(prototype, specialized, parameterBinding, parent);
    }

    /*non-public*/
    static SegmentHandle makeInitialPrototype(Class<?> declaringClass,
                                              long vmsegment,
                                              SegmentHandle cseg) {
        return new Info(declaringClass, vmsegment, cseg).prototype;
    }

    /**
     * Since a segment can be instantiated any number of times, the VM
     * keeps track of information common to all instances in a
     * separate place in metaspace, the C struct CPSegmentInfo.  A
     * {@code SegmentHandle.Info} points to this metadata.  Because it
     * holds a reference to the declaring class, this little object
     * also keeps the class alive, so it won't be unloaded. This in
     * turn prevents the metaspace structures from being deallocated.
     * For this reason, the {@code vmseginfo} field is always a valid
     * address, and so is the {@code vmsegment} field for any {@code
     * SegmentHandle} that points to this {@code Info}.
     */
    private static final class Info {
        private @Stable final Class<?> clazz;       // class in which the segment is defined
        private @Stable final long     vmseginfo;   // metaspace address of a CPSegmentInfo
        private @Stable final Info     parent;      // class segment, if this one is nested
        private @Stable final SegmentHandle prototype;  // blank prototype segment

        /**
         * The class which declared this segment.
         */
        Class<?> declaringClass() {
            return clazz;
        }

        /**
         * The index of this segments lead parameter, in the constant pool
         * of the declaring class.
         */
        int parameterIndex() {
            return Meta.constantIndex(Meta.infoConstant(vmseginfo,
                       Meta.CON_CPSegmentInfo_parameter_subindex));
        }

        /**
         * The kind code for this segment.
         */
        int parameterKindCode() {
            return Meta.infoParamKind(vmseginfo);
        }

        private Info(Class<?> declaringClass, long vmsegment, SegmentHandle parent) {
            this.clazz = declaringClass;
            this.vmseginfo = Meta.segmentInfo(vmsegment);
            if (parameterKindCode() == Meta.CON_JVM_PARAM_MethodAndClass) {
                this.parent = parent.info;
            } else {
                this.parent = null;
            }
            // Make the canonical blank segment from the VM-supplied metadata block:
            this.prototype = new SegmentHandle(this, vmsegment);
        }
    }

    /**
     * Holder for methods that traverse metaspace on behalf of SegmentHandle.
     */
    private static final class Meta {
        // struct ConstantPoolSegment
        static long segmentInfo(long cpseg) {  // cpseg: ConstantPoolSegment*
            return getAddress(cpseg + OFF_ConstantPoolSegment_info); // return: CPSegmentInfo*
        }
        static long segmentCseg(long cpseg) {  // cpseg: ConstantPoolSegment*
            return getAddress(cpseg + OFF_ConstantPoolSegment_cseg); // return: ConstantPoolSegment*
        }
        static Object segmentRefs(long cpseg) {  // cpseg: ConstantPoolSegment*
            return resolveOopHandle(cpseg + OFF_ConstantPoolSegment_refs);
        }

        // struct CPSegmentInfo
        static int infoFlags(long info) { // info: CPSegmentInfo*
            return getInt(info + OFF_CPSegmentInfo_flags);
        }
        static int infoParamKind(long info) { // info: CPSegmentInfo*
            return infoFlags(info) & CON_ConstantPool_JVM_PARAM_MASK;
        }
        static int infoReflen(long info) { // info: CPSegmentInfo*
            return getInt(info + OFF_CPSegmentInfo_reflen);
        }
        static int infoConstantCount(long info) { // info: CPSegmentInfo*
            return getInt(info + OFF_CPSegmentInfo_constant_count);
        }
        static long infoConstant(long info, int which) {  // info: CPSegmentInfo*
            assert(which >= 0 && which < infoConstantCount(info));
            long base = info + OFF_CPSegmentInfo_cinfo_array;
            return base + (CON_wordSize * which); // return: CInfo*
        }

        // struct CPSegmentInfo::CInfo
        static int constantIndexAndTag(long cinfo) {  // cinfo: CPSegmentInfo::CInfo*
            return getInt(cinfo + OFF_CInfo_index_and_tag);
        }
        static int constantIndex(long cinfo) {  // cinfo: CPSegmentInfo::CInfo*
            return constantIndexAndTag(cinfo) >> CON_CInfo_index_shift;
        }

        private static final Unsafe U = MethodHandleStatics.UNSAFE; //= Unsafe.getUnsafe();

        static int getInt(long p) { // p: int*
            return U.getInt(null, p);
        }
        static long getAddress(long p) { // p: intptr_t*
            return U.getAddress(null, p);
        }
        static Object resolveOopHandle(long p) {  // p: OopHandle*
            long oopHandleObj = getAddress(p + OFF_OopHandle_obj);
            if (oopHandleObj == Meta.NULL)  return null;
            // Perform extra indirection via OopHandle::_obj:
            return U.getReference(null, oopHandleObj);
            //@@ FIXME: This doesn't work right for compressed oops,
            // and/or load barriers.  In general we lack Unsafe
            // support for resolving OopHandles and/or requesting
            // access to uncompress oops. Known problem, nothing done
            // yet.  Oops.
        }

        static final long NULL                            = 0L;
        static final int CON_wordSize                     = U.addressSize();
        static final int CON_JVM_PARAM_Class              = vmconstant("JVM_PARAM_Class");
        static final int CON_JVM_PARAM_MethodAndClass     = vmconstant("JVM_PARAM_MethodAndClass");
        static final int CON_JVM_PARAM_MethodOnly         = vmconstant("JVM_PARAM_MethodOnly");
        static final int CON_ConstantPool_JVM_PARAM_MASK  = vmconstant("ConstantPool::JVM_PARAM_MASK");

        static final int OFF_ConstantPoolSegment_info     = vmfield("ConstantPoolSegment", "_info", "CPSegmentInfo*");
        static final int OFF_ConstantPoolSegment_cseg     = vmfield("ConstantPoolSegment", "_cseg", "ConstantPoolSegment*");
        static final int OFF_ConstantPoolSegment_refs     = vmfield("ConstantPoolSegment", "_refs", "OopHandle");
        static final int CON_ConstantPoolSegment_argument_ref_index   = vmconstant("ConstantPoolSegment::_argument_ref_index");
        static final int CON_ConstantPoolSegment_handle_ref_index     = vmconstant("ConstantPoolSegment::_handle_ref_index");
        static final int CON_ConstantPoolSegment_cseg_refs_ref_index  = vmconstant("ConstantPoolSegment::_cseg_refs_ref_index");
        static final int CON_ConstantPoolSegment_fixed_ref_limit      = vmconstant("ConstantPoolSegment::_fixed_ref_limit");

        static final int OFF_CPSegmentInfo_flags          = vmfield("CPSegmentInfo", "_flags", "int");
        static final int OFF_CPSegmentInfo_reflen         = vmfield("CPSegmentInfo", "_reflen", "int");
        static final int OFF_CPSegmentInfo_constant_count = vmfield("CPSegmentInfo", "_constant_count", "int");
        //static final int OFF_CPSegmentInfo_cinfo_array  = vmfield("CPSegmentInfo", "_cinfo_array", "CInfo[]");
        static final int OFF_CPSegmentInfo_cinfo_array    = vmconstant("CPSegmentInfo::header_size()") * CON_wordSize;
        static final int CON_CPSegmentInfo_parameter_subindex = vmconstant("CPSegmentInfo::_parameter_subindex");


        static final int OFF_CInfo_index_and_tag          = vmfield("CInfo", "_index_and_tag", "int");
        static final int OFF_CInfo_offset_in_meta         = vmfield("CInfo", "_offset_in_meta", "int");
        static final int OFF_CInfo_offset_in_refs         = vmfield("CInfo", "_offset_in_refs", "int");
        static final int CON_CInfo_sizeof                 = vmconstant("sizeof(CInfo)");
        static final int CON_CInfo_index_shift            = vmconstant("CInfo::_index_shift");

        static final int OFF_OopHandle_obj                = vmfield("OopHandle", "_obj", "oop*");
    }

    // Call the JVM to make a new segment.
    private static native long vmsegmentAllocate(long vmseginfo, long vmsegparent, Object refsOrNull);

    // These two pull information from some place like vmStructs.cpp.
    private static native int vmfield(String typeName, String fieldName, String fieldType);
    private static native int vmconstant(String constantExpression);
}
