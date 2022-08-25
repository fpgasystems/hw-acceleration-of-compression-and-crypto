// AES-256 call and key calculation

#include "gzip_channels.h"

//---------------------------------------------------------------------------------------
// AES
//---------------------------------------------------------------------------------------

long8  aes_256(long8 x, int8 config, long16 key_lsb, long16 key_msb);
long16 aes_key_256(int8 x, char flax);

long8 aes_256_decrypt(long8 x, int8 config, long16 key_lsb, long16 key_msb);
long16 aes_key_256_decrypt(int8 x, char flax);

channel long16 ch_aes_keylsb[2][AES_ENGINES];
channel long16 ch_aes_keymsb[2][AES_ENGINES];

union enc_t {
  unsigned short data[VECX2];
  long8 datalong;
};

union header_u {
  unsigned int values[16];
  long8 datalong;
};

void aes_encrypt_internal (unsigned int engine_id)
{
  while(true) {
    long16 round_key_lsb;
    long16 round_key_msb;
    unsigned int pointer;
    unsigned int n_lines;
    struct gzip_to_aes_t huffman_data;
    struct aes_enc_setup_t setup;
    union enc_t data_enc;

    setup = read_channel_intel(ch_aes_enc_setup[engine_id]);

    round_key_lsb = aes_key_256(setup.key, 0x01);
    round_key_msb = aes_key_256(setup.key, 0x02);

    pointer = setup.offset + 1; // HEADER SIZE = 512 bits = 1 LINE
    n_lines = 0;
    do {
      huffman_data = read_channel_intel(ch_load2aes[engine_id]);

      #pragma unroll
      for (unsigned int i=0; i<VECX2; i++) {
        data_enc.data[i] = huffman_data.data[i];
      }

      setup.out[pointer] = aes_256(data_enc.datalong, setup.config_data, round_key_lsb, round_key_msb);
      pointer = pointer + 1;
      n_lines = n_lines + 1;

    } while (!huffman_data.last);

    // store header
    struct gzip_header_t header_int = read_channel_intel(ch_header2aes[engine_id]);

    union header_u header;
    header.values[0] = n_lines;
    header.values[1] = header_int.first_valid_pos;
    header.values[2] = header_int.compsize_lz;
    header.values[3] = header_int.compsize_huffman;

    setup.out[setup.offset] = header.datalong;
  }
}

__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt0() {
  aes_encrypt_internal(0);
}

#if AES_ENGINES > 1
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt1() {
  aes_encrypt_internal(1);
}
#endif

#if AES_ENGINES > 2
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt2() {
  aes_encrypt_internal(2);
}
#endif

#if AES_ENGINES > 3
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt3() {
  aes_encrypt_internal(3);
}
#endif

#if AES_ENGINES > 4
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt4() {
  aes_encrypt_internal(4);
}
#endif

#if AES_ENGINES > 5
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt5() {
  aes_encrypt_internal(5);
}
#endif

#if AES_ENGINES > 6
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt6() {
  aes_encrypt_internal(6);
}
#endif

#if AES_ENGINES > 7
__attribute__((max_global_work_dim(0)))
__attribute__((autorun))
void kernel aes_encrypt7() {
  aes_encrypt_internal(7);
}
#endif

void aes_keygen_dec_internal (int8 key, unsigned int engine_id)
{
    long16 round_key_lsb;
    long16 round_key_msb;

    round_key_lsb = aes_key_256_decrypt(key, 0x01);
    round_key_msb = aes_key_256_decrypt(key, 0x02);

    write_channel_intel(ch_aes_keylsb[1][engine_id], round_key_lsb);
    write_channel_intel(ch_aes_keymsb[1][engine_id], round_key_msb);
}

__attribute__((max_global_work_dim(0)))
void kernel aes_keygen_dec0(int8 key)
{
  aes_keygen_dec_internal(key, 0);
}

__attribute__((max_global_work_dim(0)))
kernel void aes_decrypt0 (
  global long8 * restrict in,
  global long8 * restrict out,
  int8 config_data,
  unsigned int n_pages,
  unsigned int mem_offset)
{
  long16 round_keys[2];
  round_keys[0] = read_channel_intel(ch_aes_keylsb[1][0]);
  round_keys[1] = read_channel_intel(ch_aes_keymsb[1][0]);

  unsigned int pointer = 0;
  unsigned int offset = 0;

  for (unsigned page = 0; page < n_pages; page++) {
    union header_u header;
    header.datalong = in[pointer];
    out[pointer] = header.datalong;
    pointer++;

    for (unsigned int k = 0; k < header.values[0]; k++) {
      long8 data = in[pointer];
      out[pointer] = aes_256_decrypt(data, config_data, round_keys[0], round_keys[1]);
      pointer++;
    }

    offset += mem_offset;
    pointer = offset;
  }
}
