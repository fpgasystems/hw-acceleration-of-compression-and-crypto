#ifndef INC_COMPNCRYPT_H
#define INC_COMPNCRYPT_H

// NOTE: GZIP_ENGINES and VEC values for the host and kernel must match 
// If either of the variables were otherwise defined in the kernel complation, 
// they must also be defined the same for the host compilation 
#ifndef VEC
#define VEC 16
#endif

#ifndef GZIP_ENGINES
#define GZIP_ENGINES 1
#endif

#ifndef AES_ENGINES
#define AES_ENGINES 1
#endif

struct gzip_out_info_t {
  // location of first uncompressed byte from the input stream
  unsigned int fvp[GZIP_ENGINES];
  // lz compressed file size
  unsigned int compsize_lz[GZIP_ENGINES];
  // final compressed file size
  unsigned int compsize_huffman[GZIP_ENGINES];
};

// The maximum number of nodes in the Huffman tree is 2^(8+1)-1 = 511 
#define MAX_TREE_NODES 511
#define MAX_HUFFCODE_BITS 16
//#define DEBUGTREE

typedef struct {
  unsigned char *BytePtr;
  unsigned int  BitPos;
} huff_bitstream_t;

typedef struct {
  int Symbol;
  unsigned int Count;
  unsigned int Code;
  unsigned int Bits;
} huff_sym_t;


typedef struct huff_encodenode_struct huff_encodenode_t;

struct huff_encodenode_struct {
  huff_encodenode_t *ChildA, *ChildB;
  int Count;
  int Symbol;
  int Level;
};

typedef struct huff_decodenode_struct huff_decodenode_t;

struct huff_decodenode_struct {
  huff_decodenode_t *ChildA, *ChildB;
  int Symbol;
};

#endif
