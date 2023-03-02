// AES-256 call and key calculation

#include "gzip_channels.h"

//---------------------------------------------------------------------------------------
// AES
//---------------------------------------------------------------------------------------

long8  aes_256 (long8 x, int8 config, long16 key_lsb, long16 key_msb);
long16 aes_key_256 (int8 x, char flax);

long8 aes_256_decrypt (long8 x, int8 config, long16 key_lsb, long16 key_msb);
long16 aes_key_256_decrypt (int8 x, char flax);

channel long16 chan_msb;
channel long16 chan_lsb;
channel long16 chan_msb_decrypt;
channel long16 chan_lsb_decrypt;

union enc_t {
  unsigned short data[VECX2];
  long8 datalong;
};

void aes_encrypt_internal (
    global long8 *restrict output,
    int8 config_data,
    unsigned int engine_id) {

    long16 round_keys[2];

    unsigned int outpos = 0;

    struct gzip_to_aes_t huffman_data;
    union enc_t data_enc;

    round_keys[0] = read_channel_intel(chan_lsb);
    round_keys[1] = read_channel_intel(chan_msb);

    do
    {
        huffman_data = read_channel_intel(ch_gzip2aes[0]);

        #pragma unroll
        for (unsigned int i=0; i<VECX2; i++) {
            data_enc.data[i] = huffman_data.data[i];
        }

        output[outpos] = aes_256(data_enc.datalong, config_data, round_keys[0], round_keys[1]);

        outpos = outpos + 1;

    } while (!huffman_data.last);
}

void kernel aes_encrypt0 (global long8 *restrict out, int8 config_data) {
    aes_encrypt_internal(out, config_data, 0);
}

void kernel aes_keygen_enc (int8 key)
{
    long16 round_key_lsb;
    long16 round_key_msb;

    round_key_lsb = aes_key_256(key, 0x01);
    round_key_msb = aes_key_256(key, 0x02);

    write_channel_intel(chan_lsb, round_key_lsb);
    write_channel_intel(chan_msb, round_key_msb);
}

kernel void aes_decrypt0 (global long8 * restrict in, global long8 * restrict out, int8 config_data, unsigned int N)
{
  long16 round_keys[2];
  round_keys[0] = read_channel_intel(chan_lsb_decrypt);
  round_keys[1] = read_channel_intel(chan_msb_decrypt);

  for (unsigned int k=0; k<N; k++) 
  {
    long8 data = in[k];
    out[k] = aes_256_decrypt(data, config_data, round_keys[0], round_keys[1]);
  }
}

kernel void aes_keygen_dec (int8 key)
{    
    long16 round_key_lsb;
    long16 round_key_msb;

    round_key_lsb = aes_key_256_decrypt(key, 0x01);
    round_key_msb = aes_key_256_decrypt(key, 0x02);

    write_channel_intel(chan_lsb_decrypt, round_key_lsb);
    write_channel_intel(chan_msb_decrypt, round_key_msb);  
}
