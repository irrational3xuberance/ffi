/*
 * Copyright (c) 2009, Wayne Meissner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * The name of the author or authors may not be used to endorse or promote
 *   products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <ruby.h>
#include <ffi.h>
#include "rbffi.h"
#include "compat.h"
#include "Types.h"
#include "Type.h"


typedef struct BuiltinType_ {
    Type type;
    char* name;
} BuiltinType;

static void builtin_type_free(BuiltinType *);

VALUE rbffi_TypeClass = Qnil;

static VALUE classBuiltinType = Qnil;
static VALUE typeMap = Qnil, sizeMap = Qnil;
static ID id_find_type = 0, id_type_size = 0, id_size = 0;

static VALUE
type_allocate(VALUE klass)
{
    Type* type;
    VALUE obj = Data_Make_Struct(klass, Type, NULL, -1, type);

    type->nativeType = -1;
    type->ffiType = &ffi_type_void;
    
    return obj;
}

static VALUE
type_initialize(VALUE self, VALUE value)
{
    Type* type;
    Type* other;

    Data_Get_Struct(self, Type, type);

    if (FIXNUM_P(value)) {
        type->nativeType = FIX2INT(value);
    } else if (rb_obj_is_kind_of(value, rbffi_TypeClass)) {
        Data_Get_Struct(value, Type, other);
        type->nativeType = other->nativeType;
        type->ffiType = other->ffiType;
    } else {
        rb_raise(rb_eArgError, "wrong type");
    }
    
    return self;
}

static VALUE
type_size(VALUE self)
{
    Type *type;

    Data_Get_Struct(self, Type, type);

    return INT2FIX(type->ffiType->size);
}

static VALUE
type_alignment(VALUE self)
{
    Type *type;

    Data_Get_Struct(self, Type, type);

    return INT2FIX(type->ffiType->alignment);
}

static VALUE
type_inspect(VALUE self)
{
    char buf[256];
    Type *type;

    Data_Get_Struct(self, Type, type);
    snprintf(buf, sizeof(buf), "#<FFI::Type:%p size=%d alignment=%d>", 
            type, (int) type->ffiType->size, (int) type->ffiType->alignment);

    return rb_str_new2(buf);
}

int
rbffi_Type_GetIntValue(VALUE type)
{
    Type* t;

    if (!rb_obj_is_kind_of(type, rbffi_TypeClass)) {
        rb_raise(rb_eTypeError, "wrong type.  Expected (FFI::Type)");
    }

    Data_Get_Struct(type, Type, t);

    return t->nativeType;
}

static VALUE
builtin_type_new(VALUE klass, int nativeType, ffi_type* ffiType, const char* name)
{
    BuiltinType* type;
    VALUE obj = Qnil;

    obj = Data_Make_Struct(klass, BuiltinType, NULL, builtin_type_free, type);
    
    type->name = strdup(name);
    type->type.nativeType = nativeType;
    type->type.ffiType = ffiType;

    return obj;
}

static void
builtin_type_free(BuiltinType *type)
{
    free(type->name);
    xfree(type);
}

static VALUE
builtin_type_inspect(VALUE self)
{
    char buf[256];
    BuiltinType *type;

    Data_Get_Struct(self, BuiltinType, type);
    snprintf(buf, sizeof(buf), "#<FFI::Type::Builtin:%s size=%d alignment=%d>",
            type->name, (int) type->type.ffiType->size, type->type.ffiType->alignment);

    return rb_str_new2(buf);
}

int
rbffi_type_size(VALUE type)
{
    int t = TYPE(type);
    if (t == T_FIXNUM || t == T_BIGNUM) {
        return NUM2INT(type);
    } else if (t == T_SYMBOL) {
        /*
         * Try looking up directly in the type and size maps
         */
        VALUE nType;
        if ((nType = rb_hash_aref(typeMap, type)) != Qnil) {
            VALUE nSize = rb_hash_aref(sizeMap, nType);
            if (TYPE(nSize) == T_FIXNUM) {
                return FIX2INT(nSize);
            }
        }
        // Not found - call up to the ruby version to resolve
        return NUM2INT(rb_funcall2(rbffi_FFIModule, id_type_size, 1, &type));
    } else {
        return NUM2INT(rb_funcall2(type, id_size, 0, NULL));
    }
}

VALUE
rbffi_Type_Lookup(VALUE name)
{
    int t = TYPE(name);
    if (t == T_SYMBOL || t == T_STRING) {
        /*
         * Try looking up directly in the type Map
         */
        VALUE nType;
        if ((nType = rb_hash_aref(typeMap, name)) != Qnil && rb_obj_is_kind_of(nType, rbffi_TypeClass)) {
            return nType;
        }
    } else if (rb_obj_is_kind_of(name, rbffi_TypeClass)) {
    
        return name;
    }

    /* Nothing found - let caller handle raising exceptions */
    return Qnil;
}

/**
 * rbffi_Type_Find() is like rbffi_Type_Lookup, but an error is raised if the
 * type is not found.
 */
VALUE
rbffi_Type_Find(VALUE name)
{
    VALUE rbType = rbffi_Type_Lookup(name);

    if (!RTEST(rbType)) {
        rb_raise(rb_eTypeError, "invalid type, %s", RSTRING_PTR(rb_inspect(name)));
    }

    return rbType;
}

void
rbffi_Type_Init(VALUE moduleFFI)
{
    VALUE moduleNativeType;
    VALUE classType = rbffi_TypeClass = rb_define_class_under(moduleFFI, "Type", rb_cObject);

    rb_define_const(moduleFFI, "TypeDefs", typeMap = rb_hash_new());
    rb_define_const(moduleFFI, "SizeTypes", sizeMap = rb_hash_new());
    rb_global_variable(&typeMap);
    rb_global_variable(&sizeMap);
    id_find_type = rb_intern("find_type");
    id_type_size = rb_intern("type_size");
    id_size = rb_intern("size");


    classBuiltinType = rb_define_class_under(rbffi_TypeClass, "Builtin", rbffi_TypeClass);
    moduleNativeType = rb_define_module_under(moduleFFI, "NativeType");

    rb_global_variable(&rbffi_TypeClass);
    rb_global_variable(&classBuiltinType);
    rb_global_variable(&moduleNativeType);

    rb_define_alloc_func(classType, type_allocate);
    rb_define_method(classType, "initialize", type_initialize, 1);
    rb_define_method(classType, "size", type_size, 0);
    rb_define_method(classType, "alignment", type_alignment, 0);
    rb_define_method(classType, "inspect", type_inspect, 0);

    // Make Type::Builtin non-allocatable
    rb_undef_method(CLASS_OF(classBuiltinType), "new");
    rb_define_method(classBuiltinType, "inspect", builtin_type_inspect, 0);
    
    rb_global_variable(&rbffi_TypeClass);
    rb_global_variable(&classBuiltinType);

    // Define all the builtin types
    #define T(x, ffiType) do { \
        VALUE t = Qnil; \
        rb_define_const(classType, #x, t = builtin_type_new(classBuiltinType, NATIVE_##x, ffiType, #x)); \
        rb_define_const(moduleNativeType, #x, t); \
        rb_define_const(moduleFFI, "TYPE_" #x, t); \
    } while(0)

    #define A(old_type, new_type) do { \
        VALUE t = rb_const_get(classType, rb_intern(#old_type)); \
        rb_const_set(classType, rb_intern(#new_type), t); \
    } while(0)

    T(VOID, &ffi_type_void);
    T(INT8, &ffi_type_sint8);
    A(INT8, SCHAR);
    A(INT8, CHAR);
    T(UINT8, &ffi_type_uint8);
    A(UINT8, UCHAR);

    T(INT16, &ffi_type_sint16);
    A(INT16, SHORT);
    A(INT16, SSHORT);
    T(UINT16, &ffi_type_uint16);
    A(UINT16, USHORT);
    T(INT32, &ffi_type_sint32);
    A(INT32, INT);
    A(INT32, SINT);
    T(UINT32, &ffi_type_uint32);
    A(UINT32, UINT);
    T(INT64, &ffi_type_sint64);
    A(INT64, LONG_LONG);
    A(INT64, SLONG_LONG);
    T(UINT64, &ffi_type_uint64);
    A(UINT64, ULONG_LONG);
    T(LONG, &ffi_type_slong);
    A(LONG, SLONG);
    T(ULONG, &ffi_type_ulong);
    T(FLOAT32, &ffi_type_float);
    A(FLOAT32, FLOAT);
    T(FLOAT64, &ffi_type_double);
    A(FLOAT64, DOUBLE);
    T(POINTER, &ffi_type_pointer);
    T(STRING, &ffi_type_pointer);
    T(BUFFER_IN, &ffi_type_pointer);
    T(BUFFER_OUT, &ffi_type_pointer);
    T(BUFFER_INOUT, &ffi_type_pointer);
    T(BOOL, &ffi_type_uchar);
    T(VARARGS, &ffi_type_void);
}
