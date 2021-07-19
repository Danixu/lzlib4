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

#include "lzlib4_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>

uint8_t lzlib4_compression_level = LZ4HC_CLEVEL_DEFAULT;

int lzlib4_compress_init(
    lzlib4_stream * strm,
    size_t block_size,
    lzlib4_block_mode block_mode,
    uint8_t compression_level
){
    // Input data
    strm->next_in = NULL;
    strm->avail_in = 0;
    strm->total_in = 0;

    // Output data
    strm->next_out = NULL;
    strm->avail_out = 0;
    strm->total_out = 0;

    // Initializing the compression buffer
    strm->state.compress_in_bytes = block_size;
    strm->state.compress_in_buffer = (uint8_t*) malloc(strm->state.compress_in_bytes);
    strm->state.compress_in_index = 0;
    strm->state.compress_out_bytes = LZ4_COMPRESSBOUND(strm->state.compress_in_bytes); // Worst case
    strm->state.compress_out_buffer = (uint8_t*) malloc(strm->state.compress_out_bytes);
    
    strm->state.compress_block_mode = block_mode;

    // Initializing the LZ4HC stream
    strm->state.strm_lz4 = LZ4_createStreamHC();
    // Set the block compression
    LZ4_resetStreamHC(strm->state.strm_lz4, compression_level);

    lzlib4_compression_level = compression_level;

    return 0;
}

int lzlib4_compress_block(lzlib4_stream * strm, lzlib4_flush_mode flush_mode) {
    // LZLIB4_FULL_CONTENT will require that avail_in will fit into the compress buffer
    if (strm->state.compress_block_mode == LZLIB4_INPUT_NOSPLIT && strm->avail_in > strm->state.compress_in_bytes) {
        // FULL data mode is selected and block is bigger than block size.
        strm->msg = "Input size is bigger than block size.";
        return -1;
    }

    // Compress the input buffer block by block until is empty or output buffer is full
    while (true) {
        bool to_compress = false;
        size_t space_left = strm->state.compress_in_bytes - strm->state.compress_in_index;
        size_t to_read = 0;

        // Check if new block is required
        if (strm->state.compress_block_mode == LZLIB4_INPUT_NOSPLIT && strm->avail_in > space_left) {
            // No split mode forces to create a new block if doesn't fit the space that left
            to_compress = true;
        }
        else {
            to_read = std::min(space_left, strm->avail_in);
        }

        printf("Index: %d\n", strm->state.compress_in_index);

        if (to_read) {
            memcpy(strm->state.compress_in_buffer + strm->state.compress_in_index, strm->next_in, to_read); // Copy the data
            strm->next_in += to_read; // Move the pointer to the new position
            strm->avail_in -= to_read; // Set the new available size
            strm->state.compress_in_index += to_read; // Set the new buffer index

            if (
                (strm->state.compress_in_index >= (strm->state.compress_in_bytes)) ||
                (strm->avail_in == 0 && flush_mode > 0)
            ) {
                // In buffer was filled so is ready to compress a block
                to_compress = true;
            }
        }

        if (to_compress) {
            // A new block will be created
            size_t compressed = LZ4_compress_HC_continue(
                strm->state.strm_lz4,
                (char *) strm->state.compress_in_buffer,
                (char *) strm->state.compress_out_buffer,
                strm->state.compress_in_index,
                strm->state.compress_out_bytes
            );

            if (compressed) {
                if (compressed > strm->avail_out) {
                    // Compressed stream doesn't fit the output buffer, si an error is returned
                    return -1;
                }

                // Copy the compressed block to output
                memcpy(strm->next_out, strm->state.compress_out_buffer, compressed);
                // Set the new pointer position and available space
                strm->next_out += compressed;
                strm->avail_out -= compressed;
                // Reset the input index
                strm->state.compress_in_index = 0;
            }
            else {
                return -1;
            }
        }

        if (strm->avail_in == 0) {
            // There's no more data in input buffer so exit the loop
            break;
        }
    }

    if (flush_mode == LZLIB4_FINISH){
        // Flush mode is FINISH, so a reset is required
        LZ4_resetStreamHC(strm->state.strm_lz4, lzlib4_compression_level);
    }

    return 0;
}