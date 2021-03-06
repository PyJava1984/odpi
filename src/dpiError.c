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
// dpiError.c
//   Implementation of error.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"
#include "dpiErrorMessages.h"

//-----------------------------------------------------------------------------
// dpiError__check() [INTERNAL]
//   Checks to see if the status of the last call resulted in an error
// condition. If so, the error is populated. Note that trailing newlines and
// spaces are truncated from the message if they exist. If the connection is
// not NULL a check is made to see if the connection is no longer viable.
//-----------------------------------------------------------------------------
int dpiError__check(dpiError *error, sword status, dpiConn *conn,
        const char *action)
{
    uint32_t i, numChars, bufferChars;
    sword errorGetStatus;
    uint16_t *utf16chars;
    char *ptr;

    // no error has taken place
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO)
        return DPI_SUCCESS;

    // special error cases
    if (status == OCI_INVALID_HANDLE)
        return dpiError__set(error, action, DPI_ERR_INVALID_HANDLE, "OCI");
    else if (!error->handle)
        return dpiError__set(error, action, DPI_ERR_ERR_NOT_INITIALIZED);

    // fetch OCI error
    error->buffer->action = action;
    strcpy(error->buffer->encoding, error->encoding);
    errorGetStatus = OCIErrorGet(error->handle, 1, NULL, &error->buffer->code,
            (unsigned char*) error->buffer->message,
            sizeof(error->buffer->message), OCI_HTYPE_ERROR);
    if (errorGetStatus != OCI_SUCCESS)
        return dpiError__set(error, action, DPI_ERR_GET_FAILED);

    // determine if error is recoverable (Transaction Guard)
    // if the attribute cannot be read properly, simply leave it as false;
    // otherwise, that error will mask the one that we really want to see
    error->buffer->isRecoverable = 0;
#if DPI_ORACLE_CLIENT_VERSION_HEX >= DPI_ORACLE_CLIENT_VERSION(12, 1)
    OCIAttrGet(error->handle, OCI_HTYPE_ERROR,
            (dvoid*) &error->buffer->isRecoverable, 0,
            OCI_ATTR_ERROR_IS_RECOVERABLE, error->handle);
#endif

    // determine length of message since OCI does not provide this information;
    // all encodings except UTF-16 can use normal string processing; cannot use
    // type whar_t for processing UTF-16, though, as its size may be 4 on some
    // platforms, not 2; also strip trailing whitespace from error
    // messages
    if (error->charsetId == DPI_CHARSET_ID_UTF16) {
        numChars = 0;
        utf16chars = (uint16_t*) error->buffer->message;
        bufferChars = sizeof(error->buffer->message) / 2;
        for (i = 0; i < bufferChars; i++) {
            if (utf16chars[i] == 0)
                break;
            if (utf16chars[i] > 127 || !isspace(utf16chars[i]))
                numChars = i + 1;
        }
        error->buffer->messageLength = numChars * 2;
    } else {
        error->buffer->messageLength =
                (uint32_t) strlen(error->buffer->message);
        ptr = error->buffer->message + error->buffer->messageLength - 1;
        while (ptr > error->buffer->message && isspace(*ptr--))
            error->buffer->messageLength--;
    }

    // check for certain errors which indicate that the session is dead and
    // should be dropped from the session pool (if a session pool was used)
    if (conn && !conn->dropSession) {
        switch (error->buffer->code) {
            case    22: // invalid session ID; access denied
            case    28: // your session has been killed
            case    31: // your session has been marked for kill
            case    45: // your session has been terminated with no replay
            case   378: // buffer pools cannot be created as specified
            case   602: // internal programming exception
            case   603: // ORACLE server session terminated by fatal error
            case   609: // could not attach to incoming connection
            case  1012: // not logged on
            case  1041: // internal error. hostdef extension doesn't exist
            case  1043: // user side memory corruption
            case  1089: // immediate shutdown or close in progress
            case  1092: // ORACLE instance terminated. Disconnection forced
            case  2396: // exceeded maximum idle time, please connect again
            case  3113: // end-of-file on communication channel
            case  3114: // not connected to ORACLE
            case  3122: // attempt to close ORACLE-side window on user side
            case  3135: // connection lost contact
            case 12153: // TNS:not connected
            case 12537: // TNS:connection closed
            case 12547: // TNS:lost contact
            case 12570: // TNS:packet reader failure
            case 12583: // TNS:no reader
            case 27146: // post/wait initialization failed
            case 28511: // lost RPC connection
                conn->dropSession = 1;
                break;
        }
    }

    return DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// dpiError__getInfo() [INTERNAL]
//   Get the error state from the error structure. Returns DPI_FAILURE as a
// convenience to the caller.
//-----------------------------------------------------------------------------
int dpiError__getInfo(dpiError *error, dpiErrorInfo *info)
{
    info->code = error->buffer->code;
    info->offset = error->buffer->offset;
    info->message = error->buffer->message;
    info->messageLength = error->buffer->messageLength;
    info->fnName = error->buffer->fnName;
    info->action = error->buffer->action;
    info->isRecoverable = error->buffer->isRecoverable;
    info->encoding = error->buffer->encoding;
    switch(info->code) {
        case 12154: // TNS:could not resolve the connect identifier specified
            info->sqlState = "42S02";
            break;
        case    22: // invalid session ID; access denied
        case   378: // buffer pools cannot be created as specified
        case   602: // Internal programming exception
        case   603: // ORACLE server session terminated by fatal error
        case   604: // error occurred at recursive SQL level
        case   609: // could not attach to incoming connection
        case  1012: // not logged on
        case  1033: // ORACLE initialization or shutdown in progress
        case  1041: // internal error. hostdef extension doesn't exist
        case  1043: // user side memory corruption
        case  1089: // immediate shutdown or close in progress
        case  1090: // shutdown in progress
        case  1092: // ORACLE instance terminated. Disconnection forced
        case  3113: // end-of-file on communication channel
        case  3114: // not connected to ORACLE
        case  3122: // attempt to close ORACLE-side window on user side
        case  3135: // connection lost contact
        case 12153: // TNS:not connected
        case 27146: // post/wait initialization failed
        case 28511: // lost RPC connection to heterogeneous remote agent
            info->sqlState = "01002";
            break;
        default:
            if (error->buffer->code == 0 && error->buffer->dpiErrorNum == 0)
                info->sqlState = "00000";
            else info->sqlState = "HY000";
            break;
    }
    return DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// dpiError__set() [INTERNAL]
//   Set the error buffer to the specified DPI error. Returns DPI_FAILURE as a
// convenience to the caller.
//-----------------------------------------------------------------------------
int dpiError__set(dpiError *error, const char *action, dpiErrorNum errorNum,
        ...)
{
    va_list varArgs;

    error->buffer->code = 0;
    error->buffer->isRecoverable = 0;
    error->buffer->offset = 0;
    strcpy(error->buffer->encoding, DPI_CHARSET_NAME_UTF8);
    error->buffer->action = action;
    error->buffer->dpiErrorNum = errorNum;
    va_start(varArgs, errorNum);
    error->buffer->messageLength = vsnprintf(error->buffer->message,
            sizeof(error->buffer->message),
            dpiErrorMessages[errorNum - DPI_ERR_NO_ERR], varArgs);
    va_end(varArgs);
    return DPI_FAILURE;
}

