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

/**
 * LZ4 works differently compared with zlib. Every time you call the compress function it creates a block.
 *
 * My program call the compress function every sector, wich create blocks of 2k or less (sometimes even 4 bytes
 * blocks), so the compression ratio suffers too much. In a test with the Final Fantasy VII Disk image, the size
 * of the compressed stream using the lz4 program is 454Mb vs 481Mb of my encoder (6% less).
 *  
 * The pourpose of this wrapper is to create a zlib like function which will contains a buffer to store the data
 * until its size is near the 64k block, and then call the compression function. This will reduce the blocks
 * overhead.
 **/

#include "lz4hc.h"

#include <cstdio>

// Block size of uncompressed data. This size must be able to fit into the LZLIB5_BLOCK_HEADER compressed_size variable,
// after passing it thought the LZ4_COMPRESSBOUND macro.
// Example: for an uint16_t variable the max size is 65535. The worst scenario must be lower than this so real max size
//      would be.
//
//   65535 - 16 fixed required bytes - 4 header bytes (LZLIB4_BLOCK_HEADER) = 65515
//   65519 / 255 (every 255 bytes an extra byte is required) = 256,92...
// 
//   recommended block size = 256 * 255 = 65280
// 
// This block size will keep the worst case below the max allowed size for a compressed_size option
//
#define LZLIB4_BLOCK_SIZE 65280

// Block header that contains both compressed and decompressed size.
struct LZLIB4_BLOCK_HEADER {
    uint32_t compressed_size = 0;
    uint32_t uncompressed_size = 0;
};

// Compression flush modes, keeping almost all zlib modes.
// Only two different modes are used:
// * LZLIB4_NO_FLUSH: Will not flush the data until
enum lzlib4_flush_mode : uint8_t {
    LZLIB4_NO_FLUSH = 0,
    LZLIB4_PARTIAL_FLUSH,
    LZLIB4_SYNC_FLUSH,
    LZLIB4_FULL_FLUSH,
    LZLIB4_FINISH,
    LZLIB4_BLOCK,
};

/**
 * @brief Block fill mode.
 * 
 * LZLIB4_FULL_CONTENT: If new data doesn't fit into the free space in block buffer, the program will flush the buffer
 *                      before add the data. This mode keep all new data into the same block.
 * LZLIB4_PARTIAL_CONTENT: Block buffer is always filled with data before flush it, even if new data doesn't fit into it.
 *                      This mode will keep the blocks at the same size, but new data can be splitted between two blocks.
 * 
 */ 
enum lzlib4_block_mode: uint8_t {
    LZLIB4_INPUT_NOSPLIT,
    LZLIB4_INPUT_SPLIT
};

// Internal state and buffers
struct lzlib4_internal_state {
    // Compression buffer
    uint8_t * compress_in_buffer = NULL;
    size_t compress_in_bytes;
    size_t compress_in_index = 0;
    uint8_t * compress_out_buffer = NULL;
    size_t compress_out_bytes;

    lzlib4_block_mode compress_block_mode;

    // Decompression buffer
    uint8_t * decompress_buffer = NULL;
    size_t decompress_bytes;
    size_t decompress_index = 0;

    // LZ4HC stream status
    LZ4_streamHC_t * strm_lz4;
};

// Stream state similar to zlib state
struct lzlib4_stream {
    uint8_t * next_in;   /* next input byte */
    size_t  avail_in;  /* number of bytes available at next_in */
    size_t  total_in;  /* total number of input bytes read so far */

    uint8_t * next_out;  /* next output byte will go here */
    size_t  avail_out; /* remaining free space at next_out */
    size_t  total_out; /* total number of bytes output so far */

    char *msg;  /* last error message, NULL if no error */
    struct lzlib4_internal_state state;
};

/**
 * @brief : Initialize the stream compression state to keep the buffers and the state of the compression
 * 
 * @param strm : lzlib4 stream to initialize
 * @param block_size : wanted block size
 * @param block_mode : defines if block will be filled with data splitting input data, or entire input data will be kept in same block, Defaults LZLIB4_PARTIAL_CONTENT
 * @param compression_level : LZ4HC compression level (1-13). Defaults to LZ4HC_CLEVEL_DEFAULT
 * @return int : returns 0 if all was right, negative number otherwise.
 */
int lzlib4_compress_init(lzlib4_stream * strm, size_t block_size, lzlib4_block_mode block_mode = LZLIB4_INPUT_SPLIT, uint8_t compression_level = LZ4HC_CLEVEL_DEFAULT);

int lzlib4_compress_block(lzlib4_stream * strm, lzlib4_flush_mode flush_mode);