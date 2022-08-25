long8 aes_256 (long8 x, char flags) { return x << 16 | x >> 16; };
long16 aes_key_256(int8 x) {return x;};
long8 aes_256_decrypt (long8 x, char flags) { return x << 16 | x >> 16; };
long16 aes_key_256_decrypt(int8 x) {return x;};