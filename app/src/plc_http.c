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

/****************************************************************************
#		  Simple, Homemade Web Server with WebSocket Support				#
# 		  ==================================================				#
# 																			#
# This code represents a very simple, homemade web server that supports 	#
# basic WebSocket functionalities. It has been developed with the intent 	#
# to experiment with and demonstrate certain features within a controlled	#
# environment. This server is intentionally kept simple and includes 		#
# minimal to no error handling.												#
# 																			#
# Important Notices:														#
# - The server lacks any form of security measures. No efforts 				#
#   have been made to prevent or mitigate security vulnerabilities.			#
# 																			#
# - Due to the absence of error handling and security measures, this 		#
#   server should never be deployed in production environments or 			#
#   networks that are used for any critical or public purposes.				#
# 																			#
# - The use of this server on the internet or in any networks needed 		#
#   for important tasks is strongly discouraged.							#
# 																			#	
# Disclaimer:																#
# I, the author of this code, take no responsibility for any damages, 		#
# failures, or other issues arising from the use of this server. Use is 	#
# entirely at your own risk. Any liability for the consequences of 			#
# deploying this server is hereby explicitly disclaimed.					#
# 																			#
#  This server is intended solely for demonstration and testing purposes. 	#
#        Please be aware of the risks and use it responsibly.				#
# 																			#
****************************************************************************/

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(plc_http, LOG_LEVEL_DBG);

#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/data/json.h>
#include <mbedtls/sha1.h>
#include <zephyr/sys/base64.h>

#include "config.h"
#include "plc_settings.h"
#include "plc_util.h"
#include "plc_http.h"


#define STATUS_PUBLISH_INTERVAL (1000)			// Time in milliseconds between status updates
#define SOCKET_TIMEOUT (100 * USEC_PER_MSEC) 	// Socket timeout in microseconds

#define HTTP_RESPONSE_TEMPLATE "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n"

extern uint32_t plc_initialized;
extern uint32_t plc_run;

__ccm_noinit_section char receive_buffer[1024];		// Buffer for receiving HTTP requests
__ccm_noinit_section char temp_buffer[1024];	 	// Copy of the buffer, for parsing
__ccm_noinit_section char file_buffer[1024]; 		// Buffer for reading files

// WebSocket opcode definitions
typedef enum
{
	WS_OPCODE_CONTINUATION = 0x0,
	WS_OPCODE_TEXT = 0x1,
	WS_OPCODE_BINARY = 0x2,
	WS_OPCODE_CLOSE = 0x8,
	WS_OPCODE_PING = 0x9,
	WS_OPCODE_PONG = 0xA
} ws_opcode_t;

bool websocket_connected = false; // WebSocket connection status
int websocket_client_fd = -1;	  // File descriptor for the WebSocket client
bool data_to_send = false;		  // Flag to indicate if there's data to send

k_tid_t websocket_thread; // Thread ID for the WebSocket communication thread


/************************************************************************************************************************************/
/* HTTP server thread																												*/
/* This thread handles incoming HTTP requests and upgrades to WebSocket when requested.												*/
/************************************************************************************************************************************/
#define HTTP_SERVER_STACK_SIZE 3072
#define HTTP_SERVER_PRIORITY 5
void plc_http_server(void *, void *, void *);
K_THREAD_DEFINE_CCM(plc_http, HTTP_SERVER_STACK_SIZE, plc_http_server, NULL, NULL, NULL, HTTP_SERVER_PRIORITY, 0, HTTP_SERVER_STARTUP_DELAY);

/************************************************************************************************************************************/
/* WebSocket thread																													*/
/* This thread manages WebSocket communication, including sending and receiving frames.												*/
/************************************************************************************************************************************/
#define WEBSOCKET_SERVER_STACK_SIZE 3072
#define WEBSOCKET_SERVER_PRIORITY 5
void plc_websocket_server(void *, void *, void *);
K_KERNEL_STACK_DEFINE(websocket_thread_stack_, WEBSOCKET_SERVER_STACK_SIZE);
struct k_thread plc_websocket_server_data;

// Structure to hold status information for encoding to JSON
struct status_info
{
	const char *messageType;
	bool plcModuleLoaded;
	const char *loadSource;
	bool plcAutostart;
	const char * startSource;
	bool plcStarted;
	const char *cycleTime;
	const char *ipAddress;
	const char *ipStatus;
	bool rpcServer;
	int rpcServerPort;
	bool modbusServer;
	bool modbusClient;
	bool canOpen;
};

// JSON descriptor for the status_info structure
struct json_obj_descr status_info_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct status_info, messageType, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct status_info, plcModuleLoaded, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct status_info, loadSource, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct status_info, plcAutostart, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct status_info, startSource, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct status_info, plcStarted, JSON_TOK_FALSE),
	JSON_OBJ_DESCR_PRIM(struct status_info, cycleTime, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct status_info, ipAddress, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct status_info, ipStatus, JSON_TOK_STRING), 
	JSON_OBJ_DESCR_PRIM(struct status_info, rpcServer, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct status_info, rpcServerPort, JSON_TOK_NUMBER),
	// JSON_OBJ_DESCR_PRIM(struct status_info, modbusServer, JSON_TOK_TRUE),	 
	// JSON_OBJ_DESCR_PRIM(struct status_info, modbusClient, JSON_TOK_TRUE),
	// JSON_OBJ_DESCR_PRIM(struct status_info, canOpen, JSON_TOK_TRUE),
};

/**
 * @brief Sends a status update over WebSocket.
 */
void send_status_update()
{
	if (websocket_connected && websocket_client_fd >= 0)
	{
		struct status_info status = {
			.messageType = "status",
			.plcModuleLoaded = plc_initialized,
			.loadSource = plc_loader_get_modulename(),
			.plcAutostart = get_plc_autostart_setting(),
			.startSource = plc_loader_get_autostart_source(),
			.plcStarted = plc_run,
			.cycleTime = "-",
			.ipAddress = get_ip_address(),
			.ipStatus = get_ip_assignment_method(),
			.rpcServer = false,
			// .rpcServerPort = CONFIG_RPC_PORT,
			// .modbusServer = false,
			// .modbusClient = false,
			// .canOpen = false,
		};

		ssize_t needed_buf_len = json_calc_encoded_len(status_info_descr, ARRAY_SIZE(status_info_descr), &status);
		if (needed_buf_len < 0) {
			LOG_ERR("Fehler bei der Berechnung der benötigten JSON-Länge");
			return;
		}

		char *json_content = k_malloc((size_t)needed_buf_len + 1);
		if (!json_content)
		{
			LOG_ERR("Failed to allocate memory for json_content");
			return;
		}

		int ret = json_obj_encode_buf(status_info_descr, ARRAY_SIZE(status_info_descr), &status, json_content, (size_t)needed_buf_len + 1);
		if (ret < 0)
		{
			strcpy(json_content, "{\"error\": \"Could not encode JSON\"}");
		}

		send_websocket_message(websocket_client_fd, json_content);
		k_free(json_content);
	}
}


/**
 * @brief Handles a status request by sending JSON-encoded status information.
 *
 * @param client_fd The file descriptor for the client socket.
 */
void handle_status_request(int client_fd)
{
	struct status_info status = {
		.plcModuleLoaded = plc_initialized,
		.plcStarted = plc_run,
		.ipAddress = get_ip_address(),
		.rpcServer = false,
		.modbusServer = false,
		.modbusClient = false,
		.canOpen = false,
	};

	ssize_t needed_buf_len = json_calc_encoded_len(status_info_descr, ARRAY_SIZE(status_info_descr), &status);

	if (needed_buf_len < 0)
	{
		LOG_ERR("Error calculating the required JSON length");
		return;
	}

	char *json_content = k_malloc((size_t)needed_buf_len + 1);
	if (!json_content)
	{
		LOG_ERR("Failed to allocate memory for json_content");
		return;
	}

	int ret = json_obj_encode_buf(status_info_descr, ARRAY_SIZE(status_info_descr), &status, json_content, (size_t)needed_buf_len + 1);
	if (ret < 0)
	{
		const char *error_message = "{\"error\": \"Could not encode JSON\"}";
		strncpy(json_content, error_message, (size_t)needed_buf_len + 1);
	}

	int content_length = strlen(json_content);

	int total_length = snprintf(NULL, 0, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", content_length, json_content) + 1;

	char *response = k_malloc(total_length);
	if (!response)
	{
		LOG_ERR("Failed to allocate memory for response");
		k_free(json_content);
		return;
	}

	snprintf(response, total_length, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", content_length, json_content);

	send(client_fd, response, total_length - 1, 0); 

	k_free(json_content);
	k_free(response);
}

/************************************************************************************************************************************/
/* 													Server part																		*/
/************************************************************************************************************************************/

/**
 * @brief Performs a WebSocket handshake with the client.
 *
 * @param client_fd Socket file descriptor for the client.
 * @param client_key The client's WebSocket key.
 */
void process_websocket_handshake(int client_fd, const char *client_key)
{
	mbedtls_sha1_context ctx;
	unsigned char sha1_output[20];
	char concatenated[100];
	char encoded[32];
	size_t len;

	const char *ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	snprintf(concatenated, sizeof(concatenated), "%s%s", client_key, ws_guid);


	mbedtls_sha1_init(&ctx);
	mbedtls_sha1_starts(&ctx);
	mbedtls_sha1_update(&ctx, (const unsigned char *)concatenated, strlen(concatenated));
	mbedtls_sha1_finish(&ctx, sha1_output);
	mbedtls_sha1_free(&ctx);

	base64_encode((unsigned char *)encoded, sizeof(encoded), &len, sha1_output, sizeof(sha1_output));

	char response[256];
	snprintf(response, sizeof(response),
			 "HTTP/1.1 101 Switching Protocols\r\n"
			 "Upgrade: websocket\r\n"
			 "Connection: Upgrade\r\n"
			 "Sec-WebSocket-Accept: %s\r\n\r\n",
			 encoded);

	send(client_fd, response, strlen(response), 0);
}

/**
 * @brief Sends a WebSocket ping frame to the client.
 *
 * @param client_fd The client's socket file descriptor.
 */
void send_ping_frame(int client_fd)
{
	unsigned char ping_frame[2] = {0x89, 0x00};
	send(client_fd, ping_frame, sizeof(ping_frame), 0);
	LOG_INF("Sent ping frame to client.");
}

/**
 * @brief Sends a WebSocket pong frame to the client.
 *
 * @param client_fd The client's socket file descriptor.
 */
void send_pong_frame(int client_fd)
{
	unsigned char pong_frame[2] = {0x8A, 0x00}; 
	send(client_fd, pong_frame, sizeof(pong_frame), 0);
	LOG_INF("Sent pong frame to client.");
}

/**
 * @brief Sends a WebSocket close frame to the client.
 *
 * @param client_fd The client's socket file descriptor.
 */
void send_close_frame(int client_fd)
{
	unsigned char close_frame[4] = {0x88, 0x02, 0x03, 0xE8}; 

	send(client_fd, close_frame, sizeof(close_frame), 0);
	LOG_INF("Sent close frame to client.");
}

/**
 * @brief Sends a WebSocket text message to the client.
 *
 * @param client_fd The client's socket file descriptor.
 * @param message The message to be sent.
 */
void send_websocket_message(int client_fd, const char *message)
{
	size_t message_length = strlen(message);
	size_t header_length = 2; // Start with minimal header length

	if (message_length <= 125)
	{
		header_length = 2;
	}
	else if (message_length <= 0xFFFF)
	{
		header_length = 4; // 16-bit length field
	}
	else
	{
		header_length = 10; // 64-bit length field
	}

	// Allocate memory for the frame with appropriate header size
	size_t frame_size = message_length + header_length;
	unsigned char *frame = k_malloc(frame_size);

	if (!frame)
	{
		LOG_DBG("Memory allocation failed.");
		return;
	}

	frame[0] = 0x81;

	// Set payload length depending on the message size
	if (message_length <= 125)
	{
		frame[1] = (unsigned char)message_length;
	}
	else if (message_length <= 0xFFFF)
	{
		frame[1] = 126;
		frame[2] = (message_length >> 8) & 0xFF;
		frame[3] = message_length & 0xFF;
	}
	else
	{
		frame[1] = 127;
		// Assume 32-bit platforms for simplicity; adjust for 64-bit if needed
		memset(&frame[2], 0, 4); // Clear the first 4 bytes
		frame[6] = (message_length >> 24) & 0xFF;
		frame[7] = (message_length >> 16) & 0xFF;
		frame[8] = (message_length >> 8) & 0xFF;
		frame[9] = message_length & 0xFF;
	}

	// Copy the message content
	memcpy(&frame[header_length], message, message_length);

	// Send the frame
	ssize_t bytes_sent = send(client_fd, frame, frame_size, MSG_DONTWAIT);
	if (bytes_sent < 0)
	{
		LOG_DBG("Error sending WebSocket message: errno %d", errno);
	}

	k_free(frame); // Free the allocated memory
}

/**
 * @brief Parses a WebSocket frame and performs appropriate actions based on its opcode.
 *
 * @param frame Pointer to the frame data.
 * @param frame_size Size of the frame data.
 * @param client_fd File descriptor for the client socket.
 */
void parse_websocket_frame(unsigned char *frame, size_t frame_size, int client_fd)
{
	if (frame_size < 2)
	{
		LOG_INF("Frame too short for parsing.");
		return;
	}

	uint8_t fin = (frame[0] >> 7) & 0x01;
	uint8_t opcode = frame[0] & 0x0F;
	uint8_t masked = (frame[1] >> 7) & 0x01;
	uint64_t payload_length = frame[1] & 0x7F;
	int pos = 2;

	if (payload_length == 126)
	{
		if (frame_size < 4)
		{
			LOG_INF("Frame too short for extended length.");
			return;
		}
		payload_length = (frame[2] << 8) + frame[3];
		pos += 2;
	}
	else if (payload_length == 127)
	{
		if (frame_size < 10)
		{
			LOG_INF("Frame too short for extended length.");
			return;
		}
		// Caution: Assuming that the system uses Little-Endian
		payload_length = *((uint64_t *)&frame[2]);
		pos += 8;
	}

	if (masked)
	{
		if (frame_size < pos + 4)
		{
			LOG_INF("Frame too short");
			return;
		}
		uint8_t mask[4];
		memcpy(mask, &frame[pos], 4);
		pos += 4;

		for (uint64_t i = 0; i < payload_length; i++)
		{
			frame[pos + i] ^= mask[i % 4];
		}
	}

	LOG_INF("Websocket | Fin: %d, Opcode: %d, Masked: %d, Payload Length: %llu", fin, opcode, masked, payload_length);
	switch (opcode)
	{
	case WS_OPCODE_TEXT:
		LOG_INF("Received a text frame");
		break;
	case WS_OPCODE_BINARY:
		LOG_INF("Received a binary frame");
		break;
	case WS_OPCODE_CLOSE:
		LOG_INF("Received a close frame");
		send_close_frame(client_fd);
		break;
	case WS_OPCODE_PING:
		LOG_INF("Received a ping frame");
		send_pong_frame(client_fd);
		break;
	case WS_OPCODE_PONG:
		LOG_INF("Received a pong frame");
		break;
	default:
		LOG_INF("Received frame with unknown opcode: %x", opcode);
	}
}

/**
 * @brief Handles WebSocket communication for a client socket.
 *
 * @param client_fd The file descriptor for the client socket.
 */
void handle_websocket_communication(int client_fd)
{
	// Stellen Sie den Socket auf Non-blocking
	int flags = fcntl(client_fd, F_GETFL, 0);
	fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

	websocket_connected = true;
	websocket_client_fd = client_fd;

	int64_t last_send_time = k_uptime_get();

	while (websocket_connected)
	{
		fd_set read_fds, write_fds;
		struct timeval timeout;

		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		FD_SET(client_fd, &read_fds);
		FD_SET(client_fd, &write_fds);

		timeout.tv_sec = 0;
		timeout.tv_usec = SOCKET_TIMEOUT;

		int retval = select(client_fd + 1, &read_fds, &write_fds, NULL, &timeout);
		if (retval == -1)
		{
			LOG_ERR("error select()");
			break;
		}
		else if (retval)
		{
			if (FD_ISSET(client_fd, &read_fds))
			{
				unsigned char buffer[1024];
				int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
				if (bytes_received > 0)
				{
					parse_websocket_frame(buffer, bytes_received, client_fd);
				}
				else if (bytes_received == 0)
				{
					LOG_DBG("WebSocket connection closed properly");
					break;
				}
				else
				{
					if (!(errno == EAGAIN || errno == EWOULDBLOCK))
					{
						LOG_DBG("Error receiving data: errno %d", errno);
						break;
					}
				}
			}
		}

		int64_t current_time = k_uptime_get();
		if (current_time - last_send_time >= STATUS_PUBLISH_INTERVAL)
		{
			if (FD_ISSET(client_fd,
						 &write_fds)) 
			{
				send_status_update();
				last_send_time = current_time;
			}
		}
		k_msleep(50);
	}
	close(client_fd);
	websocket_connected = false;
	websocket_client_fd = -1;
}

char *strcasestr(const char *haystack, const char *needle)
{
	if (!*needle)
	{
		return (char *)haystack;
	}
	for (; *haystack; haystack++)
	{
		if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle))
		{
			const char *h, *n;
			for (h = haystack, n = needle; *h && *n; h++, n++)
			{
				if (tolower((unsigned char)*h) != tolower((unsigned char)*n))
				{
					break;
				}
			}
			if (!*n)
			{
				return (char *)haystack;
			}
		}
	}
	return NULL; 
}

/**
 * Checks if an HTTP request is a WebSocket upgrade request.
 *
 * @param request The HTTP request header as a string.
 * @return true if the request is a WebSocket upgrade request, false otherwise.
 */
bool is_websocket_upgrade_request(const char *request)
{
	if (strcasestr(request, "upgrade: websocket") && strcasestr(request, "connection: upgrade"))
	{
		return true;
	}
	return false;
}

/**
 * Extracts the WebSocket key from an HTTP request header.
 *
 * @param request The HTTP request header as a string.
 * @return The WebSocket key if found; otherwise, NULL.
 */
const char *extract_websocket_key(const char *request)
{
	const char *key_header = "Sec-WebSocket-Key: ";
	const char *start = strstr(request, key_header);
	if (start)
	{
		start += strlen(key_header);
		const char *end = strstr(start, "\r\n");
		if (end)
		{
			while (end > start && isspace((unsigned char)*(end - 1)))
			{
				--end;
			}
			static char key[25];
			size_t length = end - start;
			if (length < sizeof(key))
			{
				strncpy(key, start, length);
				key[length] = '\0';
				return key;
			}
		}
	}
	return NULL;
}

/**
 * Gets the MIME type for a given file path based on its extension.
 *
 * @param file_path The path to the file.
 * @return The MIME type as a string.
 */
const char *get_content_type(const char *file_path)
{
	const struct
	{
		const char *extension;
		const char *mime_type;
	} mime_types[] = {
		{".html", "text/html"},		   {".htm", "text/html"},  {".css", "text/css"},   {".js", "application/javascript"},
		{".json", "application/json"}, {".txt", "text/plain"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
		{".png", "image/png"},		   {".gif", "image/gif"}, {".pgs", "text/html"},
	};

	const char *dot = strrchr(file_path, '.');
	if (!dot || dot == file_path)
	{
		return "application/octet-stream"; 
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(mime_types); ++i)
	{
		if (strcmp(dot, mime_types[i].extension) == 0)
		{
			return mime_types[i].mime_type;
		}
	}

	return "application/octet-stream";
}

/**
 * @brief Sends a file to a client over a socket.
 *
 * @param client_fd The file descriptor of the client socket.
 * @param file_path The path to the file to be sent.
 */
void send_file(int client_fd, const char *file_path)
{
	struct stat stat_buf;

	if (stat(file_path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode))
	{
		FILE *file = fopen(file_path, "r");
		if (file != NULL)
		{
			ssize_t nread;

			const char *content_type = get_content_type(file_path);

			char header[512];
			snprintf(header, sizeof(header), HTTP_RESPONSE_TEMPLATE, content_type);
			send(client_fd, header, strlen(header), 0);

			while ((nread = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0)
			{
				send(client_fd, file_buffer, nread, 0);
			}

			fclose(file);
			LOG_DBG("send file %s with content-type %s", file_path, content_type);
		}
		else
		{
			LOG_DBG("cant send file %s, Internal Server Error", file_path);
			const char *response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
								   "text/html\r\n\r\n<h1>500 Internal Server Error</h1>";
			send(client_fd, response, strlen(response), 0);
		}
	}
	else
	{
		LOG_DBG("file %s not found", file_path);
		const char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: "
							   "text/html\r\n\r\n<h1>404 Not Found</h1>";
		send(client_fd, response, strlen(response), 0);
	}
}

/**
 * @brief Entry point for the WebSocket server thread.
 *
 * @param client_fd Pointer to the client socket file descriptor.
 * @param arg2 Unused parameter.
 * @param arg3 Unused parameter.
 */
void plc_websocket_server(void *client_fd, void *arg2, void *arg3)
{

	int websocket_fd = (int)client_fd;
	LOG_DBG("WebSocket-Thread started");

	handle_websocket_communication(websocket_fd);

	close(websocket_fd);
	LOG_DBG("WebSocket thread ended");
}

/**
 * Processes HTTP requests on a client socket. Handles WebSocket upgrades,
 * serves static files, and returns appropriate HTTP responses. Prevents
 * directory traversal and closes the client socket after serving the request.
 *
 * @param client_fd Client socket file descriptor.
 */
void process_request(int client_fd)
{
	int bytes_received = recv(client_fd, receive_buffer, sizeof(receive_buffer), 0);
	if (bytes_received > 0)
	{
		strncpy(temp_buffer, receive_buffer, sizeof(temp_buffer));
		temp_buffer[sizeof(temp_buffer) - 1] = '\0';

		char *method = strtok(temp_buffer, " ");
		char *uri = strtok(NULL, " ");

		if (method && uri && strcmp(method, "GET") == 0)
		{
			if (strcmp(uri, "/ws") == 0)
			{
				if (is_websocket_upgrade_request(receive_buffer))
				{
					const char *websocket_key = extract_websocket_key(receive_buffer);
					if (websocket_key)
					{
						process_websocket_handshake(client_fd, websocket_key);
						websocket_thread = k_thread_create(&plc_websocket_server_data, websocket_thread_stack_, K_THREAD_STACK_SIZEOF(websocket_thread_stack_),
														   plc_websocket_server, (void *)client_fd, NULL, NULL, WEBSOCKET_SERVER_PRIORITY, 0, K_NO_WAIT);
						k_thread_name_set(websocket_thread, "websocket_thread");
						return;
					}
				}
			}
			else
			{
				char file_path[32] = HTTP_ROOT;
				if (strstr(uri, "..") != 0)
				{
					const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Type: "
										   "text/html\r\n\r\n<h1>Bad Request</h1>";
					send(client_fd, response, strlen(response), 0);
					close(client_fd);
					return;
				}
				if (strcmp(uri, "/") == 0)
				{
					strcat(file_path, "index.html");
				}
				else if (strcmp(uri, "status") == 0) 
				{
					LOG_DBG("http status request");
					handle_status_request(client_fd);
					close(client_fd);
					return;
				}
				else
				{
					strcat(file_path, uri);
				}
				send_file(client_fd, file_path);
				close(client_fd);
				return;
			}
		}
	}

	LOG_DBG("http 400 bad request");
	const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Type: "
						   "text/html\r\n\r\n<h1>Bad Request</h1>";
	send(client_fd, response, strlen(response), 0);
}

/**
 * @brief Initializes and runs the HTTP server.
 *
 * This function creates a server socket bound to HTTP_SERVER_PORT and listens
 * for incoming connections. For each connection, it accepts the client socket
 * and processes the request through process_request(). The server runs indefinitely
 * until an error occurs or the system shuts it down.
 *
 * @param arg1 Unused parameter.
 * @param arg2 Unused parameter.
 * @param arg3 Unused parameter.
 */
void plc_http_server(void *, void *, void *)
{
	int server_fd, client_fd;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		return;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(HTTP_SERVER_PORT);

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		LOG_ERR("Error: Bind failed");
		return;
	}

	if (listen(server_fd, 5) < 0)
	{
		LOG_ERR("Error: Listen failed");
		return;
	}

	LOG_INF("HTTP-Server listen on Port %d", HTTP_SERVER_PORT);
	while (true)
	{
		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd >= 0)
		{
			process_request(client_fd);
		}
	}
}
