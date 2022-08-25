#ifndef HELPERS_H
#define HELPERS_H

//-----------------
// Data definition
//-----------------
// Data types
typedef struct{
     uint cntr_nonce[2];
     uint elements[2];
     uint iv[4];
} aes_config;

typedef struct{
     uint key[8];
} key_config;

//---------------------------------------------------------------------------------------
//  HELPER FUNCTION: GetFileSize()
//---------------------------
// Returns number of bytes in text file that is read
//----------------------------------------------------------------------------------------

unsigned long get_filesize(FILE *f)
{
  long size;

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  if (size < 0)
    exit(1);

  return size;
}

#endif
