// Copyright (C) 2013-2019 Altera Corporation, San Jose, California, USA. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
// 
// This agreement shall be governed in all respects by the laws of the State of California and
// by the laws of the United States of America.

#ifndef GZIP_CHANNELS_H
#define GZIP_CHANNELS_H

#pragma OPENCL EXTENSION cl_intel_channels : enable
#include "../sw/inc/gzip_tools.h"

// GZIP_ENGINES and VEC are defined in gzip_tools.h
// NOTE: ENGINES and VEC values for the host and kernel must match 
// If either of the variables were otherwise defined in the kernel complation, 
// they must also be defined the same for the host compilation 
#define VECX2 (2 * VEC)

#define HUFTABLESIZE 256

//Maximum length of huffman codes
#define MAX_HUFFCODE_BITS 16

struct lz_input_t {
  unsigned char data[VEC];
};

struct huffman_input_t {
  unsigned char data[VECX2];
  bool dontencode[VECX2];
  bool valid[VECX2];
};

struct huffman_output_t {
  unsigned short data[VECX2];
  bool write;
};

struct gzip_to_aes_t {
  unsigned short data[VECX2];
  bool last;
};


channel struct lz_input_t ch_lz_in[GZIP_ENGINES]  __attribute__((depth(64)));
// The first value is read outside of the inner loop,
// and we can't read from the same channel in two places. 
// Use a seperate channel instead of  moving the first read into the loop and predicating.
channel struct lz_input_t ch_lz_in_first_value[GZIP_ENGINES];
channel struct huffman_input_t ch_huffman_input[GZIP_ENGINES] __attribute__((depth(64)));
channel struct huffman_output_t ch_huffman_output[GZIP_ENGINES] __attribute__((depth(64)));
// See comment on ch_lz_in_first_value
channel struct huffman_output_t ch_huffman_output_last_value[GZIP_ENGINES];
channel unsigned int ch_huffman_coeff[GZIP_ENGINES];

// We write out a few integer values at the end of the compression.  It's
// simplest to just create a channel for each one.
channel unsigned int ch_lz_out_first_valid_pos[GZIP_ENGINES];
channel unsigned int ch_lz_out_compsize_lz[GZIP_ENGINES];
channel unsigned int ch_huffman_out_compsize_huffman[GZIP_ENGINES];

channel struct gzip_to_aes_t ch_gzip2aes[GZIP_ENGINES] __attribute__((depth(64)));

#endif

