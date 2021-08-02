////////////////////////////////////////////////////////////////////////////////
//
// Created by Daniel Carrasco at https://www.electrosoftcloud.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#include "lzlib4.h"
#include <stdlib.h>
#include <string.h>
#include <iostream>


/**
 * @brief : Initialize the stream decompression state to keep the buffers and the state
 * 
 * @return int : returns 0 if all was right, negative number otherwise.
 */
lzlib4::lzlib4(){
    // Input data
    strm.next_in = NULL;
    strm.avail_in = 0;

    // Output data
    strm.next_out = NULL;
    strm.avail_out = 0;

    // Initializing the LZ4HC stream
    strm.state.strm_lz4_decode = LZ4_createStreamDecode();
    if (!strm.state.strm_lz4_decode) {
        //throw std::runtime_error("Error initializing LZ4 compressor.");
    }
}


/**
 * @brief : Initialize the stream compression state to keep the buffers
 * 
 * @param block_size : wanted block size
 * @param block_mode : defines if block will be filled with data splitting input data, or entire input data will be kept in same block, Defaults LZLIB4_PARTIAL_CONTENT
 * @param comp_level : LZ4HC compression level (1-13). Defaults to LZ4HC_CLEVEL_DEFAULT
 * @return int : returns 0 if all was right, negative number otherwise.
 */
lzlib4::lzlib4(
    size_t block_size,
    lzlib4_block_mode block_mode,
    int8_t comp_level
){
    // Limit the block size to avoid to have a very big buffers.
    if (block_size > LZLIB4_MAX_BLOCK_SIZE) {
        //
    }
    // Input data
    strm.next_in = NULL;
    strm.avail_in = 0;

    // Output data
    strm.next_out = NULL;
    strm.avail_out = 0;

    // Initializing the compression buffer
    strm.state.compress_in_size = block_size;
    strm.state.compress_in_buffer = (uint8_t*) malloc(strm.state.compress_in_size);
    strm.state.compress_in_index = 0;
    strm.state.compress_out_size = LZ4_COMPRESSBOUND(strm.state.compress_in_size) + sizeof(LZLIB4_BLOCK_HEADER); // Worst case
    strm.state.compress_out_buffer = (uint8_t*) malloc(strm.state.compress_out_size);
    
    strm.state.compress_block_mode = block_mode;

    // Initializing the LZ4HC stream
    strm.state.strm_lz4 = LZ4_createStreamHC();
    if (!strm.state.strm_lz4) {
        //throw std::runtime_error("Error initializing LZ4 compressor.");
    }
    // Set the block compression
    LZ4_resetStreamHC(strm.state.strm_lz4, comp_level);

    compression_level = comp_level;
}

lzlib4::~lzlib4() {
    close();
}

int lzlib4::compress(lzlib4_flush_mode flush_mode) {
    if (strm.state.compress_block_mode == LZLIB4_INPUT_NOSPLIT && strm.avail_in > strm.state.compress_in_size) {
        // FULL data mode is selected and block is bigger than block size.
        //strm.msg = "Input size is bigger than block size.";
        return LZLIB4_RC_BLOCK_SIZE_ERROR;
    }

    // While there is data in input buffer, create blocks
    while (strm.avail_in || flush_mode) {
        // Only compress if the buffer is filled or flush_mode is LZLIB4_FULL_FLUSH
        bool to_compress = false;
        // Free space in input buffer
        size_t space_left = strm.state.compress_in_size - strm.state.compress_in_index;
        // Size of the data that will be readed
        size_t to_read = 0;

        // If available data doesn't fit the current block and block mode is LZLIB4_INPUT_NOSPLIT, then compress and free the buffer
        if (strm.state.compress_block_mode == LZLIB4_INPUT_NOSPLIT && strm.avail_in > space_left) {
            to_compress = true;
        }
        // Else, fill the buffer or read all the data if it fits in the buffer
        else {
            to_read = std::min(space_left, strm.avail_in);
        }

        // We have to read data from input buffer
        if (to_read) {
            // Read the data to the compression buffer
            memcpy(strm.state.compress_in_buffer + strm.state.compress_in_index, strm.next_in, to_read);
            // Update the index, pointers and sizes...
            strm.next_in += to_read;
            strm.avail_in -= to_read;
            strm.state.compress_in_index += to_read;
        }

        // If input buffer is filled or there is no more data with any flush mode, compress the block.
        if (strm.state.compress_in_index > strm.state.compress_in_size) {
            // in index should not be bigger than size
            return LZLIB4_RC_BUFFER_ERROR;
        }
        else if (
            (strm.state.compress_in_index == strm.state.compress_in_size) ||
            (strm.avail_in == 0 && flush_mode > 0)
        ) {
            to_compress = true;
            // Remove the flush mode to exit the loop at end
            flush_mode = LZLIB4_NO_FLUSH;
        }

        // If block is ready to compress, then compress it
        if (to_compress) {
            // A new block will be created
            size_t compressed = LZ4_compress_HC_continue(
                strm.state.strm_lz4,
                (char *) strm.state.compress_in_buffer,
                (char *) strm.state.compress_out_buffer,
                strm.state.compress_in_index,
                strm.state.compress_out_size
            );

            // If data was compressed then write the output buffer
            if (compressed) {
                // If output buffer is too small, raise an error
                if ((compressed + sizeof(LZLIB4_BLOCK_HEADER)) > strm.avail_out) {
                    return LZLIB4_RC_BUFFER_ERROR;
                }

                // Calculate the CRC, which will allow to check the block later and will be used as Identifier (is important)
                uint32_t crc = crc32(strm.state.compress_in_buffer, strm.state.compress_in_index);

                // Add block header
                LZLIB4_BLOCK_HEADER header = {
                    (uint32_t) compressed, // compressed_size
                    (uint32_t) strm.state.compress_in_index, // uncompressed_size
                    crc // CRC
                };
                memcpy(strm.next_out, &header, sizeof(header));
                // Set the new pointer position and available space
                strm.next_out += sizeof(header);
                strm.avail_out -= sizeof(header);

                // Copy the compressed block to the output buffer
                memcpy(strm.next_out, strm.state.compress_out_buffer, compressed);
                // Set the new pointer position and available space
                strm.next_out += compressed;
                strm.avail_out -= compressed;
                // Reset the input index
                strm.state.compress_in_index = 0;
            }
            else {
                return LZLIB4_RC_COMPRESSION_ERROR;
            }
        }
    }

    /* Flush mode was set to FINISH, so stream state will be reset */
    if (flush_mode == LZLIB4_FINISH){
        LZ4_resetStreamHC(strm.state.strm_lz4, compression_level);
    }

    return 0;
}


int lzlib4::decompress(bool check_crc) {
    LZLIB4_BLOCK_HEADER header;

    while (strm.avail_in) {
        bool to_decompress = false;
        size_t to_read = 0;

        // If block is not a partial block
        if (!strm.partial_block) {
            // Read the block header
            memcpy(&header, strm.next_in, sizeof(header));

            // Check if header is damaged and any of the sizes is 0
            if (!header.compressed_size || !header.uncompressed_size || !header.crc) {
                printf("There is no size or crc\n");
                return LZLIB4_RC_BLOCK_DAMAGED;
            }

            // Check if compressed/uncompressed size is too high (possible corrupted header)
            if (header.compressed_size > LZ4_COMPRESSBOUND(LZLIB4_MAX_BLOCK_SIZE) || header.uncompressed_size > LZLIB4_MAX_BLOCK_SIZE) {
                return LZLIB4_RC_BLOCK_DAMAGED;
            }

            // Output Buffer is smaller than the block size
            if (header.uncompressed_size > strm.avail_out) {
                // Compressed stream doesn't fit the output buffer, so an error is returned
                return LZLIB4_RC_BUFFER_ERROR;
            }

            //
            // Create required buffers
            //
            // If the compressed block size is bigger than the decompression input buffer,
            // create a bigger buffer.
            if (header.compressed_size > strm.state.decompress_in_size_real) {
                // Free the old buffer if exists
                if (strm.state.decompress_in_buffer) {
                    free(strm.state.decompress_in_buffer);
                }
                // And create a new one
                strm.state.decompress_in_buffer = (uint8_t*) malloc(header.compressed_size);

                if (!strm.state.decompress_in_buffer) {
                    return LZLIB4_RC_BUFFER_ERROR;
                }

                strm.state.decompress_in_size_real = header.compressed_size;
            }

            // If the decompressed block size is bigger than the decompression output buffer,
            // create a bigger buffer.
            if (header.uncompressed_size > strm.state.decompress_out_size_real) {
                // Free the old buffer if exists
                if (strm.state.decompress_out_buffer) {
                    free(strm.state.decompress_out_buffer);
                }
                // And create a new one
                strm.state.decompress_out_buffer = (uint8_t*) malloc(header.uncompressed_size);

                if (!strm.state.decompress_out_buffer) {
                    return LZLIB4_RC_BUFFER_ERROR;
                }

                strm.state.decompress_out_size_real = header.uncompressed_size;
            }

            // Set the block sizes
            strm.state.decompress_in_size = header.compressed_size;
            strm.state.decompress_out_size = header.uncompressed_size;

            // Started to process a block.
            strm.partial_block = true;
            strm.next_in += sizeof(header);
            strm.avail_in -= sizeof(header);
        }

        // Check the space left in input buffer
        size_t space_left = strm.state.decompress_in_size - strm.state.decompress_in_index;
        // If there is space, set to_read
        if (space_left) {
            to_read = std::min(space_left, strm.avail_in);
        }
        // Else means that block is complete, so decompress it
        else {
            to_decompress = true;
        }

        // We need to read more data to fill the block buffer
        if (to_read) {
            memcpy(strm.state.decompress_in_buffer + strm.state.decompress_in_index, strm.next_in, to_read); // Copy the data
            strm.next_in += to_read; // Move the pointer to the new position
            strm.avail_in -= to_read; // Set the new available size
            strm.state.decompress_in_index += to_read; // Set the new buffer index

            // Check if now the block is full to decompress it
            if (strm.state.decompress_in_index > strm.state.decompress_in_size) {
                // in index should not be bigger than size (internal error)
                return LZLIB4_RC_BUFFER_ERROR;
            }
            else if (strm.state.decompress_in_index == strm.state.decompress_in_size) {
                // In buffer was filled so is ready to decompress a block
                to_decompress = true;
            }
        }

        if (to_decompress) {
            // Block is full so no more data is required
            size_t decompressed = LZ4_decompress_safe_continue(
                strm.state.strm_lz4_decode,
                (char *) strm.state.decompress_in_buffer,
                (char *) strm.state.decompress_out_buffer,
                strm.state.decompress_in_index,
                strm.state.decompress_out_size
            );

            if (decompressed != strm.state.decompress_out_size) {
                // There was an error decompressing the block
                return LZLIB4_RC_BLOCK_SIZE_ERROR;
            }

            if (check_crc) {
                uint32_t crc = crc32(strm.state.decompress_out_buffer, strm.state.decompress_out_size);

                if (crc != header.crc) {
                    // Block CRC error
                    return LZLIB4_RC_BLOCK_DAMAGED;
                }
            }

            // Copy the decompressed buffer to output
            memcpy(strm.next_out, strm.state.decompress_out_buffer, decompressed);
            // Set the new pointer position and available space
            strm.next_out += decompressed;
            strm.avail_out -= decompressed;
            // Reset the input index
            strm.state.decompress_in_index = 0;
            strm.partial_block = false;
        }

        if (strm.avail_out == 0) {
            // There's no more space in output buffer so exit the loop
            break;
        }
    }

    return 0;
}

/**
 * @brief Decompress a part of the stream to fit into the output buffer. Multiple calls to this function
 *        keeping the same block in "strm.next_in" will decompress the next parts of the block.
 *        Ths function requires that the entire block exists in input buffer or will not work.
 * 
 * @param check_crc Check the block CRC. This will ensure that every block is correct, but will be slower.
 * @param seek_to Seek to a part of the block or -1 to continue at the last position.
 * @return An int variable. 0 if everything is OK otherwise a negative number.
 */
int lzlib4::decompress_partial(bool reset, bool check_crc, long long seek_to) {
    // Temporal position keepers
    uint8_t * bkp_next_out;
    size_t  bkp_avail_out;

    // While there is space in the output buffer
    while (strm.avail_out) {
        // If there is no more data in buffer or reset == true, read more data
        if (!(strm.state.decompress_tmp_size - strm.state.decompress_tmp_index) || reset) {
            // Get the header
            LZLIB4_BLOCK_HEADER header;
            memcpy(&header, strm.next_in, sizeof(LZLIB4_BLOCK_HEADER));

            // Check if compressed/uncompressed size is too high (possible corrupted header)
            if (header.compressed_size > LZ4_COMPRESSBOUND(LZLIB4_MAX_BLOCK_SIZE) || header.uncompressed_size > LZLIB4_MAX_BLOCK_SIZE) {
                return LZLIB4_RC_BLOCK_SIZE_ERROR;
            }

            // if new block size is bigger than reserved size, realloc the memory
            if (header.uncompressed_size > strm.state.decompress_tmp_size_real) {
                uint8_t * new_buffer = (uint8_t*) realloc(strm.state.decompress_tmp_buffer, header.uncompressed_size);
                if (new_buffer) {
                    strm.state.decompress_tmp_buffer = new_buffer;
                }
                else {
                    return LZLIB4_RC_BUFFER_ERROR;
                }
                
                strm.state.decompress_tmp_size_real = header.uncompressed_size;
            }

            // Store the output pointers into the backup variables
            bkp_next_out = strm.next_out;
            bkp_avail_out = strm.avail_out;
            uint8_t * dummy_tmp_buffer_pnt = strm.state.decompress_tmp_buffer;

            // Point the tmp buffer to the output
            strm.next_out = dummy_tmp_buffer_pnt;
            strm.avail_out = header.uncompressed_size;

            // Decompress the block
            int return_code = decompress(check_crc);

            // Recover the original output pointers
            strm.next_out = bkp_next_out;
            strm.avail_out = bkp_avail_out;

            // If return code is not 0, then something happens.
            // If block is not complete, a subsequent calls with more data to decompress_partial will fill the buffer
            if (return_code != 0) {
                // There was an error decompressing the block
                printf("There was an error decompressing the block: %d\n", return_code);
                return return_code;
            }

            strm.state.decompress_tmp_size = header.uncompressed_size;
            strm.state.decompress_tmp_index = 0;
        }

        // Check if there is no data in buffer and input is empty to break the loop
        if (!(strm.state.decompress_tmp_size - strm.state.decompress_tmp_index) && !strm.avail_in) {
            break;
        }

        // Copy buffer data to output buffer
        size_t to_read = std::min(strm.state.decompress_tmp_size - strm.state.decompress_tmp_index, strm.avail_out);
        memcpy(strm.next_out, strm.state.decompress_tmp_buffer + strm.state.decompress_tmp_index, to_read);

        strm.state.decompress_tmp_index += to_read;
        strm.avail_out -= to_read;
        strm.next_out += to_read;
    }

    return 0;
}

/**
 * @brief Free al reserved resources
 * 
 */
void lzlib4::close() {
    // Free the lz4 state
    if (strm.state.strm_lz4) {
        LZ4_freeStreamHC(strm.state.strm_lz4);
    }
    
    if (strm.state.strm_lz4_decode) {
        LZ4_freeStreamDecode(strm.state.strm_lz4_decode);
    }

    // Free compression and decompression buffers
    if (strm.state.compress_in_buffer) {
        free(strm.state.compress_in_buffer);
    }
    if (strm.state.compress_out_buffer) {
        free(strm.state.compress_out_buffer);
    }
    if (strm.state.decompress_in_buffer) {
        free(strm.state.decompress_in_buffer);
    }
    if (strm.state.decompress_out_buffer) {
        free(strm.state.decompress_out_buffer);
    }
}


uint32_t lzlib4::crc32(uint8_t *buf, size_t len) {
    register uint32_t oldcrc32;

    oldcrc32 = 0xFFFFFFFF;

    for ( ; len; --len, ++buf) {
        oldcrc32 = (crc_32_tab[((oldcrc32) ^ (*buf)) & 0xff] ^ ((oldcrc32) >> 8));
    }

    return ~oldcrc32;
}