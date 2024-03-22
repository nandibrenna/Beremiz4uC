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

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(plc_rpc, LOG_LEVEL_DBG);

#include <stdio.h>
#include <string.h>

#include <mbedtls/md5.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/random/random.h>
#include <zephyr/net/dns_sd.h>

#include "c_erpc_PLCObject_server.h"
#include "erpc_PLCObject_common.h"
#include "erpc_error_handler.h"
#include "erpc_server_setup.h"
#include "erpc_transport_setup.h"

#include "config.h"
#include "plc_filesys.h"
#include "plc_loader.h"
#include "plc_rpc.h"

mbedtls_md5_context complete_ctx;  					// md5 context over all received chunks
mbedtls_md5_context temporary_ctx; 					// md5 context for actual chunk

extern uint32_t plc_run; 

uint16_t erpc_port = ERPC_SERVER_PORT;

struct file_upload file_uploads[MAX_FILE_UPLOADS]; 	// File uploads
int num_uploads = 0;							   	

int rpc_connection_active = 0;
extern uint32_t reload_plc_file;

extern int32_t plc_logCounts[];

/***************************************************************************************************************************************/
/*		rpc server thread																								     		   */
/***************************************************************************************************************************************/
#define RPC_SERVER_STACK_SIZE 4096
#define RPC_SERVER_PRIORITY 5
extern void rpc_server_task(void *, void *, void *);
K_THREAD_DEFINE_CCM(rpc_server, RPC_SERVER_STACK_SIZE, rpc_server_task, NULL, NULL, NULL, RPC_SERVER_PRIORITY, 0, RPC_SERVER_STARTUP_DELAY);

/***************************************************************************************************************************************/
/*		common plc rpc functions from plcObject																				 		   */
/***************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief 			Retrieves the PLC ID and PSK.
 * 					Allocates memory for and initializes the PLC ID and PSK with test values. This function is designed for initial
 * 					testing purposes and sets static values for the PLC ID and PSK.
 *
 * @param plcID 	Pointer to the PSKID structure where the PLC ID and PSK will be stored.
 * @return 			Returns 0 on success.
 ****************************************************************************************************************************************/
uint32_t GetPLCID(PSKID *plcID)
{
	LOG_INF("GetPLCID");
	const char *plcid_id = PLC_ID;
	const char *plcid_psk = PLC_PSK;
	plcID->ID = (char *)k_malloc(strlen(plcid_id) + 1);
	plcID->PSK = (char *)k_malloc(strlen(plcid_psk) + 1);

	strcpy(plcID->ID, plcid_id);
	strcpy(plcID->PSK, plcid_psk);

	return 0;
}

/****************************************************************************************************************************************
 * @brief 			Gets the current status of the PLC.
 * 					Determines the PLC's status by checking its state and loader state, then stores this status in the provided structure.
 *
 * @param status 	Pointer to a PLCstatus structure to receive the current status.
 * @return 			Returns 0 on success.
 ****************************************************************************************************************************************/
uint32_t GetPLCstatus(PLCstatus *status)
{
	uint32_t plc_state = 0;
	uint32_t plc_loader_state = 0;
	PLCstatus_enum PLCstatus = Broken;

	plc_state = plc_get_state();
	plc_loader_state = plc_get_loader_state();

	if (plc_state == 0)
		PLCstatus = Stopped;
	else if (plc_state == 1)
		PLCstatus = Started;

	if (plc_loader_state == 0)
		PLCstatus = Empty;

	status->logcounts[0] = plc_logCounts[0];
	status->logcounts[1] = plc_logCounts[1];
	status->logcounts[2] = plc_logCounts[2];
	status->logcounts[3] = plc_logCounts[3];
	status->PLCstatus = PLCstatus;
	return 0;
}

/****************************************************************************************************************************************
 * @brief 			Compares a provided MD5 hash with the expected MD5 hash for file integrity verification.
 * 					Reads the MD5 hash stored in a file and compares it with the provided MD5 hash. The comparison result is stored in the match parameter.
 *
 * @param MD5 		The MD5 hash string to be compared.
 * @param match 	Pointer to a boolean where the result of the comparison will be stored (true if matches).
 * @return 			Returns 0 on success.
 ****************************************************************************************************************************************/
uint32_t MatchMD5(const char *MD5, bool *match)
{
	// LOG_INF("MatchMD5 %s", MD5);
	uint32_t result = 0;
	FILE *fp;
	char plc_md5sum[33];

	rpc_connection_active = 1;
	fp = fopen(PLC_MD5_FILE, "r");
	if (fp == NULL)
	{
		LOG_ERR("no md5 file found");
	}
	else
	{
		size_t bytesRead = fread(plc_md5sum, 1, 33, fp);
		if (bytesRead != 33)
		{
			LOG_ERR("error reading from file");
		}
		else
		{
			if (memcmp(plc_md5sum, MD5, 33)) // compare md5
			{
				LOG_ERR("md5 failed");
				*match = false;
				fclose(fp);
				return result;
			}
			else
			{
				LOG_INF("md5 ok");
				*match = true;
				fclose(fp);
				return result;
			}
		}
	}
	*match = false;
	fclose(fp);
	return result;
}

/****************************************************************************************************************************************
 * @brief 			Starts the PLC program.
 * 					Initiates the start sequence of the PLC program. This is a placeholder for actual implementation.
 *
 * @return 			Returns 0 on successful start of the PLC program.
 ****************************************************************************************************************************************/
uint32_t StartPLC(void)
{
	LOG_INF("StartPLC");
	uint32_t result;
	result = 0;
	plc_set_start();
	return result;
}

/****************************************************************************************************************************************
 * @brief 			Stops the PLC program.
 * 					Initiates the stop sequence for the PLC program. This function updates a success flag based on the operation result.
 *
 * @param success 	Pointer to a boolean where the success state of the operation will be stored.
 * @return 			Returns 0 on successful stop of the PLC program.
 ****************************************************************************************************************************************/
uint32_t StopPLC(bool *success)
{
	LOG_INF("StopPLC");
	uint32_t result;
	result = 0;
	*success = true;
	plc_set_stop();
	return result;
}

/****************************************************************************************************************************************
 * @brief 			Repairs the PLC by erasing its program from both the filesystem and the flash partition,
 * 					including the MD5 file and the startup script.
 *
 * @param 	None.
 * @return	uint32_t - Returns the result of the erase operation: 0 for success, non-zero for failure.
 ****************************************************************************************************************************************/
uint32_t RepairPLC(void)
{
	uint32_t result = 0;
	result = erase_plc_programm_flash(1);

	return result;
}

/****************************************************************************************************************************************
 *		file upload helper functions																					    		    *
 ****************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief 			Initializes a file upload structure.
 * 					Sets the filename, blobID data, and blobID data length to initial values and the file pointer to NULL.
 *
 * @param fu 		Pointer to a file_upload structure to be initialized.
 * @return 			None.
 ****************************************************************************************************************************************/
void init_file_upload(struct file_upload *fu)
{
	memset(fu->filename, 0, sizeof(fu->filename));		 
	memset(fu->blobID.data, 0, sizeof(fu->blobID.data)); 
	fu->blobID.dataLength = 0;							 
	fu->file_pointer = NULL;							
}

/****************************************************************************************************************************************
 * @brief 			Initializes the file uploads array.
 * 					Iterates through the file_uploads array and initializes each element using init_file_upload function.
 *
 * @return 			None.
 ****************************************************************************************************************************************/
void init_file_uploads()
{
	for (int i = 0; i < MAX_FILE_UPLOADS; i++)
	{
		init_file_upload(&file_uploads[i]);
	}
}

/****************************************************************************************************************************************
 * @brief 			Adds a new file upload to the uploads list.
 * 					Saves the filename, blobID, and file pointer for a new file upload in the next available slot in the file_uploads array.
 *
 * @param filename 	The name of the file being uploaded.
 * @param blobID 	Pointer to the blobID associated with the file.
 * @param file_pointer The file pointer to the file being uploaded.
 * @return 			None.
 ****************************************************************************************************************************************/
void add_file_upload(char *filename, struct binary_t *blobID, FILE *file_pointer)
{
	if (num_uploads < MAX_FILE_UPLOADS)
	{
		strncpy(file_uploads[num_uploads].filename, filename, 32);
		file_uploads[num_uploads].filename[32] = '\0';
		memcpy(file_uploads[num_uploads].blobID.data, blobID->data, blobID->dataLength);
		file_uploads[num_uploads].blobID.dataLength = blobID->dataLength;
		file_uploads[num_uploads].file_pointer = file_pointer;
		num_uploads++;
	}
}

/****************************************************************************************************************************************
 * @brief 			Updates the last blobID in the file uploads list.
 * 					Replaces the blobID data and data length of the most recent file upload entry with new values.
 *
 * @param blobID 	Pointer to the new blobID to replace the old one.
 * @return 			None.
 ****************************************************************************************************************************************/
void update_last_blobID(const struct binary_t *blobID)
{
	if (num_uploads > 0)
	{
		memcpy(file_uploads[num_uploads - 1].blobID.data, blobID->data, blobID->dataLength);
		file_uploads[num_uploads - 1].blobID.dataLength = blobID->dataLength;
	}
}

/****************************************************************************************************************************************
 * @brief 			Retrieves the filename associated with a given blobID.
 * 					Searches the file uploads list for a matching blobID and returns the associated filename.
 *
 * @param blobID 	Pointer to the blobID structure used to find the corresponding filename.
 * @return 			Returns the filename associated with the given blobID or NULL if not found.
 ****************************************************************************************************************************************/
char *get_filename_from_blobID(const struct binary_t *blobID)
{
	for (int i = 0; i < num_uploads; i++)
	{
		if (memcmp(&file_uploads[i].blobID.data, blobID->data, blobID->dataLength) == 0 && file_uploads[i].blobID.dataLength == blobID->dataLength)
		{
			return file_uploads[i].filename;
		}
	}
	return NULL;
}

/****************************************************************************************************************************************
 * @brief 			Retrieves the file pointer associated with a given blobID.
 * 					Searches the file uploads list for a matching blobID and returns the associated file pointer.
 *
 * @param blobID 	Pointer to the blobID structure used to find the corresponding file pointer.
 * @return 			Returns the file pointer associated with the given blobID or NULL if not found.
 ****************************************************************************************************************************************/
FILE *get_file_pointer_from_blobID(const struct binary_t *blobID)
{
	for (int i = 0; i < num_uploads; i++)
	{
		if (memcmp(file_uploads[i].blobID.data, blobID->data, blobID->dataLength) == 0 && file_uploads[i].blobID.dataLength == blobID->dataLength)
		{
			return file_uploads[i].file_pointer;
		}
	}
	return NULL; // Datei nicht gefunden
}

/****************************************************************************************************************************************
 * @brief 			Retrieves the blobID of the last uploaded file.
 * 					Returns the blobID of the most recently uploaded file by accessing the last entry in the file uploads list.
 *
 * @return 			Returns the blobID of the last uploaded file or an empty blobID if no files have been uploaded.
 ****************************************************************************************************************************************/
struct blob_t get_last_blobID()
{
	struct blob_t empty_blobID = {{0}, 0};

	if (num_uploads > 0)
	{
		return file_uploads[num_uploads - 1].blobID;
	}

	return empty_blobID; // when no file is uploaded, return empty blobID
}

/****************************************************************************************************************************************
 * @brief 			Checks if the last file pointer in the uploads list is valid.
 * 					Verifies whether the file pointer of the most recent file upload entry is not NULL, indicating an ongoing file upload.
 *
 * @return 			Returns the file pointer if valid, otherwise NULL.
 ****************************************************************************************************************************************/
FILE *is_last_file_pointer_valid()
{
	if (num_uploads > 0 && file_uploads[num_uploads - 1].file_pointer != NULL)
	{
		return file_uploads[num_uploads - 1].file_pointer;
	}
	return NULL;
}

/****************************************************************************************************************************************
 * @brief 			Generates a random filename for a temporary file.
 * 					Creates a random filename using a specified character set. The filename is then appended to the provided path.
 *
 * @param file_name Buffer to store the generated filename.
 * @param path 		Path where the temporary file will be located.
 * @return 			Returns the generated filename with path.
 ****************************************************************************************************************************************/
char *generate_random_filename(char *file_name, char *path)
{
	const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; // Zeichensatz für den Dateinamen
	int path_length = strlen(path);
	if (path_length > 0 && path[path_length - 1] != '/')
	{
		strcat(file_name, path);
		strcat(file_name, "/");
	}
	else
	{
		strcat(file_name, path);
	}

	for (int i = 0; i < MAX_FILENAME_LENGTH; i++)
	{
		file_name[path_length + i] = charset[sys_rand32_get() % (sizeof(charset) - 1)];
	}
	file_name[path_length + MAX_FILENAME_LENGTH] = '\0';

	strcat(file_name, TMP_FILE_EXTENSION);

	return file_name;
}

/****************************************************************************************************************************************
 * @brief 		Converts a blob_t structure to a binary_t structure.
 * 				Allocates memory for and copies the data from a blob_t structure to a newly created binary_t structure.
 *
 * @param blob 	The blob_t structure to be converted.
 * @return 		Returns the newly created binary_t structure.
 ****************************************************************************************************************************************/
binary_t blob_to_binary(struct blob_t blob)
{
	binary_t binary;

	binary.data = (uint8_t *)k_malloc(blob.dataLength);
	memcpy(binary.data, blob.data, blob.dataLength);
	binary.dataLength = blob.dataLength;

	return binary;
}

/****************************************************************************************************************************************
 * @brief 		Finalizes the upload process for a file.
 * 				Closes the file associated with the given blobID and updates the blobID in the file uploads list.
 *
 * @param blobID Pointer to the blobID of the file to finalize.
 * @return 		Returns 0 on success, 1 if the file pointer is NULL or the file cannot be closed.
 ****************************************************************************************************************************************/
uint32_t finish_fileUpload(const binary_t *blobID)
{
	update_last_blobID(blobID); // update the last blobID for file
	FILE *fp = get_file_pointer_from_blobID(blobID);
	if (fp != NULL)
	{
		fclose(fp);
		return 0;
	}
	return 1;
}

/****************************************************************************************************************************************
 * @brief 		Updates the blobID for the current file upload.
 * 				Updates the blobID of the most recent file upload entry with a new blobID.
 *
 * @param blobID Pointer to the new blobID to be used.
 * @return 		None.
 ****************************************************************************************************************************************/
void update_fileUpload(binary_t *blobID) { update_last_blobID(blobID); }

/****************************************************************************************************************************************
 * @brief 		Prepares for a new file upload by checking and finalizing any ongoing upload.
 * 				Finalizes any ongoing file upload and starts a new upload process by generating a random filename and opening a new file.
 *
 * @param blobID Pointer to the blobID for the new file upload.
 * @return 		Returns 0 on success, other error codes for specific failures.
 ****************************************************************************************************************************************/
uint32_t prepare_fileUpload(binary_t *blobID)
{
	char filename[33] = {0};

	FILE *fp = is_last_file_pointer_valid(); // check if there is actually a file upload
	if (fp != NULL)
	{
		struct blob_t lastBlobID = get_last_blobID();
		binary_t lastBlob = blob_to_binary(lastBlobID);
		finish_fileUpload(&lastBlob); // finish the last upload before starting new
		k_free(lastBlob.data);
	}

	generate_random_filename(filename, TMP_FILE_PATH); // begin new file upload
	fp = fopen(filename, "wb");
	add_file_upload(filename, blobID, fp); // add upload to upload list
	return 0;
}

/****************************************************************************************************************************************
 * @brief 		Deletes all temporary files in a specified directory.
 * 				Iterates through a directory and deletes all files with a ".tmp" extension.
 *
 * @param directory Path to the directory from which temporary files should be deleted.
 * @return 		None.
 ****************************************************************************************************************************************/
void delete_tmp_files(const char *directory)
{
	struct fs_dir_t dirp;
	fs_dir_t_init(&dirp);
	struct fs_dirent entry;
	char file_path[256];

	if (fs_opendir(&dirp, directory) != 0)
	{
		LOG_INF("Failed to open directory %s", directory);
		return;
	}

	while (fs_readdir(&dirp, &entry) == 0 && entry.name[0] != '\0')
	{
		if (entry.type == FS_DIR_ENTRY_FILE)
		{
			if (strstr(entry.name, ".tmp") != NULL)
			{
				snprintf(file_path, sizeof(file_path), "%s/%s", directory, entry.name);
				if (fs_unlink(file_path) != 0)
				{
					LOG_INF("Failed to delete file %s", file_path);
				}
				else
				{
					LOG_INF("Deleted file %s", file_path);
				}
			}
		}
	}

	fs_closedir(&dirp);
}

/***************************************************************************************************************************************/
/* 		rpc file upload functions																					 		   		   */
/***************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief 	Deletes all old file blobs and reinitializes the file uploads array.
 * 			This function is called to clear out any old file blobs from the temporary file directory and to reinitialize the file uploads
 * 			array, effectively resetting the file upload environment.
 *
 * @return Returns 0 on success.
 ****************************************************************************************************************************************/
uint32_t PurgeBlobs(void)
{
	LOG_INF("starting file upload");
	uint32_t result = 0;
	delete_tmp_files(TMP_FILE_PATH);
	init_file_uploads();
	return result;
}

/****************************************************************************************************************************************
 * @brief 	Starts the file upload process by initializing MD5 contexts and preparing for file upload.
 * 			Initializes the MD5 contexts for a complete file and for the current chunk. Also, prepares for a new file upload by allocating
 * 			memory for the blobID and setting up the file upload infrastructure.
 *
 * @param seed Pointer to the initial data chunk (seed) for the file being uploaded.
 * @param blobID Pointer to a structure where the blobID for the new file will be stored.
 * @return Returns 0 on success, 1 on invalid parameters, and other error codes for specific failures.
 ****************************************************************************************************************************************/
uint32_t SeedBlob(const binary_t *seed, binary_t *blobID)
{
	uint32_t result = 0;
	mbedtls_md5_init(&complete_ctx);
	mbedtls_md5_starts(&complete_ctx);
	mbedtls_md5_init(&temporary_ctx);
	mbedtls_md5_starts(&temporary_ctx);

	if (seed == NULL || blobID == NULL)
	{
		LOG_ERR("SeedBlob Invalid parameters");
		return 1; // Error: Invalid parameters
	}

	blobID->data = k_malloc(16); // Allocate memory for MD5 hash (16 bytes)
	if (blobID->data == NULL)
	{
		LOG_ERR("SeedBlob Memory allocation failed");
		return 1; // Error: Memory allocation failed
	}

	mbedtls_md5_update(&temporary_ctx, seed->data, seed->dataLength);
	mbedtls_md5_update(&complete_ctx, seed->data, seed->dataLength);
	mbedtls_md5_finish(&temporary_ctx, blobID->data);
	blobID->dataLength = 16;

	prepare_fileUpload(blobID);
	return result;
}

/****************************************************************************************************************************************
 * @brief 	Appends a data chunk to the current file blob and updates the MD5 hash.
 * 			This function updates the complete file's MD5 context with the new data chunk, calculates the new MD5 hash, writes the data to
 * 			the file, and updates the file upload list with the new blobID.
 *
 * @param data Pointer to the data chunk being uploaded.
 * @param blobID Pointer to the current blobID associated with the file.
 * @param newBlobID Pointer to a structure where the new blobID will be stored after appending the chunk.
 * @return Returns 0 on success, with various error codes for specific failures.
 ****************************************************************************************************************************************/
uint32_t AppendChunkToBlob(const binary_t *data, const binary_t *blobID, binary_t *newBlobID)
{
	uint32_t result = 0;
	uint8_t actual_md5[16];

	if (data == NULL || blobID == NULL || newBlobID == NULL)
	{
		LOG_ERR("Invalid parameters");
		return 2; // Error: Invalid parameters
	}

	// Allocate memory for newBlobID
	newBlobID->data = k_malloc(16); // Allocate memory for MD5 hash (16 bytes)
	if (newBlobID->data == NULL)
	{
		LOG_ERR("Memory allocation failed");
		return 1; // Error: Memory allocation failed
	}

	// Calculate MD5 hash of the concatenated data
	mbedtls_md5_update(&complete_ctx, data->data, data->dataLength);
	memcpy(&temporary_ctx, &complete_ctx, sizeof(mbedtls_md5_context));
	mbedtls_md5_finish(&temporary_ctx, actual_md5);
	memcpy(newBlobID->data, actual_md5, 16); // copy md5 hash
	newBlobID->dataLength = 16;				 // MD5 produces a 128-bit hash (16 bytes)
	update_fileUpload(newBlobID);

	// Write data to file
	FILE *file = get_file_pointer_from_blobID(newBlobID);
	if (file == NULL)
	{
		LOG_ERR("Failed to open file");
		return 3; // Error: Failed to open file
	}
	size_t bytesWritten = fwrite(data->data, sizeof(uint8_t), data->dataLength, file);
	if (bytesWritten != data->dataLength)
	{
		LOG_ERR("Failed to write %u bytes to file #%u (wrote %u)", data->dataLength, file->_file, bytesWritten);
		return 4; // Error: Failed to write data to file
	}
	// LOG_INF("write 0x%03x bytes to file #%u ", bytesWritten, file->_file, bytesWritten);
	fflush(file);
	// LOG_INF("flushed file #%u", file->_file);
	return result;
}

/****************************************************************************************************************************************
 * @brief 	Finalizes the PLC file upload and verifies the file integrity using MD5 checksum.
 * 			Finishes the file upload process by comparing the final MD5 checksum of the uploaded file against the provided checksum. If the
 * 			checksums match, it proceeds to update the PLC program with the uploaded file.
 *
 * @param md5sum The expected MD5 checksum of the complete file.
 * @param plcObjectBlobID Pointer to the blobID of the uploaded PLC object file.
 * @param extrafiles Pointer to a list of additional files that were uploaded.
 * @param success Pointer to a boolean that indicates the success of the operation.
 * @return Returns 0 on success, with the success parameter indicating whether the file was successfully updated.
 ****************************************************************************************************************************************/
uint32_t NewPLC(const char *md5sum, const binary_t *plcObjectBlobID, const list_extra_file_1_t *extrafiles, bool *success)
{
	// LOG_INF("NewPLC");
	uint8_t file_md5[16];
	struct fs_dirent dirent;
	struct fs_file_t file;
	fs_file_t_init(&file);

	finish_fileUpload(plcObjectBlobID);										  // finish the upload
	mbedtls_md5_finish(&complete_ctx, file_md5);							  // get final md5sum
	if (memcmp(file_md5, plcObjectBlobID->data, plcObjectBlobID->dataLength)) // compare with md5 calculated from upload
	{
		LOG_ERR("md5 mismatch, file upload error");
		*success = false;
		return 0;
	}
	else
	{
		LOG_INF("md5 match, file uploaded succesfull");
		plc_run = 0;

		char *plc_tmp_file = get_filename_from_blobID(plcObjectBlobID);

		int rc = fs_stat(PLC_MD5_FILE, &dirent);
		if (rc == 0)
			fs_unlink(PLC_BIN_FILE); // remove PLC_BIN_FILE

		if (fs_rename(plc_tmp_file, PLC_BIN_FILE) == 0)
		{
			; // LOG_INF("set uploaded file %s as plc start file %s", plc_tmp_file, PLC_BIN_FILE);
		}
		else
		{
			LOG_ERR("cant set uploaded file %s as plc start file %s", plc_tmp_file, PLC_BIN_FILE);
		}

		// LOG_INF("Extra Files: %u", extrafiles->elementsCount);
		for (uint32_t i = 0; i < extrafiles->elementsCount; ++i)
		{
			binary_t extra_files_blob;
			char ex_file_name[48] = {0};
			extra_files_blob.data = NULL;
			extra_files_blob.data = (uint8_t *)k_malloc(extrafiles->elements[i].blobID.dataLength);
			if (extra_files_blob.data == NULL)
			{
				LOG_ERR("error allocating memory for extra_files");
				*success = false;
				return 0;
			}

			extra_files_blob = extrafiles->elements[i].blobID;
			char *ex_tmp_file = get_filename_from_blobID(&extra_files_blob);

			const char *fileExtension = strrchr(extrafiles->elements[i].fname, '.');
			bool isWebFile = fileExtension &&
							 (strcmp(fileExtension, ".htm") == 0 || 
							 strcmp(fileExtension, ".html") == 0 || 
							 strcmp(fileExtension, ".js") == 0 || 
							 strcmp(fileExtension, ".json") == 0 || 
							 strcmp(fileExtension, ".css") == 0);

			const char *destinationPath = isWebFile ? HTTP_ROOT "/" : PLC_ROOT_PATH;
			strcat(ex_file_name, destinationPath);
			strcat(ex_file_name, extrafiles->elements[i].fname);
			if (fs_rename(ex_tmp_file, ex_file_name) != 0)
			{
				LOG_ERR("cant rename uploaded file %s to %s", ex_tmp_file, ex_file_name);
				*success = false;
				return 0;
			}
		}
		rc = fs_stat(PLC_MD5_FILE, &dirent);
		if (rc == 0)
			fs_unlink(PLC_MD5_FILE); // remove PLC_MD5_FILE

		// Write md5sum to md5 file
		FILE *fp_md5 = fopen(PLC_MD5_FILE, "wb");
		if (fp_md5 == NULL)
		{
			LOG_ERR("Failed to open md5 file %s", PLC_MD5_FILE);
			*success = false;
			return 0;
		}

		size_t bytesWritten = fwrite(md5sum, sizeof(uint8_t), 33, fp_md5);
		if (bytesWritten != 33)
		{
			LOG_ERR("Failed to write data to md5 file");
			*success = false;
			return 0;
		}
		fclose(fp_md5);

		reload_plc_file = 1; // let plc_loader reload plc module
		LOG_INF("file upload finished");
		*success = true;
		return 0;
	}
	*success = false;
	return 0;
}

/***************************************************************************************************************************************/
/*		rte debugging 																										 		   */
/***************************************************************************************************************************************/

//
// see file plc_debug.c
//

/***************************************************************************************************************************************/
/*		rpc server thread																								     		   */
/***************************************************************************************************************************************/
void rpc_server_task(void *, void *, void *)
{
	// MDNS service
	static const char *beremiz_txt[] = {
		"description=Beremiz remote PLC",
		"protocol=ERPC",
		NULL,
		};
	DNS_SD_REGISTER_TCP_SERVICE(
		beremiz_service, 
		CONFIG_NET_HOSTNAME, 
		"_Beremiz", "local", 
		beremiz_txt, 
		ERPC_SERVER_PORT
		);

	while (true)
	{
		/* TCP transport layer initialization */
		LOG_INF("TCP transport layer initialization");
		erpc_transport_t transport = erpc_transport_tcp_zephyr_init(INADDR_ANY, erpc_port, true);
		if (transport == NULL)
		{
			LOG_ERR("Failed to initialize TCP transport");
			continue; // Skip this iteration to attempt a restart
		}

		/* MessageBufferFactory initialization */
		LOG_INF("MessageBufferFactory initialization");
		erpc_mbf_t message_buffer_factory = erpc_mbf_dynamic_init();
		if (message_buffer_factory == NULL)
		{
			LOG_ERR("Failed to initialize MessageBufferFactory");
			erpc_transport_tcp_zephyr_deinit(transport); // Clean up transport before restart
			continue; // Attempt restart
		}

		/* eRPC server side initialization */
		LOG_INF("eRPC server side initialization");
		erpc_server_t server = erpc_server_init(transport, message_buffer_factory);
		if (server == NULL)
		{
			LOG_ERR("Failed to initialize eRPC server");
			erpc_mbf_dynamic_deinit(message_buffer_factory); // Clean up message buffer factory
			erpc_transport_tcp_zephyr_deinit(transport); // Clean up transport
			continue; // Attempt restart
		}

		/* Adding the service to the server */
		LOG_INF("Adding the service to the server");
		erpc_service_t service = create_BeremizPLCObjectService_service();
		if (service == NULL)
		{
			LOG_ERR("Failed to create service");
			erpc_server_deinit(server); // Clean up server
			erpc_mbf_dynamic_deinit(message_buffer_factory); // Clean up message buffer factory
			erpc_transport_tcp_zephyr_deinit(transport); // Clean up transport
			continue; // Attempt restart
		}
		erpc_add_service_to_server(server, service);

		/* Server loop for processing messages */
		while (true)
		{
			/* Process message */
			erpc_status_t status = erpc_server_poll(server);

			/* Handle non-successful status */
			if (status != kErpcStatus_Success && status != kErpcStatus_ConnectionClosed)
			{
				LOG_ERR("Error status %u", status);
				erpc_error_handler(status, 0); // Handle the error

				/* Cleanup and break from server loop for restart */
				erpc_remove_service_from_server(server, service);
				destroy_BeremizPLCObjectService_service(service);
				erpc_server_stop(server);
				erpc_server_deinit(server);
				erpc_mbf_dynamic_deinit(message_buffer_factory);
				erpc_transport_tcp_zephyr_deinit(transport);
				break; // Exit the server loop for restart
			}

			/* Check if the connection was closed */
			if (status == kErpcStatus_ConnectionClosed)
			{
				LOG_INF("Connection to client closed");
				rpc_connection_active = 0; // Update connection status
			}

			k_msleep(10); // Sleep to reduce CPU usage
		}
		LOG_ERR("The RPC server was terminated due to an error. Try restarting the RPC server...");
	}
}
