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
#include <stack>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "CL/opencl.h"
#include "AOCLUtils/aocl_utils.h"

#include "gzip_tools.h"
#include "helpers.h"

#define TIMES 64
#define STRING_BUFFER_LEN 1024

#define xstr(s) str(s)
#define str(s) #s

using namespace std;
using namespace aocl_utils;

//#define DEBUG

//---------------------------------------------------------------------------------------
// ACL runtime configuration
//---------------------------------------------------------------------------------------

// All kernels end in 0.  There may be 1,2,3 versions as well, depending on
// value of ENGINES, but for now, not exercising them from the host.
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
  NUM_KERNELS
};

// OpenCL runtime configuration
static cl_platform_id platform    = NULL;
static cl_device_id device        = NULL;
static cl_context context         = NULL;
static cl_program program         = NULL;
static cl_int status              = 0;

static cl_command_queue gzip_queue[GZIP_KERNELS];
static cl_kernel gzip_kernel[GZIP_KERNELS];

//---------------------------------------------------------------------------------------
//  DATA BUFFERS on FPGA
//---------------------------------------------------------------------------------------
// GZIP
static cl_mem input_buf          = NULL;
static cl_mem huftable_buf       = NULL;
static cl_mem out_info_buf       = NULL;
static cl_mem output_huffman_buf = NULL;  // TODO: delete this buffer

//---------------------------------------------------------------------------------------
//  FUNCTION PROTOTYPES
//---------------------------------------------------------------------------------------

bool init(bool use_emulator);
void cleanup();

void compress_and_encrypt(const char *filename, unsigned int);

cl_ulong offload_to_FPGA(unsigned char *input, unsigned int *huftable,
    unsigned int insize, unsigned int outsize, unsigned char marker, unsigned int &fvp,
    unsigned short *output_huffman, unsigned int &compsize_lz, unsigned int &compsize_huffman);

int compute_on_host(unsigned char *input, huff_encodenode_t *tree, unsigned int insize,
    unsigned int outsize, unsigned char marker, unsigned int fvp, unsigned char *output_lz,
    unsigned short *output_huffman, unsigned int compsize_lz, unsigned int compsize_huffman,
    unsigned int remaining_bytes);

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
    std::cout<<"File name: "<< argv[1]<< std::endl;
    unsigned int original = atoi(argv[2]);
    compress_and_encrypt(argv[1], original);
  }
  else
    compress_and_encrypt("./file0.txt",0);

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

void compress_and_encrypt(const char *filename, unsigned int original)
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

  // Worst case output_lz buffer size (too pessimistic for now)
  outsize = insize*2;

  // Host Buffers
  unsigned char  *input          = (unsigned char *)alignedMalloc(insize);
  unsigned char  *output_lz      = (unsigned char *)alignedMalloc(outsize);
  unsigned short *output_huffman = (unsigned short *)alignedMalloc(outsize);

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
  unsigned int *huftable   = (unsigned int *)alignedMalloc(1024);
  huff_encodenode_t *tree  = (huff_encodenode_t *)malloc(MAX_TREE_NODES * sizeof(huff_encodenode_t));
  huff_encodenode_t **root = &tree;
  unsigned char marker     = Compute_Huffman(input, insize, huftable, root);

  //Print_Huffman(tree);
  
  //-------------------------------------------------------------------------------------
  // 2- Send input file on *FPGA* for Compresion and Encryption
  //-------------------------------------------------------------------------------------

  unsigned int compsize_lz = 0;
  unsigned int compsize_huffman = 0;
  unsigned int fvp = 0;
   
  memset(output_huffman, 0, outsize);
  
  cl_ulong time_ns_profiler_gzip = offload_to_FPGA(input, huftable, insize, outsize, marker,
      fvp, output_huffman, compsize_lz, compsize_huffman);
   
  std::cout<<"Remaining bytes: "<<remaining_bytes<<std::endl;
  std::cout<<"Marker         : "<<static_cast<unsigned>(marker)<<std::endl;
  
//  std::fstream file;
//  file.open("file_compressed.txt", std::ios::out);
// 
//  unsigned char *ptr_huff = (unsigned char*) output_huffman;
//  for(unsigned int i=0; i<(2*compsize_huffman); i++){
//     file<<*ptr_huff;
//     ptr_huff++;
//  }
//
//  file.close();

  //std::cout<<"Huffman table"<<std::endl;
  //for(unsigned int i=0; i<256; i++)
  //  std::cout<<huftable[i]<<" ";
  //std::cout<<std::endl;

  //-------------------------------------------------------------------------------------
  // 3- Decrypt data on *host* and compare to compressed data for verification
  //-------------------------------------------------------------------------------------
  
  // TODO

  //-------------------------------------------------------------------------------------
  // 4- Decrypt and decompress FPGA output on host and compare to input for verification
  //-----------------------------------------------------------------
  int numerrors = 0;
  numerrors = compute_on_host(input, tree, insize, outsize, marker, fvp, output_lz,
              output_huffman, compsize_lz, compsize_huffman, remaining_bytes);

  //---------------------------------------------------------------------------
  // 5- Print result and cleanup
  //---------------------------------------------------------------------------

  if (numerrors == 0)
    std::cout << "PASSED, no errors" << std::endl;
  else
    std::cerr << "FAILED, " << numerrors << " errors" << std::endl;
  
  double throughput = (double)insize / double(time_ns_profiler_gzip);
  double thr_comp   = (double)compsize_huffman / double(time_ns_profiler_gzip); 
  float  compression_ratio = (float)(insize)/compsize_huffman;
  printf("Compression Ratio = %.3f \n", compression_ratio);

float multiplier = 1.0;
#ifdef GZIP_ENGINES
  // Every engine should be doing identical work, but credit them as if they
  // had all done useful work
  multiplier *= GZIP_ENGINES;
#endif
  printf("Throughput = %.5f GB/s \n", throughput * multiplier);

  //free buffers
  //??free(nodes);
  alignedFree(input);
  alignedFree(output_lz);
  alignedFree(output_huffman);

  if(numerrors==0){

    FILE *file_results;
    //Open input file
    char cval[20]; 
    sprintf(cval,"%u",original);
    std::string name = "fpga_deflate_";
    name.append((const char *)cval);
    name.append("_");
    sprintf(cval,"%u",insize);
    name.append((const char *)cval);
    name.append(".dat");
  
    file_results = fopen(name.c_str(), "a");
  
    //input files size
    unsigned int file_results_size = get_filesize(file_results);
    if (file_results_size == 0)
      fprintf(file_results, "file_size ratio throughput thr_comp\n");
   
    fseek(file_results, file_results_size, SEEK_END); 
    fprintf(file_results,"%u %.5f %.5f %.5f\n", insize, compression_ratio, throughput, thr_comp);
    fclose(file_results);
  }
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
  for (int k = 0; k < GZIP_KERNELS; ++k) {
    gzip_queue[k] = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
    checkError(status, "Failed to create command queue GZIP");
  }

  // Create the program.
  std::string path = xstr(BUILD_FOLDER);
  path += "/compNcrypt";
  
  std::string binary_file = getBoardBinaryFile(path.c_str(), device);
  std::cout << "Programming FPGA with " << binary_file << std::endl;
  program = createProgramFromBinary(context, binary_file.c_str(), &device, 1);

  // Build the program that was just created.
  status = clBuildProgram(program, 0, NULL, "", NULL, NULL);
  checkError(status, "Failed to build program");

  for (int k = 0; k < GZIP_KERNELS; ++k) {
    // Create the kernel - name passed in here must match kernel name in the
    // original CL file, that was compiled into an AOCX file using the AOC tool
    const char *kernel_name1 = gzip_kernel_name[k];
    gzip_kernel[k] = clCreateKernel(program, kernel_name1, &status);
    checkError(status, "Failed to create GZIP kernel %s", kernel_name1);
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
  for (int k = 0; k < GZIP_KERNELS; ++k) {
    if (gzip_kernel[k])
      clReleaseKernel(gzip_kernel[k]);
    if (gzip_queue[k])
      clReleaseCommandQueue(gzip_queue[k]);
  }

  if (program)
    clReleaseProgram(program);
  if (context)
    clReleaseContext(context);	

  //free in/out buffers
  if (input_buf)
    clReleaseMemObject(input_buf);
  if (huftable_buf)
    clReleaseMemObject(huftable_buf);
  if (output_huffman_buf)
    clReleaseMemObject(output_huffman_buf);
  if (out_info_buf)
    clReleaseMemObject(out_info_buf);
}


//---------------------------------------------------------------------------------------
//  Offload to FPGA
//---------------------------
// 
//---------------------------------------------------------------------------------------

cl_ulong offload_to_FPGA(unsigned char *input, unsigned int *huftable,
    unsigned int insize, unsigned int outsize, unsigned char marker, unsigned int &fvp,
    unsigned short *output_huffman, unsigned int &compsize_lz, unsigned int &compsize_huffman)
{
  cl_int status;
  gzip_out_info_t out_info;
  
  // Input buffers
  input_buf = clCreateBuffer(context, CL_MEM_READ_ONLY, insize, NULL, &status);
  checkError(status, "Failed to create buffer for input");

  huftable_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, 1024, NULL, NULL);
  checkError(status, "Failed to create buffer for huftable");

  out_info_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(gzip_out_info_t), NULL, &status);
  checkError(status, "Failed to create buffer for out_info");  

  // Output buffers
  output_huffman_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, outsize, NULL, &status);
  checkError(status, "Failed to create buffer for output_huffman"); 
  

  // Transfer inputs. Each of the host buffers supplied to clEnqueueWriteBuffer here
  // should be "aligned" to ensure that DMA is used for the host-to-device transfer.
  cl_event write_event[2];
  status = clEnqueueWriteBuffer(gzip_queue[LOAD_LZ], input_buf, CL_FALSE, 0,
      insize, input, 0, NULL, &write_event[0]); // TODO: sizeof(char)
  checkError(status, "Failed to transfer raw input");

  status = clEnqueueWriteBuffer(gzip_queue[LOAD_HUFF_COEFF], huftable_buf, CL_TRUE, 0, 1024,
      huftable, 0, NULL, &write_event[1]);
  checkError(status, "Failed to transfer huftable");

  //-------------------------------------------------------------------------------------
  // [openCL] Set kernel arguments and enqueue commands into kernel
  //-------------------------------------------------------------------------------------

  unsigned argi, k;

  // LZ input loads
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
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_mem), &output_huffman_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  status = clSetKernelArg(gzip_kernel[k], argi++, sizeof(cl_mem), &out_info_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, gzip_kernel_name[k]);
  
  // Launch kernels
  cl_event kernel_event[6]; 
  cl_event finish_event[2];
  
  const size_t global_work_size = 1;
  const size_t local_work_size  = 1;

  // Launch gzip kernels
  for (int k = 0; k < GZIP_KERNELS; ++k) { 
    // Use a global work size corresponding to the number of elements to process on
    // for the "task" it is just equal to 1
    //
    // Events are used to ensure that the kernel is not launched until
    // the writes to the input buffers have completed.
    
    status = clEnqueueNDRangeKernel(gzip_queue[k], gzip_kernel[k], 1, NULL,
        &global_work_size, &local_work_size, 2, write_event, &kernel_event[k]);
    // don't forget to adjust the number of events when enqueuing more read buffers
    checkError(status, "Failed to launch GZIP kernel");

    if(!status)
      std::cout<<"Launched kernel: "<< gzip_kernel_name[k]<<std::endl;
  }

  //-------------------------------------------------------------------------------------
  // [openCL] Dequeue read buffers from kernel and verify results from them
  //-------------------------------------------------------------------------------------
  
  status = clEnqueueReadBuffer(gzip_queue[STORE_HUFF], out_info_buf, CL_FALSE, 0,
           sizeof(gzip_out_info_t), &out_info, 1, &kernel_event[STORE_HUFF], &finish_event[0]);
  checkError(status, "Failed to read back information data");

  status = clEnqueueReadBuffer(gzip_queue[STORE_HUFF], output_huffman_buf, CL_FALSE, 0,
           outsize, output_huffman, 1, &kernel_event[STORE_HUFF], &finish_event[1]);
  checkError(status, "Failed to read back encrypted data");
  
//  for(int i=0; i<out_info.compsize_huffman; i++)
//     std::cout<<output_huffman[i]<<" ";
//
//  std::cout<<std::endl;

  // Release local events.
  clReleaseEvent(write_event[0]);
  clReleaseEvent(write_event[1]);

  // Wait for all devices to finish.
  clWaitForEvents(2, finish_event);

  // Get kernel times using the OpenCL event profiling API from the Huffman kernel.
  cl_ulong time_ns_profiler = getStartEndTime(kernel_event[HUFF0]);

  // Release all events
  for (unsigned k = 0; k < GZIP_KERNELS; ++k)
    clReleaseEvent(kernel_event[k]);

  clReleaseEvent(finish_event[0]); 
  clReleaseEvent(finish_event[1]);

  // Return results
  fvp = out_info.fvp;
  compsize_lz = out_info.compsize_lz;
  compsize_huffman = out_info.compsize_huffman;

  std::cout<<"FVP          : "<<fvp<<std::endl;
  std::cout<<"COM size lz  : "<<compsize_lz<<std::endl;
  std::cout<<"COM size huff: "<<compsize_huffman<<std::endl;

  return time_ns_profiler;
}

//---------------------------------------------------------------------------------------
//  Compute on HOST TODO move to gzip_tools
//---------------------------
//
//---------------------------------------------------------------------------------------

int compute_on_host(unsigned char *input, huff_encodenode_t *tree, unsigned int insize,
    unsigned int outsize, unsigned char marker, unsigned int fvp,
    unsigned char *output_lz, unsigned short *output_huffman, unsigned int compsize_lz, 
    unsigned int compsize_huffman, unsigned int remaining_bytes)
{	
  unsigned char *output_huffman_char = (unsigned char *)alignedMalloc(outsize);
  unsigned char *decompress_lz       = (unsigned char *)alignedMalloc(outsize);
  unsigned char *decompress_huff     = (unsigned char *)alignedMalloc(outsize);
  unsigned char *decompress_huff_lz  = (unsigned char *)alignedMalloc(outsize);
  
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

  //append to output_lz the ommitted bytes
  for (int i = 0; i < remaining_bytes; i++)
  {
    decompress_huff[compsize_lz++] = input[insize++];
    if (input[insize-1] == marker)
      decompress_huff[compsize_lz++] = 0;
  }

  //---------------------------------------
  // DECOMPRESS LZ
  //---------------------------------------

  // Compare input / output_lz data 
  int err_count = 0;

  err_count += LZ_Uncompress(decompress_huff, decompress_huff_lz, compsize_lz);

  //---------------------------------------

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
