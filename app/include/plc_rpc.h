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


#ifndef PLC_RPC_H
#define PLC_RPC_H

char *print_hash(const uint8_t hash[16]);
void add_file_upload(char *filename, struct binary_t *blobID, FILE *file_pointer);
void update_last_blobID(const struct binary_t *blobID);
char* get_filename_from_blobID(const struct binary_t *blobID);
FILE* get_file_pointer_from_blobID(const struct binary_t *blobID);
struct blob_t get_last_blobID();
FILE* is_last_file_pointer_valid();
char *generate_random_filename(char *file_name, char *path);
binary_t blob_to_binary(struct blob_t blob);
uint32_t finish_fileUpload(const binary_t *blobID);
void update_fileUpload(binary_t *blobID);
uint32_t prepare_fileUpload(binary_t *blobID);
bool path_exists(const char *path);
int rmdir(const char *path);
void rpc_server_task(void *, void *, void *);

struct blob_t
{
    uint8_t data[33];
    uint32_t dataLength;
};

struct file_upload
{
    char filename[33];
    struct blob_t blobID;
    FILE *file_pointer;
};
#endif