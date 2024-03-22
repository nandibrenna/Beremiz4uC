/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rpc_errorhandler, LOG_LEVEL_DBG);
#include "erpc_error_handler.h"
#include <zephyr/kernel.h>

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

extern bool g_erpc_error_occurred;
bool g_erpc_error_occurred = false;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

void erpc_error_handler(erpc_status_t err, uint32_t functionID)
{
    switch (err)
    {
        case kErpcStatus_Fail:
            LOG_ERR("Generic failure");
            break;

        case kErpcStatus_InvalidArgument:
            LOG_ERR("Argument is an invalid value");
            break;

        case kErpcStatus_Timeout:
            LOG_ERR("Operated timed out");
            break;

        case kErpcStatus_InvalidMessageVersion:
            LOG_ERR("Message header contains an unknown version");
            break;

        case kErpcStatus_ExpectedReply:
            LOG_ERR("Expected a reply message but got another message type");
            break;

        case kErpcStatus_CrcCheckFailed:
            LOG_ERR("Message is corrupted");
            break;

        case kErpcStatus_BufferOverrun:
            LOG_ERR("Attempt to read or write past the end of a buffer");
            break;

        case kErpcStatus_UnknownName:
            LOG_ERR("Could not find host with given name");
            break;

        case kErpcStatus_ConnectionFailure:
            LOG_ERR("Failed to connect to host");
            break;

        case kErpcStatus_ConnectionClosed:
            LOG_ERR("Connection closed by peer");
            break;

        case kErpcStatus_MemoryError:
            LOG_ERR("Memory allocation error");
            break;

        case kErpcStatus_ServerIsDown:
            LOG_ERR("Server is stopped");
            break;

        case kErpcStatus_InitFailed:
            LOG_ERR("Transport layer initialization failed");
            break;

        case kErpcStatus_ReceiveFailed:
            LOG_ERR("Failed to receive data");
            break;

        case kErpcStatus_SendFailed:
            LOG_ERR("Failed to send data");
            break;

        /* no error occurred */
        case kErpcStatus_Success:
            return;

        /* unhandled error */
        default:
            LOG_ERR("Unhandled error occurred");
            break;
    }

    /* When error occurred on client side. */
    if (functionID != 0)
    {
        LOG_ERR("Function id '%u'.", (unsigned int)functionID);
    }

    /* error occurred */
    g_erpc_error_occurred = true;
}