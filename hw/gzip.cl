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

#include "gzip_channels.h"

//---------------------------------------------------------------------------------------
// Constants (keep in sync with host)
//---------------------------------------------------------------------------------------

//Specifies the maximum match length that we are looking for
// choose 4 or 8 or 16 (LEN <= VEC)
#define LEN VEC

//depth of the dictionary buffers
//can also go to depth 1024 which slightly improves quality
//but must change hash function to take advantage of that
#define DEPTH 512

//---------------------------------------------------------------------------------------
// load lz
//---------------------------------------------------------------------------------------
void load_lz_internal (
    volatile global unsigned char  *restrict input,
    unsigned int insize,
    unsigned int page_size,
    unsigned char marker) {

  // Initialize input stream position
  unsigned int inpos = 0;
  unsigned int pagepos = 0;
  unsigned int engine_id = 0;
  bool first = true;

  do {
    struct lz_input_t in;
    #pragma unroll
    for (char i = 0; i < VEC; i++)
      in.data[i] = input[inpos+i];
    in.last = ((pagepos + VEC) == page_size);
    in.marker = marker;

    if (first) {
      switch (engine_id) {
        case 0: write_channel_intel(ch_lz_in_first_value[0], in); break;
#if GZIP_ENGINES > 1
        case 1: write_channel_intel(ch_lz_in_first_value[1], in); break;
#endif
#if GZIP_ENGINES > 2
        case 2: write_channel_intel(ch_lz_in_first_value[2], in); break;
#endif
#if GZIP_ENGINES > 3
        case 3: write_channel_intel(ch_lz_in_first_value[3], in); break;
#endif
      }
    } else {
      switch (engine_id) {
        case 0: write_channel_intel(ch_lz_in[0], in); break;
#if GZIP_ENGINES > 1
        case 1: write_channel_intel(ch_lz_in[1], in); break;
#endif
#if GZIP_ENGINES > 2
        case 2: write_channel_intel(ch_lz_in[2], in); break;
#endif
#if GZIP_ENGINES > 3
        case 3: write_channel_intel(ch_lz_in[3], in); break;
#endif
      }
    }

    first = false;
    inpos += VEC;
    pagepos += VEC;

    if (pagepos == page_size) {
      pagepos = 0;
      first = true;
      engine_id++;
    }

    if (engine_id == GZIP_ENGINES)
      engine_id = 0;

  } while (inpos < insize);
}

__attribute__((max_global_work_dim(0)))
void kernel load_lz0 (
    // Use volatile to prevent caching, which wastes area and hurts Fmax
    volatile global unsigned char  *restrict input,
    unsigned int insize,
    unsigned int page_size,
    unsigned char marker) {
  load_lz_internal(input, insize, page_size, marker);
}

//---------------------------------------------------------------------------------------
// lz77
//---------------------------------------------------------------------------------------

struct dict_string {
  unsigned char s[LEN];
};


void lz_internal (unsigned int engine_id) {
  //-------------------------------------
  //   Hash Table(s)
  //-------------------------------------

  // Logically, these arrays should probably be [VEC][DEPTH], since we have VEC banks, each
  // of depth DEPTH.  But the compiler does banking by default on the lower bits of the array
  // address, so swap them to avoid having to specify bank bits.
  // The compiler currently doesn't explore solutions that combine banking 
  // and replication by default, so we need to specify the number of banks and ports per bank.
  // On A10, the compiler decides to use doublepump for dictionary_offset, which saves M20Ks,
  // but hurts Fmax enough that the trade-off isn't worth it, so use an attribute to prevent this.
  struct dict_string __attribute__((singlepump,numbanks(VEC),numreadports(VEC),numwriteports(1))) dictionary[DEPTH][VEC];
  // Parallel array to store the position of each dictionary entry in the input file.
  unsigned int __attribute__((singlepump,numbanks(VEC),numreadports(VEC),numwriteports(1))) dictionary_offset[DEPTH][VEC];

  // This is the window of data on which we look for matches
  // We fetch twice our data size because we have VEC offsets
  unsigned char current_window[VECX2];

  // This is the window of data on which we look for matches
  // We fetch twice our data size because we have VEC offsets
  unsigned char compare_window[LEN][VEC][VEC];
  // VEC bytes per dict---------|    |    |
  // VEC dictionaries----------------|    |
  // one for each curr win offset---------|

  // load offset into these arrays
  unsigned int compare_offset[VEC][VEC];
  // one per VEC bytes---------|    |
  // one for each compwin-----------|

  struct lz_input_t in;
  unsigned short hash[VEC];

  // arrays to store length, best length etc..
  unsigned char length[VEC];
  bool done[VEC];
  char bestlength[VEC];
  unsigned int bestoffset[VEC];

  // first_valid_pos is loop-carried, and tricky to compute.  So first compute it speculatively
  // in parallel for every possible value of the previous first_valid_pos.
  char first_valid_pos_speculative[VEC];
  // don't need to speculatively compute first_valid_full for performance, but it's more
  // convenient to also do it for first_valid_full if doing it for first_valid_pos.
  char first_valid_full_speculative[VEC];

  // Initialize history to empty.
  for (int i = 0; i < DEPTH; ++i) {
    #pragma unroll
    for (char k = 0; k < VEC; ++k) {
      dictionary_offset[i][k] = 0;
    }
  }

  // Initialize input stream position
  unsigned int inposMinusVecDiv16 = 0;
  unsigned int outpos_lz = 0;
  char first_valid_pos = 0;

  // load in new data
  in = read_channel_intel(ch_lz_in_first_value[engine_id]);
  #pragma unroll
  for (char i = 0; i < VEC; i++)
    current_window[i+VEC] = in.data[i];

  bool first_iteration = true;
  unsigned char marker = in.marker;

  do
  {
    //-----------------------------
    // Prepare current window
    //-----------------------------

    // shift current window
    #pragma unroll
    for (char i = 0; i < VEC; i++)
      current_window[i] = current_window[i+VEC];

    // load in new data
    in = read_channel_intel(ch_lz_in[engine_id]);
    #pragma unroll
    for (char i = 0; i < VEC; i++)
      current_window[VEC+i] = in.data[i];

    //-----------------------------
    // Compute hash
    //-----------------------------

    #pragma unroll
    for (char i = 0; i < VEC; i++) {
      unsigned short first_shifted  = current_window[i];
      hash[i] = ((first_shifted << 1) ^ current_window[i+1] ^ (current_window[i+2])) & 0x1ff; // 9 bits
    }

    //-----------------------------
    // Dictionary look-up
    //-----------------------------

    // loop over VEC compare windows, each has a different hash
    #pragma unroll
    for (char i = 0; i < VEC; i++) {
      // loop over all VEC bytes
      #pragma unroll
      for (char j = 0; j < LEN; j++) {
        #pragma unroll
        for (char k = 0; k < VEC; k++) {
          compare_window[j][k][i] = dictionary[hash[i]][k].s[j];
        }
      }
    }

    // loop over compare windows
    #pragma unroll
    for (char i = 0; i < VEC; i++) {
      // loop over frames in this compare window (they come from different dictionaries)
      #pragma unroll
      for (char k = 0; k < VEC; ++k) {
        compare_offset[k][i] = dictionary_offset[hash[i]][k];
      }
    }

    //-----------------------------
    // Dictionary update
    //-----------------------------

    // loop over different dictionaries to store different frames
    // store one frame per dictionary
    // loop over VEC bytes to store
    #pragma unroll
    for (char i = 0; i < LEN; i++) {
      // store actual bytes
      #pragma unroll
      for (char k = 0; k < VEC; k++) {
        dictionary[hash[k]][k].s[i] = current_window[i+k];
      }
    }

    #pragma unroll
    for (char k = 0; k < VEC; ++k) {
      // inpos - VEC + 0, we know that inpos - VEC has 0 as the 4 lower bits so really just concatenate
      dictionary_offset[hash[k]][k] = (inposMinusVecDiv16 << 4) | k;
    }

    //-----------------------------
    // Match search
    //-----------------------------

    // initialize bestlength
    #pragma unroll
    for (char i = 0; i < VEC; i++) {
      bestlength[i] = 0;
      bestoffset[i] = 0;
    }

    // loop over each comparison window frame
    // one comes from each dictionary
    #pragma unroll
    for(char i = 0; i < VEC; i++ ) {
      // initialize length and done
      #pragma unroll
      for (char l = 0; l < VEC; l++) {
        length[l] = 0;
        done[l] = 0;
      }

      // loop over each current window
      #pragma unroll
      for (char j = 0; j < VEC ; j++) {
        // loop over each char in the current window
        // and corresponding char in comparison window
        #pragma unroll
        for(char k = 0; k < LEN ; k++) {
          bool comp = current_window[k+j] == compare_window[k][i][j] && !done[j];
          length[j] += comp;
          done[j] = !comp;
        }
      }

      // check if this the best length
      #pragma unroll
      for (char m = 0; m < VEC; m++) {
        bool updateBest = (length[m] > bestlength[m]) && (compare_offset[i][m] != 0) && (((inposMinusVecDiv16<<4)|(i&0xf))-(compare_offset[i][m]) < 0x40000);
        bestoffset[m] = (updateBest ? ((inposMinusVecDiv16<<4)|(m&0xf))-(compare_offset[i][m]) : bestoffset[m]) & 0x7ffff;  //19 bits is sufficient
        bestlength[m] = (updateBest ? length[m] : bestlength[m]) & 0x1f;    //5 bits is sufficient
      }
    }

    //-----------------------------
    // Filter matches step 1
    //-----------------------------

    // remove matches with offsets that are <= 0: this means they're self-matching or didn't match
    // and keep only the matches that, when encoded, take fewer bytes than the actual match length
    #pragma unroll
    for (char i = 0; i < VEC; i++) {
      bestlength[i] = (( ((bestlength[i]&0x1f) >= 5) || (((bestlength[i]&0x1f) == 4) && ((bestoffset[i]&0x7ffff) < 0x800))) ? bestlength[i] : 0) & 0x1f; //5 bits is sufficient
    }

    //-----------------------------
    // Assign first_valid_pos
    //-----------------------------

    #pragma unroll
    for (char guess = 0; guess < VEC; guess++) {
      first_valid_pos_speculative[guess] = 0;

      // select the last index with a positive bestlength
      #pragma unroll
      for(char i = 0; i < VEC; i++) {
        // First we need to filter bestlength based on our guess
        // This code is duplicated just after the speculative loop for the actual first_valid_pos
        first_valid_pos_speculative[guess] = ((i < guess || (bestlength[i]&0x1f)==0) ? first_valid_pos_speculative[guess] : i + bestlength[i]) & 0x1f; // 5 bits is sufficient
      }

      first_valid_full_speculative[guess] = first_valid_pos_speculative[guess];

      // since first_valid_pos_speculative only needs 5 bits, >= VEC is the same at looking at bit 4, if true keep bits 3:0, else set to 0
      first_valid_pos_speculative[guess] = (first_valid_pos_speculative[guess] & 0x10) ? (first_valid_pos_speculative[guess] & 0xf) : 0;

      // use __fpga_reg to influence the scheduler decision on what to put in the same clock cycle as the critical loop.
      // deleting this line will not affect functionality.
      // on a large gzip variant (4 copies of VEC=16), this improved average Fmax by about 2-3%.  Without it, the critical loop is sometimes
      // more than the 3 LUTs of depth that would be expected from hand analysis (2 for 16:1 mux + the pop mux)
      first_valid_pos_speculative[guess] = __fpga_reg(first_valid_pos_speculative[guess]);
    }

    // remove matches covered by previous cycle.  We already did this speculatively to compute first_valid_pos,
    // but now do it for the actual first_valid_pos value for use in the rest of the loop
    #pragma unroll
    for(char i = 0; i < VEC; i++)
      bestlength[i] = i < first_valid_pos ? -1 : bestlength[i];

    // for VEC=16 (the largest currently supported), this should be a 16:1 mux, which is 2 6LUTs deep.  For larger VEC, it will be worse.
    first_valid_pos = first_valid_pos_speculative[ first_valid_pos & 0xf] & 0xf;    //first_valid_pos only needs 4 bits, make this explicit
    char first_valid_full = first_valid_full_speculative[first_valid_pos];

    //-----------------------------
    // Filter matches "last-fit"
    //-----------------------------

    // pre-fit stage removes the later matches that have EXACTLY the same reach
    #pragma unroll
    for (char i = 0; i < VEC-1; i++)
      #pragma unroll
      for (char j = 1; j < VEC; j++)
        bestlength[j] = ((bestlength[i] + i) == (bestlength[j] + j)) && i < j && bestlength[j] > 0 && bestlength[i] > 0 ? 0 : bestlength[j];
      //                  reach of bl[i]         reach of bl[j]

    // look for location of golden matcher
    int golden_matcher = 0;
    #pragma unroll
    for(char i = 0; i < VEC; i++)
      golden_matcher = (bestlength[i]+i == (first_valid_full)) ? i : golden_matcher;

    // if something covers golden matcher --> remove it!
    #pragma unroll
    for (char i = 0; i < VEC; i++)
      bestlength[i] = ((bestlength[i] + i) > golden_matcher) && i < golden_matcher && bestlength[i] != -1 ? 0 : bestlength[i];

    // another step to remove literals that will be covered by prior matches
    // set those to '-1' because we will do nothing for them, not even emit symbol
    #pragma unroll
    for (char i = 0; i < VEC-1; i++) {
      #pragma unroll
      for (char j = 1; j < VEC; j++) {
        if (i+j < VEC && bestlength[i] > j) {
          bestlength[i+j] = -1;
        }
      }
    }

    //-----------------------------
    // Encode LZ bytes
    //-----------------------------

    unsigned char output_buffer[VECX2];
    int output_buffer_size = 0;

    bool dontencode[VECX2];

    #pragma unroll
    for (int i = 0; i < VECX2; i++) {
      output_buffer[i] = 0;
      dontencode[i] = false;
    }

    if (first_iteration) {
      // output marker byte at start
      output_buffer[output_buffer_size++] = marker;
      first_iteration = false;
    }

    #pragma unroll
    for (char i = 0; i < VEC; i++) {
      if (bestlength[i] == 0) {
        dontencode[output_buffer_size] = false;
        output_buffer[output_buffer_size++] = current_window[i];

        if (current_window[i] == marker) {
          dontencode[output_buffer_size] = 0;
          output_buffer[output_buffer_size++] = 0;
        }
      } else if (bestlength[i] > 0) {
        // 1-output marker
        dontencode[output_buffer_size] = 0;
        output_buffer[output_buffer_size++] = marker;

        // 2-output bestlength

        // fit b.l. in 4 bits and put in upper m.s.b's
        unsigned char bestlength_byte = (bestlength[i] - 3) << 4;

        // concat with lower 4 bestoffset bytes
        unsigned int offset = bestoffset[i] & 0x7ffff;  //19 bits
        bestlength_byte |= (offset & 0xf);

        // write to output_buffer
        dontencode[output_buffer_size] = 1;
        output_buffer[output_buffer_size++] = bestlength_byte;

        // 3-output bestoffset

        // check how many more bytes we need for bestoffset (either one or two)
        int extra_offset_bytes = 0;

        if (offset < 0x800) { // offset needs only one extra byte
          dontencode[output_buffer_size] = true;
          output_buffer[output_buffer_size++] = (offset >> 4) & 0x7f;
        } else { // offset needs two extra bytes
          dontencode[output_buffer_size] = true;
          output_buffer[output_buffer_size++] =  (offset >> 4)  | 0x80;
          dontencode[output_buffer_size] = true;
          output_buffer[output_buffer_size++] = ((offset >> 11) & 0x7f);
        }
      }
    }
    outpos_lz += output_buffer_size;

    //-----------------------------
    // Huffman encoding
    //-----------------------------
    struct huffman_input_t huff_data;

    #pragma unroll
    for (char i = 0; i < VECX2; i++) {
      huff_data.data[i] = output_buffer[i];
      huff_data.dontencode[i] = dontencode[i];
      // valid array contains '1' if there is data at this output_buffer position, zero otherwise
      if (i < output_buffer_size)
        huff_data.valid[i] = true;
      else
        huff_data.valid[i] = false;
    }
    huff_data.last = in.last;

    write_channel_intel(ch_huffman_input[engine_id], huff_data);

    // increment input position
    inposMinusVecDiv16++;
  } while (!in.last);

  // have to output first_valid_pos to tell the host from where to start writing
  write_channel_intel(ch_lz_out_first_valid_pos[engine_id], first_valid_pos);

  // return lz compressed size
  write_channel_intel(ch_lz_out_compsize_lz[engine_id], outpos_lz);
}

__attribute__((max_global_work_dim(0)))
void kernel lz77_0 () {
  lz_internal(0);
}

#if GZIP_ENGINES > 1
__attribute__((max_global_work_dim(0)))
void kernel lz77_1 () {
  lz_internal(1);
}
#endif

#if GZIP_ENGINES > 2
__attribute__((max_global_work_dim(0)))
void kernel lz77_2 () {
  lz_internal(2);
}
#endif

#if GZIP_ENGINES > 3
__attribute__((max_global_work_dim(0)))
void kernel lz77_3 () {
  lz_internal(3);
}
#endif


//---------------------------------------------------------------------------------------
// load_huff_coeff
//---------------------------------------------------------------------------------------

void load_huff_coeff_internal (
    volatile global unsigned int   *restrict huftableOrig,
    unsigned int n_pages) {

  unsigned int engine_id = 0;

  for (short p = 0; p < n_pages; p++) {
    for (short i = 0; i < HUFTABLESIZE; i++) {
      int huff_entry = huftableOrig[i];
      
      switch(engine_id) {
        case 0: write_channel_intel(ch_huffman_coeff[0], huff_entry); break;
#if GZIP_ENGINES > 1
        case 1: write_channel_intel(ch_huffman_coeff[1], huff_entry); break;
#endif
#if GZIP_ENGINES > 2
        case 2: write_channel_intel(ch_huffman_coeff[2], huff_entry); break;
#endif
#if GZIP_ENGINES > 3
        case 3: write_channel_intel(ch_huffman_coeff[3], huff_entry); break;
#endif
      }
    }

    engine_id++;

    if (engine_id == GZIP_ENGINES)
      engine_id = 0;
  }
}

__attribute__((max_global_work_dim(0)))
void kernel load_huff_coeff0 (
    volatile global unsigned int   *restrict huftableOrig,
    unsigned int n_pages) {
    load_huff_coeff_internal(huftableOrig, n_pages);
}

//---------------------------------------------------------------------------------------
// HUFFMAN
//---------------------------------------------------------------------------------------

// assembles up to VECX2 unsigned char values based on given huffman encoding
// writes up to MAX_HUFFCODE_BITS * VECX2 bits to memory
bool hufenc(unsigned short huftable[HUFTABLESIZE], unsigned char huflen[HUFTABLESIZE],
    unsigned char *data, bool *valid, bool *dontencode, unsigned short *outdata,
    unsigned short *leftover, unsigned short *leftover_size) {

  // array that contains the bit position of each symbol
  unsigned short bitpos[VECX2 + 1];
  bitpos[0] = 0;
  #pragma unroll
  for (char i = 0; i < VECX2 ; i++)  {
    bitpos[i + 1] = bitpos[i] + (valid[i] ? (dontencode[i] ? 8 : huflen[data[i]]) : 0);
  }

  // leftover is an array that carries huffman encoded data not yet written to memory
  // adjust leftover_size with the number of bits to write this time
  unsigned short prev_cycle_offset = *leftover_size;
  *leftover_size += bitpos[VECX2];

  // we'll write this cycle if we have collected enough data (VECX2 shorts or more)
  bool write = *leftover_size & (VECX2 * MAX_HUFFCODE_BITS);

  // subtract VECX2 shorts from leftover size (if it's bigger than VECX2) because we'll write those out this cycle
  *leftover_size &= ~(VECX2 * MAX_HUFFCODE_BITS);

  // Adjust bitpos based on leftover offset from previous cycle
  #pragma unroll
  for (char i = 0; i < VECX2; i++)  {
    bitpos[i] += prev_cycle_offset;
  }

  // Huffman codes have any bit alignement, so they can spill onto two shorts in the output array
  // use ushort2 to keep each part of the code separate
  // Iterate over all codes and construct ushort2 containing the code properly aligned
  ushort2 code[VECX2];
  #pragma unroll
  for (char i = 0; i < VECX2; i++)  {
    unsigned short curr_code = dontencode[i] ? data[i] : huftable[data[i]];
    unsigned char curr_code_len = dontencode[i] ? 8 : huflen[data[i]];
    unsigned char bitpos_in_short = bitpos[i] & 0x0F;

    unsigned int temp = (unsigned int)curr_code << 16;
    unsigned short temp1 = temp >> (curr_code_len + bitpos_in_short);
    unsigned short temp2 = 0;
    if (curr_code_len + bitpos_in_short - 16 >= 0) {
      temp2 = temp >> (curr_code_len + bitpos_in_short - 16);
    }
    code[i] = valid[i] ? (ushort2)(temp1, temp2) : (ushort2)(0, 0);
  }

  // Iterate over all destination locations and gather the required data
  unsigned short new_leftover[VECX2];
  #pragma unroll
  for (char i = 0; i < VECX2; i++)  {
    new_leftover[i] = 0;
    outdata[i] = 0;
    #pragma unroll
    for (char j = 0; j < VECX2; j++) {
      // figure out whether code[j] goes into bucket[i]
      bool match_first  = ((bitpos[j] >> 4) & (VECX2 - 1)) == i;
      bool match_second = ((bitpos[j] >> 4) & (VECX2 - 1)) == ((i - 1) & (VECX2 - 1));

      // if code[j] maps onto current bucket then OR its code, else OR with 0
      unsigned short component = match_first ? code[j].x : (match_second ? code[j].y : 0);

      // overflow from VECX2 shorts, need to move onto new_leftover
      bool use_later = (bitpos[j] & (VECX2 * MAX_HUFFCODE_BITS)) ||
          (match_second && (((bitpos[j] >> 4) & (VECX2 - 1)) == VECX2 - 1));

      // write to output
      outdata[i] |= use_later ? 0 : component;
      new_leftover[i] |= use_later ? component : 0;
    }
  }

  // Apply previous leftover on the outdata
  // Also, if didn't write, apply prev leftover onto newleftover
  #pragma unroll
  for (char i = 0; i < VECX2; i++) {
    outdata[i] |= leftover[i];
    if (write)
      leftover[i] = new_leftover[i];
    else
      leftover[i] |= outdata[i];
  }

  return write;
}

void huff_internal (unsigned int engine_id) {
  // carries partially assembled huffman output_lz
  unsigned short leftover[VECX2];

  // Force singlepump - double-pumping only saves a few M20K blocks, and can limit Fmax.
  unsigned short __attribute__((singlepump)) huftable[HUFTABLESIZE];
  unsigned char __attribute__((singlepump)) huflen[HUFTABLESIZE];

  struct huffman_input_t in;

  while (true) {
    #pragma unroll
    for(char i = 0; i < VECX2; i++)
      leftover[i] = 0;

    unsigned short leftover_size = 0;

    // Load Huffman codes
    for (short i = 0; i < HUFTABLESIZE; i++) {
      unsigned int a = read_channel_intel(ch_huffman_coeff[engine_id]);
      unsigned int code = a & 0xFFFF;
      unsigned char len = a >> 16;

      huftable[i] = code;
      huflen[i] = len;
    }

    unsigned int outpos_huffman = 0;

    do {
      in = read_channel_intel(ch_huffman_input[engine_id]);

      struct huffman_output_t outdata;
      outdata.write = hufenc(huftable, huflen, in.data, in.valid, in.dontencode, outdata.data, leftover, &leftover_size);
      outdata.last = in.last;

      outpos_huffman = outdata.write ? outpos_huffman + 1 : outpos_huffman;
      write_channel_intel(ch_huffman_output[engine_id], outdata);

    } while(!in.last);

    // write remaining bits
    struct huffman_output_t outdata;
    outdata.write = true;
    outdata.last = true;
    #pragma unroll
    for (char i = 0; i < VECX2; i++)
      outdata.data[i] = leftover[i];
    write_channel_intel(ch_huffman_output_last_value[engine_id], outdata);

    // change to count of shorts
    outpos_huffman = outpos_huffman * VECX2;
    outpos_huffman = outpos_huffman + (leftover_size >> 4) + ((leftover_size & 0x7) != 0);
    // return compressed size as size of chars
    write_channel_intel(ch_huffman_out_compsize_huffman[engine_id], outpos_huffman*2);
  }
}

__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel huff0 () {
    huff_internal(0);
}

#if GZIP_ENGINES > 1
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel huff1 () {
    huff_internal(1);
}
#endif

#if GZIP_ENGINES > 2
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel huff2 () {
    huff_internal(2);
}
#endif

#if GZIP_ENGINES > 3
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel huff3 () {
    huff_internal(3);
}
#endif


//---------------------------------------------------------------------------------------
// store_huff
//---------------------------------------------------------------------------------------

void store_huff_internal (unsigned int engine_id) {
  while (true) {
    struct huffman_output_t indata;
    struct gzip_to_aes_t outdata;
    bool previous_last = false;
    bool the_end = false;

    do {
      if (!previous_last)
        indata = read_channel_intel(ch_huffman_output[engine_id]);
      else
        indata = read_channel_intel(ch_huffman_output_last_value[engine_id]);

      the_end = indata.last && previous_last;
      previous_last = indata.last;

      #pragma unroll
      for (unsigned int i=0; i<VECX2; i++) {
        outdata.data[i] = indata.data[i];
      }

      outdata.last = the_end;
      if (indata.write)
        write_channel_intel(ch_gzip2load[engine_id], outdata);
        
    } while(!the_end);
    
    // Store summary values from lz and huffman
    struct gzip_header_t header;
    header.first_valid_pos = read_channel_intel(ch_lz_out_first_valid_pos[engine_id]);
    header.compsize_lz = read_channel_intel(ch_lz_out_compsize_lz[engine_id]);
    header.compsize_huffman = read_channel_intel(ch_huffman_out_compsize_huffman[engine_id]);
    write_channel_intel(ch_gzip2header[engine_id], header);
  }
}

__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel store_huff0() {
  store_huff_internal(0);
}

#if GZIP_ENGINES > 1
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel store_huff1() {
  store_huff_internal(1);
}
#endif

#if GZIP_ENGINES > 2
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel store_huff2() {
  store_huff_internal(2);
}
#endif

#if GZIP_ENGINES > 3
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel store_huff3(global struct gzip_out_info_t *restrict out_info) {
  store_huff_internal(out_info, 3);
}
#endif
