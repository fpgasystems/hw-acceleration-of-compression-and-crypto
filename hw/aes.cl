// AES-256 call and key calculation

# pragma OPENCL EXTENSION cl_intel_channels : enable 

long8 aes_256(long8 x, int8 config, long16 key_lsb, long16 key_msb);
long16 aes_key_256(int8 x, char flax);

channel long16 chan_msb;
channel long16 chan_lsb;

// Using HDL library components
kernel void aes (global long8 * restrict in, global long8 * restrict out, int8 config_data, int N)
{
  long16 round_keys[2];
  round_keys[0] = read_channel_intel(chan_lsb);
  round_keys[1] = read_channel_intel(chan_msb);

  for (int k=0; k<N; k++) 
  {
    long8 data = in[k];
    char processing_flags = 1;
    
    out[k] = aes_256(data, config_data, round_keys[0], round_keys[1]);
  }
}

kernel void aes_keygen (int8 key)
{    
    long16 round_key_lsb;
    long16 round_key_msb;

    round_key_lsb = aes_key_256(key,0x01);
    round_key_msb = aes_key_256(key,0x02);

    write_channel_intel(chan_lsb, round_key_lsb);
    write_channel_intel(chan_msb, round_key_msb);  
}

