/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
// nhttp_msg_header.cc author Tom Peters <thopeter@cisco.com>

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>

#include "snort.h"
#include "detection/detection_util.h"

#include "nhttp_enum.h"
#include "nhttp_msg_request.h"
#include "nhttp_msg_header.h"

using namespace NHttpEnums;

NHttpMsgHeader::NHttpMsgHeader(const uint8_t *buffer, const uint16_t buf_size, NHttpFlowData *session_data_,
   SourceId source_id_, bool buf_owner) :
   NHttpMsgHeadShared(buffer, buf_size, session_data_, source_id_, buf_owner)
{
   transaction->set_header(this, source_id);
}

void NHttpMsgHeader::gen_events() {
    NHttpMsgHeadShared::gen_events();
    if (header_count[HEAD_CONTENT_LENGTH] > 1) create_event(EVENT_MULTIPLE_CONTLEN);

}

void NHttpMsgHeader::print_section(FILE *output) {
    NHttpMsgSection::print_message_title(output, "header");
    NHttpMsgHeadShared::print_headers(output);
    NHttpMsgSection::print_message_wrapup(output);
}

void NHttpMsgHeader::update_flow() {
    const uint64_t disaster_mask = 0;

    // The following logic to determine body type is by no means the last word on this topic.
    // FIXIT-H need to distinguish methods such as POST that should have a body from those that should not.
    // FIXIT-H need to support old implementations that don't use Content-Length but just disconnect the connection.
    if (tcp_close) {
        session_data->type_expected[source_id] = SEC_CLOSED;
        session_data->half_reset(source_id);
    }
    else if (infractions & disaster_mask) {
        session_data->type_expected[source_id] = SEC_ABORT;
        session_data->half_reset(source_id);
    }
    else if ((source_id == SRC_SERVER) && ((status_code_num <= 199) || (status_code_num == 204) || (status_code_num == 304))) {
        // No body allowed by RFC for these response codes
        // FIXIT-M inspect for Content-Length and Transfer-Encoding headers which should not be present
        session_data->type_expected[SRC_SERVER] = SEC_STATUS;
        session_data->half_reset(SRC_SERVER);
    }
    else if ((source_id == SRC_SERVER) && (transaction->get_request() != nullptr) &&
             (transaction->get_request()->get_method_id() == METH_HEAD)) {
        // No body allowed by RFC for response to HEAD method
        session_data->type_expected[SRC_SERVER] = SEC_STATUS;
        session_data->half_reset(SRC_SERVER);
    }
    // If there is a Transfer-Encoding header, see if the last of the encoded values is "chunked".
    else if ((get_header_value_norm(HEAD_TRANSFER_ENCODING).length > 0)                     &&
             ((*(int64_t *)(get_header_value_norm(HEAD_TRANSFER_ENCODING).start + 
             (get_header_value_norm(HEAD_TRANSFER_ENCODING).length - 8))) == TRANSCODE_CHUNKED) ) {
        // FIXIT-M inspect for Content-Length header which should not be present
        // Chunked body
        session_data->type_expected[source_id] = SEC_CHUNK;
        session_data->body_octets[source_id] = 0;
    }
    else if ((get_header_value_norm(HEAD_CONTENT_LENGTH).length > 0) &&
            (*(int64_t*)header_value_norm[HEAD_CONTENT_LENGTH].start > 0)) {
        // Regular body
        session_data->type_expected[source_id] = SEC_BODY;
        session_data->data_length[source_id] = *(int64_t*)get_header_value_norm(HEAD_CONTENT_LENGTH).start;
        session_data->body_octets[source_id] = 0;
    }
    else {
        // No body
        session_data->type_expected[source_id] = (source_id == SRC_CLIENT) ? SEC_REQUEST : SEC_STATUS;
        session_data->half_reset(source_id);
    }
}

ProcessResult NHttpMsgHeader::worth_detection() {
    // We can combine with body when sending to detection if the entire body is already available and the combined
    // size does exceed paf_max.
    if ((session_data->type_expected[source_id] == SEC_BODY) &&
        (session_data->data_length[source_id] <= session_data->unused_octets_visible[source_id]) &&
        (session_data->data_length[source_id] <= DATABLOCKSIZE) &&
        (session_data->section_buffer_length[source_id] + msg_text.length + session_data->data_length[source_id] <= MAXOCTETS))
    {
        return RES_AGGREGATE;
    }

    // Do not send empty headers by themselves to detection
    return ((msg_text.length > 0) || (session_data->section_buffer_length[source_id] > 0))
       ? RES_INSPECT : RES_IGNORE;
}

// Legacy support function. Puts message fields into the buffers used by old Snort.
void NHttpMsgHeader::legacy_clients() {
    ClearHttpBuffers();
    legacy_request();
    legacy_status();
    legacy_header(false);
}








