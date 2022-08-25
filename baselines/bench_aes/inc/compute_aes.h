#ifndef COMPUTE_AES
#define COMPUTE_AES

#include "aes.h"

#define MAX_NUM_THREADS 14

class compute_aes {
  public:
    unsigned char initKey[32];
    unsigned char* KEYS_enc;
    unsigned char* KEYS_dec;
    unsigned char ivec[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    unsigned char ctr_ivec[8]  = {0, 1, 2, 3, 4, 5, 6, 7};
    unsigned char ctr_nonce[4] = {0, 1, 2, 3};

    compute_aes(){

        for (unsigned int i = 0; i < 32; i++) {
            initKey[i] = (unsigned char)i;
        }
        KEYS_enc = (unsigned char*)malloc(16*15);
        KEYS_dec = (unsigned char*)malloc(16*15);
        AES_256_Key_Expansion(initKey, KEYS_enc);
        AES_256_Decryption_Keys(KEYS_enc, KEYS_dec);
    }

    ~compute_aes(){
        free(KEYS_enc);
        free(KEYS_dec); 
    }
    
    void encrypt_file(unsigned char* originalValues, unsigned int inNumWords, unsigned char* encryptedValues, unsigned int mode);
    void decrypt_file(unsigned char* encryptedValues, unsigned int inNumWords, unsigned char* decryptedValues, unsigned int mode);
};

void compute_aes::encrypt_file(unsigned char* originalValues, uint32_t inNumWords, unsigned char* encryptedValues, unsigned int mode) {    
    switch(mode){
        case 1: AES_CBC_encrypt(originalValues, encryptedValues, ivec, (uint32_t) inNumWords, KEYS_enc, 14);
        case 2: AES_CTR_encrypt(originalValues, encryptedValues, ctr_ivec, ctr_nonce, (uint32_t) inNumWords, KEYS_enc, 14);
        case 3: AES_ECB_encrypt(originalValues, encryptedValues, (uint32_t) inNumWords, KEYS_enc, 14);
    } 
}

void compute_aes::decrypt_file(unsigned char* encryptedValues, uint32_t inNumWords, unsigned char* decryptedValues, unsigned int mode) {    
    switch(mode){
        case 1: AES_CBC_decrypt(encryptedValues, decryptedValues, ivec, (uint32_t) inNumWords, KEYS_dec, 14);
        case 2: AES_CTR_encrypt(encryptedValues, decryptedValues, ctr_ivec, ctr_nonce, (uint32_t) inNumWords, KEYS_enc, 14);
        case 3: AES_ECB_decrypt(encryptedValues, decryptedValues, (uint32_t) inNumWords, KEYS_dec, 14);
    }
}

#endif
