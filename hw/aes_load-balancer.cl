// AES encryption load balancer kernel

#include "gzip_channels.h"

void kernel aes_loadbalancer(
  int8 key,
  global long8 *restrict out,
  int8 config_data,
  unsigned int n_pages,
  unsigned int mem_offset)
{
  struct gzip_to_aes_t datain;
  struct aes_enc_setup_t setup;
  struct gzip_header_t header;

  unsigned int pages = 0;
  unsigned char aes_engine_id = 0;
  unsigned char gzip_engine_id = 0;
  bool first = true;

  setup.key = key;
  setup.out = out;
  setup.config_data = config_data;
  setup.offset = 0;

  do {
    switch (gzip_engine_id) {
      case 0:
        datain = read_channel_intel(ch_gzip2load[0]);
        if (datain.last)
          header = read_channel_intel(ch_gzip2header[0]);
        break;
#if GZIP_ENGINES > 1
      case 1:
        datain = read_channel_intel(ch_gzip2load[1]);
        if (datain.last)
          header = read_channel_intel(ch_gzip2header[1]);
        break;
#endif
#if GZIP_ENGINES > 2
      case 2:
        datain = read_channel_intel(ch_gzip2load[2]);
        if (datain.last)
          header = read_channel_intel(ch_gzip2header[2]);
        break;
#endif
#if GZIP_ENGINES > 3
      case 3:
        datain = read_channel_intel(ch_gzip2load[3]);
        if (datain.last)
          header = read_channel_intel(ch_gzip2header[3]);
        break;
#endif
    }

    switch (aes_engine_id) {
      case 0:
        if (first)
          write_channel_intel(ch_aes_enc_setup[0], setup);
        write_channel_intel(ch_load2aes[0], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[0], header);
        break;
#if AES_ENGINES > 1
      case 1: 
        if (first)
          write_channel_intel(ch_aes_enc_setup[1], setup);
        write_channel_intel(ch_load2aes[1], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[1], header);
        break;
#endif
#if AES_ENGINES > 2
      case 2:
        if (first)
          write_channel_intel(ch_aes_enc_setup[2], setup);
        write_channel_intel(ch_load2aes[2], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[2], header);
        break;
#endif
#if AES_ENGINES > 3
      case 3:
        if (first)
          write_channel_intel(ch_aes_enc_setup[3], setup);
        write_channel_intel(ch_load2aes[3], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[3], header);
        break;
#endif
#if AES_ENGINES > 4
      case 4:
        if (first)
          write_channel_intel(ch_aes_enc_setup[4], setup);
        write_channel_intel(ch_load2aes[4], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[4], header);
        break;
#endif
#if AES_ENGINES > 5
      case 5:
        if (first)
          write_channel_intel(ch_aes_enc_setup[5], setup);
        write_channel_intel(ch_load2aes[5], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[5], header);
        break;
#endif
#if AES_ENGINES > 6
      case 6:
        if (first)
          write_channel_intel(ch_aes_enc_setup[6], setup);
        write_channel_intel(ch_load2aes[6], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[6], header);
        break;
#endif
#if AES_ENGINES > 7
      case 7:
        if (first)
          write_channel_intel(ch_aes_enc_setup[7], setup);
        write_channel_intel(ch_load2aes[7], datain);
        if (datain.last)
          write_channel_intel(ch_header2aes[7], header);
        break;
#endif
    }

    first = false;

    if (datain.last) {
      pages++;
      aes_engine_id++;
      gzip_engine_id++;
      first = true;
      setup.offset += mem_offset;
    }

    if (aes_engine_id == AES_ENGINES)
      aes_engine_id = 0;

    if (gzip_engine_id == GZIP_ENGINES)
      gzip_engine_id = 0;

  } while (pages < n_pages);
}
