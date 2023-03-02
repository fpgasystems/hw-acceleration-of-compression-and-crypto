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
// Author: Mohamed Abdelfattah
//---------------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------
//  INCLUDES AND OPENCL VARIABLES
//---------------------------------------------------------------------------------------

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <math.h>
#include <stack>
#include <fstream>
#include <map>
#include <unistd.h> // usleep

#include "CL/opencl.h"
#include "AOCLUtils/aocl_utils.h"

#include "gzip_tools.h"
#include "helpers.h"

#define TIMES 64
#define STRING_BUFFER_LEN 1024
static unsigned int LINE    = 512/32;
static unsigned int BUFSIZE = LINE*4;

#define xstr(s) str(s)
#define str(s) #s

using namespace std;
using namespace aocl_utils;

//#define DEBUG

//---------------------------------------------------------------------------------------
// ACL runtime configuration
//---------------------------------------------------------------------------------------

// All default kernels end in 0. There may be 1,2,3 versions as well, depending on the
// value of GZIP_ENGINES
const char *gzip_kernel_name[] = {
  "load_lz0",
  "load_huff_coeff0",
  "lz77_0",
  "huff0",
#if GZIP_ENGINES > 1
  "lz77_1",
  "huff1",
#endif
#if GZIP_ENGINES > 2
  "lz77_2",
  "huff2",
#endif
#if GZIP_ENGINES > 3
  "lz77_3",
  "huff3",
#endif
  "store_huff0"
};

enum GZIPKernels {
  LOAD_LZ = 0,
  LOAD_HUFF_COEFF,
  LZ770,
  HUFF0,
#if GZIP_ENGINES > 1
  LZ771,
  HUFF1,
#endif
#if GZIP_ENGINES > 2
  LZ772,
  HUFF2,
#endif
#if GZIP_ENGINES > 3
  LZ773,
  HUFF3,
#endif
  STORE_HUFF,
  NUM_GZIP_KERNELS
};

const char *aes_kernel_name[] = {
  "aes_encrypt0",
  "aes_decrypt0",
  "aes_keygen_enc",
  "aes_keygen_dec"
};

enum AESKernels {
  AES_ENCRYPT = 0,
  AES_DECRYPT,
  AES_ENCRYPT_KEY,
  AES_DECRYPT_KEY,
  NUM_AES_KERNELS
};

// OpenCL runtime configuration
static cl_platform_id platform    = NULL;
static cl_device_id device        = NULL;
static cl_context context         = NULL;
static cl_program program         = NULL;
static cl_int status              = 0;

static cl_command_queue gzip_queue[NUM_GZIP_KERNELS];
static cl_kernel gzip_kernel[NUM_GZIP_KERNELS];

static cl_command_queue aes_queue = NULL;
static cl_kernel aes_kernel[NUM_AES_KERNELS];

//---------------------------------------------------------------------------------------
//  DATA BUFFERS on FPGA
//---------------------------------------------------------------------------------------
// GZIP
static cl_mem input_buf          = NULL;
static cl_mem huftable_buf       = NULL;
static cl_mem out_info_buf       = NULL;
// AES
static cl_mem output_aes_enc_buf = NULL;
static cl_mem output_aes_dec_buf = NULL;

//---------------------------------------------------------------------------------------
//  FUNCTION PROTOTYPES
//---------------------------------------------------------------------------------------

bool init(bool use_emulator);
void cleanup();

void compress_and_encrypt(const char *filename);

time_profiles_s offload_to_FPGA(unsigned char *input, unsigned int *huftable,
    unsigned int insize, unsigned int outsize, unsigned char marker, unsigned int &fvp,
    unsigned int *output_aes, unsigned int &compsize_lz, unsigned int &compsize_huffman);

int decompress_on_host(unsigned char *input, huff_encodenode_t *tree, unsigned int insize,
    unsigned int outsize, unsigned char marker, unsigned int fvp,
    unsigned short *output_huffman, unsigned int compsize_lz, unsigned int compsize_huffman,
    unsigned int remaining_bytes, double &time_decompress);

//---------------------------------------------------------------------------------------
//  MAIN FUNCTION
//---------------------------------------------------------------------------------------

int main(int argc, char **argv)
{
  Options options(argc, argv);

  // Optional argument to specify whether the emulator should be used.
  bool use_emulator = options.has("emulator");

  if (!init(use_emulator))
    return -1;

  if (argc > 1) {
    std::cout << "Input to compress: " << argv[1] << std::endl;
    compress_and_encrypt(argv[1]);
  }

  cleanup();
  return 0;
}

//---------------------------------------------------------------------------------------
//  COMPRESS AND ENCRYPT
//---------------------------
//  1- Create huffman tree and select marker on host
//  2- Deflate input file on *FPGA*
//  3- Inflate FPGA output on host and compare to input for verification
//  4- Print result and cleanup
//---------------------------------------------------------------------------------------

void compress_and_encrypt(const char *filename)
{
  //-------------------------------------------------------------------------------------
  // 0- Input file
  //-------------------------------------------------------------------------------------

  //Kernel Variables
  unsigned int insize, outsize;
  FILE *f;

  //Open input file
  f = fopen(filename, "rb");
  if (f == NULL)
  {
    printf("Unable to open %s\n", filename);
    exit(1);
  }

  //input files size
  insize = get_filesize(f);
  if (insize < 1)
  {
    printf("File %s is empty\n", filename);
    fclose(f);
    exit(1);
  }
  else
    std::cout<<"File size [B]: "<<insize<<std::endl;

  // Worst case output_huff buffer size (too pessimistic for now)
  outsize = insize*2;

  // Host Buffers
  unsigned char  *input          = (unsigned char *)alignedMalloc(insize);
#if GZIP_STORE
  unsigned short *output_huffman = (unsigned short *)alignedMalloc(outsize);
#else
  unsigned short *output_huffman = NULL;
#endif
  unsigned int   *output_aes     = (unsigned int *)alignedMalloc(outsize);

  // Read and close input file
  if (fseek(f, 0, SEEK_SET) != 0)
    exit(1);
  if (fread(input, 1, insize, f) != insize)
    exit(1);
  fclose(f);

  // here truncate the file so that the number of chars is a multiple of VEC
  int remaining_bytes = insize % (2*VEC);
  insize -= remaining_bytes;

  //-------------------------------------------------------------------------------------
  // 1- Create Huffman Tree on host
  //-------------------------------------------------------------------------------------
  unsigned int *huftable  = (unsigned int *)alignedMalloc(1024);
  huff_encodenode_t *tree = (huff_encodenode_t *)malloc(MAX_TREE_NODES * sizeof(huff_encodenode_t));
  huff_encodenode_t **root = &tree;
  unsigned char marker    = Compute_Huffman(input, insize, huftable, root);

  //-------------------------------------------------------------------------------------
  // 2- Send input file on *FPGA* for Compresion and Encryption
  //-------------------------------------------------------------------------------------
  unsigned int compsize_lz = 0;
  unsigned int compsize_huffman = 0;
  unsigned int fvp = 0;

  memset(output_aes, 0, outsize);

  time_profiles_s profiles = offload_to_FPGA(input, huftable, insize, outsize, marker,
      fvp, output_aes, compsize_lz, compsize_huffman);

  std::cout << "Remaining bytes: " << remaining_bytes << std::endl;
  std::cout << "Marker         : " << static_cast<unsigned>(marker) << std::endl;

  //-------------------------------------------------------------------------------------
  // 3- Decrypt data on *host* and compare to compressed data for verification
  //-------------------------------------------------------------------------------------
  unsigned short* output_aes_short = (unsigned short*) output_aes;

  //-------------------------------------------------------------------------------------
  // 4- Decrypt and decompress FPGA output on host and compare to input for verification
  //-----------------------------------------------------------------
  int numerrors = 0;
  double time_x_profile_decompress = 0.0;
  numerrors = decompress_on_host(input, tree, insize, outsize, marker, fvp, 
      output_aes_short, compsize_lz, compsize_huffman, remaining_bytes,
      time_x_profile_decompress);

  //---------------------------------------------------------------------------
  // 5- Print result and cleanup
  //---------------------------------------------------------------------------
  if (numerrors == 0)
    std::cout << "PASSED, no errors" << std::endl;
  else
    std::cerr << "FAILED, " << numerrors << " errors" << std::endl;

  double throughput_compNcrypt   = (double)insize / double(profiles.compNcrypt);
  double throughput_core_only    = (double)insize / double(profiles.core_only);
  double throughput_gzip_com     = (double)insize / double(profiles.gzip_com);
  double throughput_aes_enc      = (double)compsize_huffman / double(profiles.aes_enc);
  double throughput_aes_dec      = (double)compsize_huffman / double(profiles.aes_dec);
  double throughput_gzip_dec     = (double)insize / (time_x_profile_decompress*1.0e+9) ;

  printf("Compression Ratio = %.2f %% \n", (float)compsize_huffman/insize*100);

  float multiplier = 1.0;
#ifdef GZIP_ENGINES
  // Every engine should be doing identical work, but credit them as if they
  // had all done useful work
  multiplier *= GZIP_ENGINES; // TODO
#endif
  printf("Throughput compNcrypt       = %.5f GB/s \n", throughput_compNcrypt);
  printf("Throughput core only        = %.5f GB/s \n", throughput_core_only);
  printf("Throughput gzip compress    = %.5f GB/s \n", throughput_gzip_com);
  printf("Throughput aes encryption   = %.5f GB/s \n", throughput_aes_enc);
  printf("Throughput aes decryption   = %.5f GB/s \n", throughput_aes_dec);
  printf("Throughput gzip decompress  = %.5f GB/s \n", throughput_gzip_dec);

  //free buffers
  alignedFree(input);
#if GZIP_STORE
  alignedFree(output_huffman);
#endif
  alignedFree(output_aes);
}

//---------------------------------------------------------------------------------------
//  INIT FUNCTION
//---------------------------
//  1- Find device
//  2- Create context
//  3- Create command queues
//  4- Create/build program
//  5- Create kernels
//----------------------------------------------------------------------------------------

bool init(bool use_emulator)
{
  // Get the OpenCL platform.
  if (use_emulator) {
    platform = findPlatform("Intel(R) FPGA Emulation Platform for OpenCL(TM)");
  } else {
    platform = findPlatform("Intel(R) FPGA SDK for OpenCL(TM)");
  }
  if(platform == NULL) {
    std::cout << "ERROR: Unable to find Intel FPGA OpenCL platform" << std::endl;
    return false;
  }

  // Print platform information
  char char_buffer[STRING_BUFFER_LEN];
  std::cout << "Querying platform for info:" << std::endl;
  std::cout << "==========================" << std::endl;
  clGetPlatformInfo(platform, CL_PLATFORM_NAME, STRING_BUFFER_LEN, char_buffer, NULL);
  printf("%-40s = %s\n", "CL_PLATFORM_NAME", char_buffer);
  clGetPlatformInfo(platform, CL_PLATFORM_VENDOR, STRING_BUFFER_LEN, char_buffer, NULL);
  printf("%-40s = %s\n", "CL_PLATFORM_VENDOR ", char_buffer);
  clGetPlatformInfo(platform, CL_PLATFORM_VERSION, STRING_BUFFER_LEN, char_buffer, NULL);
  printf("%-40s = %s\n\n", "CL_PLATFORM_VERSION ", char_buffer);

  // Query the available OpenCL devices.
  scoped_array<cl_device_id> devices;
  cl_uint num_devices;

  devices.reset(getDevices(platform, CL_DEVICE_TYPE_ALL, &num_devices));

  // We'll just use the first device.
  device = devices[0];

  // Create the context.
  context = clCreateContext(NULL, 1, &device, NULL, NULL, &status);
  checkError(status, "Failed to create context");

  // Create command queues
  for (int k = 0; k < NUM_GZIP_KERNELS; ++k) {
    gzip_queue[k] = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
    checkError(status, "Failed to create command queue GZIP");
  }
  aes_queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
  checkError(status, "Failed to create command queue AES");

  // Create the program.
  std::string path = xstr(BUILD_FOLDER);
  path += "/compNcrypt";

  std::string binary_file = getBoardBinaryFile(path.c_str(), device);
  std::cout << "Programming FPGA with " << binary_file << std::endl;
  program = createProgramFromBinary(context, binary_file.c_str(), &device, 1);

  // Build the program that was just created.
  status = clBuildProgram(program, 0, NULL, "", NULL, NULL);
  checkError(status, "Failed to build program");

  for (int k = 0; k < NUM_GZIP_KERNELS; ++k) {
    // Create the kernel - name passed in here must match kernel name in the
    // original CL file, that was compiled into an AOCX file using the AOC tool
    const char *kernel_name = gzip_kernel_name[k];
    gzip_kernel[k] = clCreateKernel(program, kernel_name, &status);
    checkError(status, "Failed to create GZIP kernel %s", kernel_name);
  }

  for (int k = 0; k < NUM_AES_KERNELS; ++k) {
    const char *kernel_name = aes_kernel_name[k];
    aes_kernel[k] = clCreateKernel(program, kernel_name, &status);
    checkError(status, "Failed to create AES kernel %s", kernel_name);
  }
  return true;
}


//---------------------------------------------------------------------------------------
//  CLEANUP
//---------------------------
// Free the resources allocated during initialization
//---------------------------------------------------------------------------------------

void cleanup()
{
  //free kernel/queue/program/context
  for (int k = 0; k < NUM_GZIP_KERNELS; ++k) {
    if (gzip_kernel[k])
      clReleaseKernel(gzip_kernel[k]);
    if (gzip_queue[k])
      clReleaseCommandQueue(gzip_queue[k]);
  }
  for (int k = 0; k < NUM_AES_KERNELS; ++k) {
    if (aes_kernel[k])
      clReleaseKernel(aes_kernel[k]);
  }
  if (aes_queue)
    clReleaseCommandQueue(aes_queue);
  if (program)
    clReleaseProgram(program);
  if (context)
    clReleaseContext(context);

  //free in/out buffers
  if (input_buf)
    clReleaseMemObject(input_buf);
  if (huftable_buf)
    clReleaseMemObject(huftable_buf);
  if (out_info_buf)
    clReleaseMemObject(out_info_buf);
  if (output_aes_enc_buf)
    clReleaseMemObject(output_aes_enc_buf);
  if(output_aes_dec_buf)
    clReleaseMemObject(output_aes_dec_buf);
}


//---------------------------------------------------------------------------------------
//  Offload to FPGA
//---------------------------
//
//---------------------------------------------------------------------------------------

time_profiles_s offload_to_FPGA(unsigned char *input, unsigned int *huftable,
    unsigned int insize, unsigned int outsize, unsigned char marker, unsigned int &fvp,
    unsigned int *output_aes, unsigned int &compsize_lz, unsigned int &compsize_huffman)
{
  cl_int status;
  gzip_out_info_t out_info;

  aes_config aes_config_run;

  aes_config_run.elements[0] = insize/4;// N is number of lines, one line = 512b=64B
  aes_config_run.elements[1] = 0;
  aes_config_run.cntr_nonce[0] = 0x00000000; //rand(); // LS uint nonce
  aes_config_run.cntr_nonce[1] = 0x00000000; //rand(); // MS uint nonce
  aes_config_run.iv[0] = 0x00000000;//rand(); // LS uint iv
  aes_config_run.iv[1] = 0x00000000;//rand(); // uint iv
  aes_config_run.iv[2] = 0x00000000;//rand(); // uint iv
  aes_config_run.iv[3] = 0x00000000;//rand(); // MS uint iv

  key_config key_config_run;
  key_config_run.key[0] = 0x00000000; // LS uint
  key_config_run.key[1] = 0xFFFFFFFF;
  key_config_run.key[2] = 0xFFFFFFFF;
  key_config_run.key[3] = 0xFFFFFFFF;
  key_config_run.key[4] = 0xFFFFFFFF;
  key_config_run.key[5] = 0xFFFFFFFF;
  key_config_run.key[6] = 0x00000001;
  key_config_run.key[7] = 0x00000001; // MS uint

  // Input buffers
  input_buf = clCreateBuffer(context, CL_MEM_READ_ONLY, insize, NULL, &status);
  checkError(status, "Failed to create buffer for input");

  huftable_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, 1024, NULL, NULL);
  checkError(status, "Failed to create buffer for huftable");

  out_info_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(gzip_out_info_t), NULL, &status);
  checkError(status, "Failed to create buffer for out_info");

  // Output buffers
  output_aes_enc_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, outsize, NULL, &status);
  checkError(status, "Failed to create buffer for output_aes");

  output_aes_dec_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY, outsize, NULL, &status);
  checkError(status, "Failed to create buffer for output_aes_decryption");

  cl_event write_event[2];

  // Input data
  status = clEnqueueWriteBuffer(gzip_queue[LOAD_LZ], input_buf, CL_FALSE, 0, insize, input,
      0, NULL, &write_event[0]);
  checkError(status, "Failed to transfer raw input");

  // Huffman table
  status = clEnqueueWriteBuffer(gzip_queue[LOAD_HUFF_COEFF], huftable_buf, CL_TRUE, 0, 1024,
      huftable, 0, NULL, &write_event[1]);
  checkError(status, "Failed to transfer huftable");

  //-------------------------------------------------------------------------------------
  // [openCL] Set kernel arguments and enqueue commands into kernel
  //-------------------------------------------------------------------------------------

  unsigned argi, k;

  argi = 0;
  k = LOAD_LZ;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_mem), &input_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

  argi = 0;
  k = LOAD_HUFF_COEFF;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_mem), &huftable_buf);

  argi = 0;
  k = LZ770;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_char), (void *) &marker);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

  argi = 0;
  k = HUFF0;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

#if GZIP_ENGINES > 1
  argi = 0;
  k = LZ771;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_char), (void *) &marker);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

  argi = 0;
  k = HUFF1;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
#endif

#if GZIP_ENGINES > 2
  argi = 0;
  k = LZ772;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_char), (void *) &marker);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

  argi = 0;
  k = HUFF2;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
#endif

#if GZIP_ENGINES > 3
  argi = 0;
  k = LZ773;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_char), (void *) &marker);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

  argi = 0;
  k = HUFF3;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
#endif

  argi = 0;
  k = STORE_HUFF;

  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_mem), &out_info_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);

  // AES-256-encryption
  argi = 0;
  k = AES_ENCRYPT;

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_mem), &output_aes_enc_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_int8), &aes_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  argi = 0;
  k = AES_ENCRYPT_KEY;

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_int8), &key_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  //-------------------------------------------------------------------------------------
  // [openCL] Launch kernels
  //-------------------------------------------------------------------------------------
  cl_event kernel_event[NUM_GZIP_KERNELS + 2];
  cl_event finish_event[3];

  const size_t global_work_size = 1;
  const size_t local_work_size  = 1;

  // aes_key
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel[AES_ENCRYPT_KEY],
      1, NULL, &global_work_size, &local_work_size, 0, NULL, NULL);
  checkError(status, "Failed to launch AES KEY Encryption kernel");

  // gzip
  for (int k = 0; k < NUM_GZIP_KERNELS; ++k) {
    status = clEnqueueNDRangeKernel(gzip_queue[k], gzip_kernel[k], 1, NULL,
        &global_work_size, &local_work_size, 2, write_event, &kernel_event[k]);
    checkError(status, "Failed to launch GZIP kernel %s", gzip_kernel_name[k]);
  }

  // aes encryption
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel[AES_ENCRYPT], 1, NULL,
      &global_work_size, &local_work_size, 2, write_event, &kernel_event[NUM_GZIP_KERNELS]);
  checkError(status, "Failed to launch AES kernel %s", aes_kernel_name[AES_ENCRYPT]);


  //-------------------------------------------------------------------------------------
  // [openCL] Dequeue read buffers from kernel and verify results from them
  //-------------------------------------------------------------------------------------
  status = clEnqueueReadBuffer(gzip_queue[STORE_HUFF], out_info_buf, CL_FALSE, 0,
      sizeof(gzip_out_info_t), &out_info, 1, &kernel_event[STORE_HUFF], &finish_event[0]);
  checkError(status, "Failed to read back information data");

  unsigned int *output_aes_encr = (unsigned int *)alignedMalloc(outsize);
  status = clEnqueueReadBuffer(aes_queue, output_aes_enc_buf, CL_FALSE, 0,
           outsize, output_aes_encr, 1, &kernel_event[NUM_GZIP_KERNELS], &finish_event[1]);
  checkError(status, "Failed to read back encrypted data");

  for (int k = 0; k < NUM_GZIP_KERNELS; ++k) {
    clWaitForEvents(1, &kernel_event[k]);
    std::cout << "Finished: " << gzip_kernel_name[k] << std::endl;
  }
  clWaitForEvents(1, &kernel_event[NUM_GZIP_KERNELS]);
  std::cout << "Finished: " << aes_kernel_name[AES_ENCRYPT] << std::endl;

  // wait for both read events to complete
  clWaitForEvents(2, finish_event);
  std::cout << "Completed: out_info_buf and out_aes" << std::endl;

  //-------------------------------------------------------------------------------------
  // [DEBUG] Save compressed and encrypted data to a file 
  //-------------------------------------------------------------------------------------
#if !GZIP_STORE
  std::fstream file;
  file.open("file_encrypted_results.txt", std::ios::out);

  unsigned char *ptr_compressedNencrypted = (unsigned char*) output_aes_encr;
  for(unsigned int i=0; i<(2*compsize_huffman); i++){
     file << *ptr_compressedNencrypted;
     ptr_compressedNencrypted++;
  }

  file.close();
#endif

  //-------------------------------------------------------------------------------------
  // [AES] Decryption
  //-------------------------------------------------------------------------------------

  argi = 0;
  k = AES_DECRYPT;

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_mem), &output_aes_enc_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_mem), &output_aes_dec_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_int8), &aes_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  unsigned int decrypt_lines = 0;
  if (out_info.compsize_huffman[0]%64==0)
    decrypt_lines = out_info.compsize_huffman[0]/64;
  else
    decrypt_lines = ((out_info.compsize_huffman[0]/64+1)*64)/64;

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_int), (void *) &decrypt_lines);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  argi = 0;
  k = AES_DECRYPT_KEY;

  status = clSetKernelArg(aes_kernel[k], argi++, sizeof(cl_int8), &key_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, aes_kernel[k]);

  // Launch aes_key kernel
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel[AES_DECRYPT_KEY], 1, NULL,
      &global_work_size, &local_work_size, 0, NULL, NULL);
  checkError(status, "Failed to launch kernel %s", aes_kernel[AES_DECRYPT_KEY]);

  // Launch aes decryption kernel
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel[AES_DECRYPT], 1, NULL,
      &global_work_size, &local_work_size, 0, NULL, &kernel_event[NUM_GZIP_KERNELS + 1]);
  checkError(status, "Failed to launch kernel %s", aes_kernel[AES_DECRYPT]);

  // Read result
  status = clEnqueueReadBuffer(aes_queue, output_aes_dec_buf, CL_FALSE, 0,
      outsize, output_aes, 1, &kernel_event[NUM_GZIP_KERNELS + 1], &finish_event[2]);
  checkError(status, "Failed to read back decrypted data");

  // Wait for decryption write event
  clWaitForEvents(1, &finish_event[2]);

  //-------------------------------------------------------------------------------------
  // [openCL] Time profiles
  //-------------------------------------------------------------------------------------
  time_profiles_s profiles;
  profiles.gzip_com         = getStartEndTime(&kernel_event[LZ770], 2);
  profiles.aes_enc          = getStartEndTime(&kernel_event[NUM_GZIP_KERNELS], 1);
  profiles.aes_dec          = getStartEndTime(&kernel_event[NUM_GZIP_KERNELS + 1], 1);
  profiles.core_only        = getStartEndTime(&kernel_event[LZ770], 4);
  profiles.compNcrypt       = getStartEndTime(&kernel_event[LOAD_LZ], 6);

  // Release all events
  for (unsigned k = 0; k < NUM_GZIP_KERNELS + 2; ++k)
    clReleaseEvent(kernel_event[k]);

  clReleaseEvent(write_event[0]);
  clReleaseEvent(write_event[1]);
  clReleaseEvent(finish_event[0]);
  clReleaseEvent(finish_event[1]);

  // Return results
  fvp              = out_info.fvp[0];
  compsize_lz      = out_info.compsize_lz[0];
  compsize_huffman = out_info.compsize_huffman[0];

  std::cout << "FVP          : " << fvp << std::endl;
  std::cout << "COM size lz  : " << compsize_lz << std::endl;
  std::cout << "COM size huff: " << compsize_huffman << std::endl;

  return profiles;
}

//---------------------------------------------------------------------------------------
//  Decompress on HOST TODO move to gzip_tools
//---------------------------
//
//---------------------------------------------------------------------------------------

int decompress_on_host(unsigned char *input, huff_encodenode_t *tree, unsigned int insize,
    unsigned int outsize, unsigned char marker, unsigned int fvp,
    unsigned short *output_huffman, unsigned int compsize_lz, unsigned int compsize_huffman,
    unsigned int remaining_bytes, double &time_decompress)
{
  unsigned char *output_huffman_char = (unsigned char *)alignedMalloc(outsize);
  unsigned char *decompress_lz       = (unsigned char *)alignedMalloc(outsize);
  unsigned char *decompress_huff     = (unsigned char *)alignedMalloc(outsize);
  unsigned char *decompress_huff_lz  = (unsigned char *)alignedMalloc(outsize);

  double lib_start = aocl_utils::getCurrentTimestamp();
  //convert output_huffman from shorts to chars
  for (int i = 0; i < compsize_huffman; i++)
  {
    //transform from shorts to chars
    short comp_short = output_huffman[i/2];
    if (i%2 == 0)
      comp_short >>= 8;
    unsigned char comp_char = comp_short;
    output_huffman_char[i] = comp_char;
  }

  //---------------------------------------
  // DECOMPRESS HUFFMAN
  //---------------------------------------
  Huffman_Uncompress(output_huffman_char, decompress_huff, tree, compsize_huffman,
      compsize_lz, marker);

  //append last VEC bytes starting from fvp
  for (int i = fvp; i < VEC; i++)
  {
    decompress_huff[compsize_lz++] = input[insize-VEC+i];
    if (input[insize-VEC+i] == marker)
      decompress_huff[compsize_lz++] = 0;
  }

  //append to output_huff the ommitted bytes
  for (int i = 0; i < remaining_bytes; i++)
  {
    decompress_huff[compsize_lz++] = input[insize++];
    if (input[insize-1] == marker)
      decompress_huff[compsize_lz++] = 0;
  }

  //---------------------------------------
  // DECOMPRESS LZ
  //---------------------------------------

  // Compare input / output_huffman data
  int err_count = 0;

  err_count += LZ_Uncompress(decompress_huff, decompress_huff_lz, compsize_lz);

  //---------------------------------------
  double lib_stop = aocl_utils::getCurrentTimestamp();
  time_decompress = lib_stop-lib_start;

  for (int k = 0; k < insize; k++)
  {
    if (input[k] != decompress_huff_lz[k])
    {
      err_count++;
      if (err_count < 10)
      {
        printf( "%d: %d '%c'!= %d '%c' errr\n", k, decompress_huff_lz[k],
            decompress_huff_lz[k], input[k], input[k]);
      }
    }
  }

  alignedFree(decompress_lz);
  alignedFree(decompress_huff);
  alignedFree(decompress_huff_lz);
  alignedFree(output_huffman_char);

  return err_count;
}
