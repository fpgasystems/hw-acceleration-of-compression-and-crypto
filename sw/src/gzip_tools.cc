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

//---------------------------------------------------------------------------------------------------------
// GZIP COMPRESSION
// TODO description
// Author: Mohamed Abdelfattah
//---------------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------
//  INCLUDES AND OPENCL VARIABLES
//---------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stack>

#include "gzip_tools.h"

using namespace std;

//---------------------------------------------------------------------------------------
//  FUNCTION IMPLEMENTATIONS
//---------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------
//  HELPER FUNCTION: Compute_Huffman() - Histogram and Huffman Tree computation
//---------------------------
//  
//----------------------------------------------------------------------------------------

unsigned char Compute_Huffman(unsigned char *input, unsigned int insize, unsigned int *huftable, huff_encodenode_t **root) {

  huff_sym_t sym[256];
  unsigned int k;

  // Calculate and sort histogram for input data
  unsigned char marker = _Huffman_Hist(input, sym, insize);

  //inject marker into histogram to improve its encoding
  //set marker count to half the file size

  sym[marker].Count = insize/3;

  huff_encodenode_t *nodes = (huff_encodenode_t *)malloc(MAX_TREE_NODES * sizeof(huff_encodenode_t));
  if (!nodes)
  {
    printf("[Huffman Tree] Pointer nodes is NULL \n");
    exit(1);
  }

  // Initialize all leaf nodes
  int num_symbols = 0;
  for (k = 0; k < 256; k++)
  {
    if (sym[k].Count > 0)
    {
      nodes[num_symbols].Symbol = sym[k].Symbol;
      nodes[num_symbols].Count = sym[k].Count;
      nodes[num_symbols].Level = 0;
      nodes[num_symbols].ChildA = (huff_encodenode_t *) 0;
      nodes[num_symbols].ChildB = (huff_encodenode_t *) 0;
      num_symbols++;
    }
  }

  // Build Huffman tree
  *root = _Huffman_MakeTree(sym, nodes, num_symbols); 

  //_Tree_Debug_Print(tree);

  for (k = 0; k < 256; k++)
  {
    huftable[k] = (sym[k].Bits << 16) | sym[k].Code;
    if (sym[k].Bits > 16)
    {
      printf("CANNOT CREATE A HUFFMAN TREE, SOMETHING IS WRONG!!! \n");
      exit(1);
    }
  }

  //print huffman table
#ifdef DEBUG
  printf("HUFFTABLE: \n");
  for (k = 0; k < 256; k++)
  {
    unsigned int a = huftable[k];
    unsigned short cod = a & 0xFFFF;
    unsigned short len = a >> 16;
    printf("%d - %c: %x (%d)\n",k,k,cod,len);
  }
#endif
  return marker;
}


//---------------------------------------------------------------------------------------
//  HELPER FUNCTION: LZ_Uncompress() - Uncompress a block of data using an LZ77 decoder.
//---------------------------
//  in      - Input (compressed) buffer.
//  out     - Output (uncompressed) buffer. This buffer must be large
//            enough to hold the uncompressed data.
//  insize  - Number of input bytes.
//----------------------------------------------------------------------------------------

int LZ_Uncompress( unsigned char *in, unsigned char *out, unsigned int insize )
{	
  unsigned char marker, symbol;
  unsigned int  i, inpos, outpos, length, offset;
  int err_count = 0;

  /* Do we have anything to uncompress? */
  if( insize < 1 )
  {
    return err_count;
  }

  /* Get marker symbol from input stream */
  marker = in[ 0 ];

  //printf("marker = %d\n",marker);
  inpos = 1;

  //DEBUG print text
  i = 0;

  /* Main decompression loop */
  outpos = 0;
  do
  {	
    symbol = in[ inpos ++ ];

    //printf("curr symbol = %d\n",symbol);
    if( symbol == marker )
    {
      // We had a marker byte 
      if( in[ inpos ] == 0 )
      {
        // It was a single occurrence of the marker byte 
        out[ outpos ++ ] = marker;
        ++ inpos;
      }
      else
      {
        // Extract true length and offset 
        length = (in[ inpos ] >> 4) + 3;
        offset = in[ inpos ] & 0xf;
        inpos++;

        offset |= (in[ inpos ] & 0x7f) << 4;

        //do we have another 7 bits?
        if(in[ inpos ] & 0x80)
        {
          inpos ++;
          offset |= (in[ inpos ] & 0x7f) << 11;
        }

        inpos++;

        if (length > 18) exit(1);
        // Copy corresponding data from history window 
        for( i = 0; i < length; ++ i )
        {
          if (offset > outpos) {
            printf("Offset %u is greater than file position %u\n", offset, outpos);
            // Write a character to outpos anyway so that
            // later errors are detected correctly
            out[ outpos ] = 'x';
            err_count++;
          }
          else {
            out[ outpos ] = out[ outpos - offset ];
          }
          ++ outpos;
        }
      }
    }
    else
    {
      // No marker, plain copy 
      out[ outpos ++ ] = symbol;
    }
  }
  while( inpos < insize );

  return err_count;
}


static void _Huffman_InitBitstream( huff_bitstream_t *stream, unsigned char *buf )
{
  stream->BytePtr  = buf;
  stream->BitPos   = 0;
}


/*************************************************************************
 * _Huffman_ReadBit() - Read one bit from a bitstream.
 *************************************************************************/

static unsigned int _Huffman_ReadBit( huff_bitstream_t *stream )
{
  unsigned int  x, bit;
  unsigned char *buf;

  /* Get current stream state */
  buf = stream->BytePtr;
  bit = stream->BitPos;

  /* Extract bit */
  x = (*buf & (1<<(7-bit))) ? 1 : 0;
  bit = (bit+1) & 7;
  if( !bit )
  {
    ++ buf;
  }

  /* Store new stream state */
  stream->BitPos = bit;
  stream->BytePtr = buf;

  return x;
}


/*************************************************************************
 * _Huffman_Read8Bits() - Read eight bits from a bitstream.
 *************************************************************************/

static unsigned int _Huffman_Read8Bits( huff_bitstream_t *stream )
{
  unsigned int  x, bit;
  unsigned char *buf;

  /* Get current stream state */
  buf = stream->BytePtr;
  bit = stream->BitPos;

  /* Extract byte */
  x = (*buf << bit) | (buf[1] >> (8-bit));
  ++ buf;

  /* Store new stream state */
  stream->BytePtr = buf;

  return x;
}


/*************************************************************************
 * _Huffman_WriteBits() - Write bits to a bitstream.
 *************************************************************************/

static void _Huffman_WriteBits( huff_bitstream_t *stream, unsigned int x, unsigned int bits )
{
  unsigned int  bit, count;
  unsigned char *buf;
  unsigned int  mask;

  /* Get current stream state */
  buf = stream->BytePtr;
  bit = stream->BitPos;

  /* Append bits */
  mask = 1 << (bits-1);
  for( count = 0; count < bits; ++ count )
  {
    *buf = (*buf & (0xff^(1<<(7-bit)))) + ((x & mask ? 1 : 0) << (7-bit));
    x <<= 1;
    bit = (bit+1) & 7;
    if( !bit )
    {
      buf++;
      *buf=0;
    }
  }

  /* Store new stream state */
  stream->BytePtr = buf;
  stream->BitPos  = bit;
}


/*************************************************************************
 * _Huffman_Hist() - Calculate (sorted) histogram for a block of data.
 *************************************************************************/

unsigned char _Huffman_Hist( unsigned char *in, huff_sym_t *sym, unsigned int size )
{
  int k;

  //Clear/init histogram 
  for( k = 0; k < 256; ++ k )
  {
    sym[k].Symbol = k;
    sym[k].Count  = 0;
    sym[k].Code   = 0;
    sym[k].Bits   = 0;
  }

  //build histogram 
  for( k = size; k; -- k )
  {
    sym[*in ++ % 256].Count ++;
  }

  //find marker 
  unsigned char marker = 0;
  for( k = 0; k < 256; k++ )
  {
    if(sym[k].Count < sym[marker].Count)
      marker = (unsigned char)k;
  }

  return marker;
}


/*************************************************************************
 * _Huffman_StoreTree() - Store a Huffman tree in the output_lz stream and
 * in a look-up-table (a symbol array).
 *************************************************************************/

static void _Huffman_StoreTree( huff_encodenode_t *node, huff_sym_t *sym, unsigned int code, unsigned int bits )
{
  unsigned int sym_idx;

  /* Is this a leaf node? */
  if( node->Symbol >= 0 )
  {

    /* Find symbol index */
    for( sym_idx = 0; sym_idx < 256; ++ sym_idx )
    {
      if( sym[sym_idx].Symbol == node->Symbol ) break;
    }

    /* Store code info in symbol array */
    sym[sym_idx].Code = code;
    sym[sym_idx].Bits = bits;
    return;
  }

  /* Branch A */
  _Huffman_StoreTree( node->ChildA, sym, (code<<1)+0, bits+1 );

  /* Branch B */
  _Huffman_StoreTree( node->ChildB, sym, (code<<1)+1, bits+1 );
}


/*************************************************************************
 * _Huffman_MakeTree() - Generate a Huffman tree.
 *************************************************************************/

huff_encodenode_t * _Huffman_MakeTree( huff_sym_t *sym, huff_encodenode_t *nodes, unsigned int num_symbols )
{
  huff_encodenode_t *node_1, *node_2, *root;
  unsigned int k, nodes_left, next_idx;
  /* Build tree by joining the lightest nodes until there is only
     one node left (the root node). */
  root = (huff_encodenode_t *) 0;
  nodes_left = num_symbols;
  next_idx = num_symbols;
  while( nodes_left > 1 )
  {
    /* Find the two lightest nodes */
    node_1 = (huff_encodenode_t *) 0;
    node_2 = (huff_encodenode_t *) 0;
    for( k = 0; k < next_idx; k++ )
    {
      if( nodes[k].Count > 0 )
      {
        if( !node_1 || (nodes[k].Count <= node_1->Count) )
        {
          node_2 = node_1;
          node_1 = &nodes[k];
        }
        else if( !node_2 || (nodes[k].Count <= node_2->Count) )
        {
          node_2 = &nodes[k];
        }
      }
    }

    /* Join the two nodes into a new parent node */
    root = &nodes[next_idx];
    root->ChildA = node_1;
    root->ChildB = node_2;
    root->Count = node_1->Count + node_2->Count;
    root->Symbol = -1;
    node_1->Count = 0;
    node_2->Count = 0;
    ++ next_idx;
    -- nodes_left;
  }

  //limit tree depth to 16 (with root on level 0)
  //this is to produce length-limited prefix codes (huffman codes)
  //this is important for the current kernel which can only deal with 16-bit huffman codes

  //traverse tree and annotate level
  _Tree_Annotate_Depth(root);

  //print for debug
#ifdef DEBUGTREE
  _Tree_Debug_Print(root);
#endif

  //check if our tree is of max depth = 16
  //if not then we'll cut a piece off of it 
  //and paste it higher up in the tree

  int curr_tree_depth = _Tree_Depth(root);
  int max_tree_depth = MAX_HUFFCODE_BITS;

  while(curr_tree_depth > max_tree_depth)
  {
#ifdef DEBUGTREE
    printf("Depth before processing: %d\n",curr_tree_depth);
#endif

    //any node with level 16 will have to be processed
    _Tree_Limit_Depth(root,16);

    //check tree depth again
    curr_tree_depth = _Tree_Depth(root);
#ifdef DEBUGTREE
    printf("Depth after processing: %d\n",curr_tree_depth);
#endif
  }

  //print for debug
#ifdef DEBUGTREE
  _Tree_Debug_Print(root);
#endif

  /* Store the tree in the output_lz stream, and in the sym[] array (the
     latter is used as a look-up-table for faster encoding) */
  _Huffman_StoreTree( root, sym, 0, 0 );
  return root;

}

/*************************************************************************
 * _Tree_Limit_Depth() - Reorganize tree so that it has a limited depth
 *************************************************************************/

static int _Tree_Limit_Depth(huff_encodenode_t *root, int max_depth)
{
  int max_depth_found = _Tree_Depth(root);

  stack<huff_encodenode_t *> traversal_stack;

  //add root node
  traversal_stack.push(root);

  huff_encodenode_t *deep_node;

  while( !traversal_stack.empty() )
  {
    //pop first node
    huff_encodenode_t *current_node = traversal_stack.top();
    traversal_stack.pop();

    //find current_depth
    if(current_node->Level > max_depth_found)
      max_depth_found = current_node->Level;

    //children
    huff_encodenode_t *child_a = current_node->ChildA;
    huff_encodenode_t *child_b = current_node->ChildB;

    //if current_depth is 16 and this node has children
    //we need to move that node to a lesser level 15, or 14, or 13 etc. deeper preffered
    if( current_node->Level == max_depth && (child_a || child_b) )
    {
#ifdef DEBUGTREE
      printf("found deep node!\n");
#endif
      deep_node = current_node;
      break;
    }

    //else keep traversing
    if(child_a)
      traversal_stack.push(child_a);
    if(child_b)
      traversal_stack.push(child_b);
  }

  //perform sugery on the deep node
  //1- find a candidate node (surrogate) to tie its children to (must be leaf)
  //2- tie the children to the surrogate node & set surrogate to nosymbol
  //3- set deep node symbol to surrogates old symbol and cut the links to children
  //4- update tree levels

  //1- find a candidate node (surrogate) to tie deep_node's children to (must be leaf)
  while(!traversal_stack.empty())
    traversal_stack.pop();
  huff_encodenode_t *surrogate_node = NULL;
  int current_acceptable_level = 16;
  while(!surrogate_node)
  {
    current_acceptable_level--;
#ifdef DEBUGTREE
    printf("current_acceptable_level = %d\n",current_acceptable_level);
#endif
    traversal_stack.push(root);
    while( !traversal_stack.empty() )
    {
      //pop first node
      huff_encodenode_t *current_node = traversal_stack.top();
      traversal_stack.pop();

      //find current_depth
      if(current_node->Level > max_depth_found)
        max_depth_found = current_node->Level;

      //children
      huff_encodenode_t *child_a = current_node->ChildA;
      huff_encodenode_t *child_b = current_node->ChildB;

      //if current_depth is 16 and this node has children
      //we need to move that node to a lesser level 15, or 14, or 13 etc. deeper preffered
      if( current_node->Level == current_acceptable_level && current_node->Symbol != -1 )
      {

#ifdef DEBUGTREE
        printf("found surrogate node!\n");
#endif
        surrogate_node = current_node;
        break;
      }

      //else keep traversing
      if(child_a)
        traversal_stack.push(child_a);
      if(child_b)
        traversal_stack.push(child_b);
    }
  }

  //2- tie the children to the surrogate node & set surrogate to nosymbol
  surrogate_node->ChildA = deep_node->ChildA;
  surrogate_node->ChildB = deep_node->ChildB;
  int surrogate_old_symbol = surrogate_node->Symbol;
  surrogate_node->Symbol = -1;

  //3- set deep node symbol to surrogates old symbol and cut the links to children
  deep_node->Symbol = surrogate_old_symbol;
  deep_node->ChildA = NULL;
  deep_node->ChildB = NULL;

  //4- update tree levels
  _Tree_Annotate_Depth(root);

  return max_depth_found;	
}

/*************************************************************************
 * _Tree_Annotate_Depth() - Annotate depth on tree nodes
 *************************************************************************/

static void _Tree_Annotate_Depth(huff_encodenode_t *root)
{
  stack<huff_encodenode_t *> traversal_stack;

  root->Level = 0;

  //add root node
  traversal_stack.push(root);

  while( !traversal_stack.empty() )
  {
    //pop first node
    huff_encodenode_t *current_node = traversal_stack.top();
    traversal_stack.pop();

    //current level we're at plus one
    int child_level = current_node->Level + 1;

    //set children level and push onto stack
    huff_encodenode_t *child_a = current_node->ChildA;
    huff_encodenode_t *child_b = current_node->ChildB;

    if(child_a)
    {
      child_a->Level = child_level;
      traversal_stack.push(child_a);
    }
    if(child_b)
    {
      child_b->Level = child_level;
      traversal_stack.push(child_b);
    }
  }
}

/*************************************************************************
 * _Tree_Depth() - returns the current tree depth
 *************************************************************************/

static int _Tree_Depth(huff_encodenode_t *root)
{
  stack<huff_encodenode_t *> traversal_stack;

  //add root node
  traversal_stack.push(root);

  int max_depth_found = 0;

  while( !traversal_stack.empty() )
  {
    //pop first node
    huff_encodenode_t *current_node = traversal_stack.top();
    traversal_stack.pop();

    //find current_depth
    if(current_node->Level > max_depth_found)
      max_depth_found = current_node->Level;

    //set children level and push onto stack
    huff_encodenode_t *child_a = current_node->ChildA;
    huff_encodenode_t *child_b = current_node->ChildB;

    if(child_a)
      traversal_stack.push(child_a);
    if(child_b)
      traversal_stack.push(child_b);
  }

  return max_depth_found;
}

/*************************************************************************
 * _Tree_Debug_Print() - prints the tree to std out for debug
 *************************************************************************/

static void _Tree_Debug_Print(huff_encodenode_t *root)
{
  stack<huff_encodenode_t *> traversal_stack;
  //add root node
  traversal_stack.push(root);

  //print debug
  while( !traversal_stack.empty() )
  {
    //pop first node
    huff_encodenode_t *current_node = traversal_stack.top();
    traversal_stack.pop();

    //print level for debug
    if(current_node->Symbol != -1)
      printf("Symbol: %c, ",current_node->Symbol);
    printf("Level: %d\n",current_node->Level);

    //set children level and push onto stack
    huff_encodenode_t *child_a = current_node->ChildA;
    huff_encodenode_t *child_b = current_node->ChildB;

    if(child_a)
      traversal_stack.push(child_a);
    if(child_b)
      traversal_stack.push(child_b);
  }
}

/*************************************************************************
 * _Huffman_RecoverTree() - Recover a Huffman tree from a bitstream.
 *************************************************************************/

static huff_decodenode_t * _Huffman_RecoverTree( huff_decodenode_t *nodes,
    huff_bitstream_t *stream, unsigned int *nodenum )
{
  huff_decodenode_t * this_node;

  /* Pick a node from the node array */
  this_node = &nodes[*nodenum];
  *nodenum = *nodenum + 1;

  /* Clear the node */
  this_node->Symbol = -1;
  this_node->ChildA = (huff_decodenode_t *) 0;
  this_node->ChildB = (huff_decodenode_t *) 0;

  /* Is this a leaf node? */
  if( _Huffman_ReadBit( stream ) )
  {
    /* Get symbol from tree description and store in lead node */
    this_node->Symbol = _Huffman_Read8Bits( stream );

    return this_node;
  }

  /* Get branch A */
  this_node->ChildA = _Huffman_RecoverTree( nodes, stream, nodenum );

  /* Get branch B */
  this_node->ChildB = _Huffman_RecoverTree( nodes, stream, nodenum );

  return this_node;
}

/*************************************************************************
 *                            PUBLIC FUNCTIONS                            *
 *************************************************************************/


/*************************************************************************
 * Huffman_Uncompress() - Uncompress a block of data using a Huffman
 * decoder.
 *  in      - Input (compressed) buffer.
 *  out     - Output (uncompressed) buffer. This buffer must be large
 *            enough to hold the uncompressed data.
 *  insize  - Number of input bytes.
 *  outsize - Number of output_lz bytes.
 *************************************************************************/

void Huffman_Uncompress( unsigned char *in, unsigned char *out, huff_encodenode_t *root,
    unsigned int insize, unsigned int outsize, unsigned char marker )
{
  huff_encodenode_t *node;
  huff_bitstream_t  stream;
  unsigned int      k;
  unsigned char     *buf;

  // Do we have anything to decompress? 
  if( insize < 1 ) return;

  // Initialize bitstream
  _Huffman_InitBitstream( &stream, in );

  //_Tree_Debug_Print(root);

  // Decode input stream 
  buf = out;
  for( k = 0; k < outsize; k++ )
  {
    //printf("Current Code: %x\n",*(stream.BytePtr));

    // Traverse tree until we find a matching leaf node
    node = root;
    while( node->Symbol < 0 )
    {
      // Get next node 
      if( _Huffman_ReadBit( &stream ) )
        node = node->ChildB;
      else
        node = node->ChildA;
    }

    // We found the matching leaf node and have the symbol 
    *buf ++ = (unsigned char) node->Symbol;

    //if we have a marker then the length and offset will come here
    //unencoded to any huffman

    if((node->Symbol == (int)marker) && (k > 0))
    {
      unsigned char match_length = _Huffman_Read8Bits( &stream );

      *buf ++ = match_length;
      k++;

      //marker byte
      if(match_length == 0)
      {
        continue;
      }

      unsigned char first_offset_byte = _Huffman_Read8Bits( &stream );
      *buf ++ = first_offset_byte;
      k++;

      if(first_offset_byte & 0x80)
      {
        unsigned char second_offset_byte = _Huffman_Read8Bits( &stream );
        *buf ++ = second_offset_byte;
        k++;
      }
    }
  }
}

void Print_Huffman(huff_encodenode_t *tree){
  _Tree_Debug_Print(tree);
}
