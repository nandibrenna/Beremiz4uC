/****************************************************************************
#  Project Name: Beremiz 4 uC                                               #
#  Author(s): nandibrenna                                                   #
#  Created: 2024-03-15                                                      #
#  ======================================================================== #
#  Copyright © 2024 nandibrenna                                             #
#                                                                           #
#  Licensed under the Apache License, Version 2.0 (the "License");          #
#  you may not use this file except in compliance with the License.         #
#  You may obtain a copy of the License at                                  #
#                                                                           #
#      http://www.apache.org/licenses/LICENSE-2.0                           #
#                                                                           #
#  Unless required by applicable law or agreed to in writing, software      #
#  distributed under the License is distributed on an "AS IS" BASIS,        #
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or          #
#  implied. See the License for the specific language governing             #
#  permissions and limitations under the License.                           #
#                                                                           #
****************************************************************************/

#ifndef PLC_HTTP_H
#define PLC_HTTP_H

#include <unistd.h>

void send_status_update();
void handle_status_request(int client_fd);
void process_websocket_handshake(int client_fd, const char *client_key);
void send_ping_frame(int client_fd);
void send_pong_frame(int client_fd);
void send_close_frame(int client_fd);
void send_websocket_message(int client_fd, const char *message);
void parse_websocket_frame(unsigned char *frame, size_t frame_size, int client_fd);
void handle_websocket_communication(int client_fd);
bool is_websocket_upgrade_request(const char *request);
const char *extract_websocket_key(const char *request);
const char *get_content_type(const char *file_path);
void send_file(int client_fd, const char *file_path);
void plc_websocket_server(void *client_fd, void *arg2, void *arg3);
void process_request(int client_fd);
void plc_http_server(void *, void *, void *);

char *strcasestr(const char *haystack, const char *needle);

#endif