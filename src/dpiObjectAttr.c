//-----------------------------------------------------------------------------
// Copyright (c) 2016 Oracle and/or its affiliates.  All rights reserved.
// This program is free software: you can modify it and/or redistribute it
// under the terms of:
//
// (i)  the Universal Permissive License v 1.0 or at your option, any
//      later version (http://oss.oracle.com/licenses/upl); and/or
//
// (ii) the Apache License v 2.0. (http://www.apache.org/licenses/LICENSE-2.0)
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// dpiObjectAttr.c
//   Implementation of object attributes.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// dpiObjectAttr__allocate() [INTERNAL]
//   Allocate and initialize an object attribute structure.
//-----------------------------------------------------------------------------
int dpiObjectAttr__allocate(dpiObjectType *objType, OCIParam *param,
        dpiObjectAttr **attr, dpiError *error)
{
    dpiObjectAttr *tempAttr;
    OCITypeCode typeCode;
    sword status;

    // allocate and assign main reference to the type this attribute belongs to
    *attr = NULL;
    if (dpiGen__allocate(DPI_HTYPE_OBJECT_ATTR, objType->env,
            (void**) &tempAttr, error) < 0)
        return DPI_FAILURE;
    if (dpiGen__setRefCount(objType, error, 1) < 0) {
        dpiObjectAttr__free(tempAttr, error);
        return DPI_FAILURE;
    }
    tempAttr->belongsToType = objType;

    // determine the name of the attribute
    if (dpiUtils__getAttrStringWithDup(error, "get name", param,
            OCI_DTYPE_PARAM, OCI_ATTR_NAME, &tempAttr->name,
            &tempAttr->nameLength) < 0) {
        dpiObjectAttr__free(tempAttr, error);
        return DPI_FAILURE;
    }

    // determine the type of the attribute
    status = OCIAttrGet(param, OCI_DTYPE_PARAM, (dvoid*) &typeCode, 0,
            OCI_ATTR_TYPECODE, error->handle);
    if (dpiError__check(error, status, objType->conn, "get type code") < 0) {
        dpiObjectAttr__free(tempAttr, error);
        return DPI_FAILURE;
    }
    tempAttr->oracleType = dpiOracleType__getFromObjectTypeInfo(typeCode,
            error);

    // if the type of the attribute is an object, determine that object type
    if (typeCode == OCI_TYPECODE_NAMEDCOLLECTION ||
            typeCode == OCI_TYPECODE_OBJECT) {
        if (dpiObjectType__allocate(objType->conn, param, OCI_ATTR_TYPE_NAME,
                &tempAttr->type, error) < 0) {
            dpiObjectAttr__free(tempAttr, error);
            return DPI_FAILURE;
        }
    }

    *attr = tempAttr;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// dpiObjectAttr__free() [INTERNAL]
//   Free the memory for an object attribute.
//-----------------------------------------------------------------------------
void dpiObjectAttr__free(dpiObjectAttr *attr, dpiError *error)
{
    if (attr->belongsToType) {
        dpiGen__setRefCount(attr->belongsToType, error, -1);
        attr->belongsToType = NULL;
    }
    if (attr->type) {
        dpiGen__setRefCount(attr->type, error, -1);
        attr->type = NULL;
    }
    if (attr->name) {
        free((void*) attr->name);
        attr->name = NULL;
    }
    free(attr);
}


//-----------------------------------------------------------------------------
// dpiObjectAttr_addRef() [PUBLIC]
//   Add a reference to the object attribute.
//-----------------------------------------------------------------------------
int dpiObjectAttr_addRef(dpiObjectAttr *attr)
{
    return dpiGen__addRef(attr, DPI_HTYPE_OBJECT_ATTR, __func__);
}


//-----------------------------------------------------------------------------
// dpiObjectAttr_getInfo() [PUBLIC]
//   Return information about the attribute to the caller.
//-----------------------------------------------------------------------------
int dpiObjectAttr_getInfo(dpiObjectAttr *attr, dpiObjectAttrInfo *info)
{
    dpiError error;

    if (dpiGen__startPublicFn(attr, DPI_HTYPE_OBJECT_ATTR, __func__,
            &error) < 0)
        return DPI_FAILURE;
    info->name = attr->name;
    info->nameLength = attr->nameLength;
    if (attr->oracleType) {
        info->oracleTypeNum = attr->oracleType->oracleTypeNum;
        info->defaultNativeTypeNum = attr->oracleType->defaultNativeTypeNum;
    } else {
        info->oracleTypeNum = 0;
        info->defaultNativeTypeNum = 0;
    }
    info->objectType = attr->type;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// dpiObjectAttr_release() [PUBLIC]
//   Release a reference to the object attribute.
//-----------------------------------------------------------------------------
int dpiObjectAttr_release(dpiObjectAttr *attr)
{
    return dpiGen__release(attr, DPI_HTYPE_OBJECT_ATTR, __func__);
}

