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
#include <math.h>
#include <stack>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>

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

// OpenCL runtime configuration
static cl_platform_id platform    = NULL;
static cl_device_id device        = NULL;
static cl_context context         = NULL;
static cl_program program         = NULL;
static cl_int status              = 0;


static cl_command_queue aes_queue  = NULL;
static cl_kernel aes_kernel_en     = NULL;
static cl_kernel aes_kernel_key_en = NULL;
static cl_kernel aes_kernel_de     = NULL;
static cl_kernel aes_kernel_key_de = NULL;

//---------------------------------------------------------------------------------------
//  DATA BUFFERS on FPGA
//---------------------------------------------------------------------------------------
static cl_mem input_buf          = NULL;
static cl_mem output_aes_en_buf  = NULL;
static cl_mem output_aes_de_buf  = NULL;

//---------------------------------------------------------------------------------------
//  FUNCTION PROTOTYPES
//---------------------------------------------------------------------------------------

bool init(bool use_emulator);
void cleanup();

void encrypt_and_decrypt(const char *filename, unsigned int augment, unsigned int repet);
void offload_to_FPGA(unsigned int *input, unsigned int insize, unsigned int *output_aes, cl_ulong &time_ns_aes_encrypt, cl_ulong &time_ns_aes_decrypt, unsigned int cache_lines);

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
  
  if (argc > 3) {
    std::cout<<"File name  : "<< argv[1]<<std::endl;
    std::cout<<"Augument   : "<< argv[2]<<std::endl;
    std::cout<<"Repetitions: "<< argv[3]<<std::endl;
    encrypt_and_decrypt(argv[1], atoi(argv[2]), atoi(argv[3]));
  }
  else{
    std::cerr << argv[0] << " <file> <augument> <repetitions>" << std::endl; 
    encrypt_and_decrypt("file0.txt",1,1);
  }

  cleanup();
  return 0;
}

//---------------------------------------------------------------------------------------
//  ENCRYPT and DECRYPT
//---------------------------
//  1- Do encryption on FPGA
//  2- Do decryption on FPGA
//  3- Verify the results is the same after decryption
//  4- Print result and cleanup
//---------------------------------------------------------------------------------------

void encrypt_and_decrypt(const char *filename, unsigned int augment, unsigned int repet)
{
  //-------------------------------------------------------------------------------------
  // 0- Input file
  //-------------------------------------------------------------------------------------

  //Kernel Variables
  unsigned int insize, rounded_size;
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

  if(insize%64==0)
    rounded_size = insize;
  else
    rounded_size = ((unsigned int)(insize/64)+1)*64;
  
  std::cout<<"Rounded size [B]: "<<rounded_size<<std::endl;
  unsigned int cache_lines = 64;

  // Host Buffers
  unsigned char  *input_file     = (unsigned char *)alignedMalloc(rounded_size*augment);
  unsigned int   *output_aes_en  = (unsigned int *)alignedMalloc(rounded_size*augment);
  unsigned int   *output_aes_de  = (unsigned int *)alignedMalloc(rounded_size*augment);

  // Read and close input file 
  if (fseek(f, 0, SEEK_SET) != 0)
       exit(1);
  if (fread(input_file, 1, insize, f) != insize)
        exit(1);
  fclose(f);
  
  for(unsigned i=1; i<augment; i++)
    memcpy((input_file+i*rounded_size), input_file, (size_t)rounded_size); 

  //-------------------------------------------------------------------------------------
  // 1-2- Encrypt and Decrypt
  //-------------------------------------------------------------------------------------
  unsigned int *input = (unsigned int *)input_file;
  memset(output_aes_en, 0, rounded_size);
  memset(output_aes_de, 0, rounded_size);
  
  
  std::vector<float> th_encrypt;
  std::vector<float> th_decrypt;

  cl_ulong time_ns_profiler_aes_encrypt = 0;
  cl_ulong time_ns_profiler_aes_decrypt = 0;
    
  //memset(input,0, rounded_size);
  for(unsigned int r=0; r<repet; r++){
  	offload_to_FPGA(input, rounded_size*augment, output_aes_de, time_ns_profiler_aes_encrypt, time_ns_profiler_aes_decrypt, cache_lines);
  	th_encrypt.push_back((double)rounded_size*augment/(double)time_ns_profiler_aes_encrypt);
 	th_decrypt.push_back((double)rounded_size*augment/(double)time_ns_profiler_aes_decrypt);     
  }
   
   
  //-------------------------------------------------------------------------------------
  // 3- Verify results
  //-----------------------------------------------------------------
  int numerrors = 0;
  const void *s1 = (const void *)input;
  const void *s2 = (const void *)output_aes_de;
  
  numerrors = memcmp(input, output_aes_de, (size_t)rounded_size*augment);
  /*
     for(int i=0; i<BUFSIZE; i+=LINE){
       printf("plaintxt : 0x");
     for(int k=LINE-1; k>=0; k--)
      printf("%08x",input[i+k]);
     printf("\ndecrypted: 0x");  
     for(int k=LINE-1; k>=0; k--)
       printf("%08x",output_aes_de[i+k]);
     printf("\n"); 
    }    
  */
  //---------------------------------------------------------------------------
  // 4- Clean up
  //---------------------------------------------------------------------------

  if (numerrors == 0)
    std::cout << "PASSED, no errors" << std::endl;
  else
    std::cerr << "FAILED, " << numerrors << " errors" << std::endl;

  //double throughput_aes_enc = (double)rounded_size*augment / double(time_ns_profiler_aes_encrypt);
  //double throughput_aes_dec = (double)rounded_size*augment / double(time_ns_profiler_aes_decrypt);

  //printf("Throughput encrypt    = %.5f GB/s \n", throughput_aes_enc);
  //printf("Throughput decryption = %.5f GB/s \n", throughput_aes_dec);
  
  std::sort(th_encrypt.begin(),th_encrypt.end());
  std::sort(th_decrypt.begin(),th_decrypt.end());
  
  float min_encrypt = th_encrypt[0];
  float min_decrypt = th_decrypt[0];
  float max_encrypt = th_encrypt[repet-1];
  float max_decrypt = th_decrypt[repet-1];

  float p25_encrypt = 0.0;
  float p50_encrypt = 0.0;
  float p75_encrypt = 0.0;
  
  float p25_decrypt = 0.0;
  float p50_decrypt = 0.0;
  float p75_decrypt = 0.0;

  if(repet>=4){
      p25_encrypt = th_encrypt[(repet/4)-1];
      p50_encrypt = th_encrypt[(repet/2)-1];
      p75_encrypt = th_encrypt[(repet*3)/4-1];

      p25_decrypt = th_decrypt[(repet/4)-1];
      p50_decrypt = th_decrypt[(repet/2)-1];
      p75_decrypt = th_decrypt[(repet*3)/4-1];
  }

  float p1_encrypt  = 0.0;
  float p5_encrypt  = 0.0;
  float p95_encrypt = 0.0;
  float p99_encrypt = 0.0;
  
  float p1_decrypt  = 0.0;
  float p5_decrypt  = 0.0;
  float p95_decrypt = 0.0;
  float p99_decrypt = 0.0;

  if (repet >= 100) {
     p1_encrypt  = th_encrypt[((repet)/100)-1];
     p5_encrypt  = th_encrypt[((repet*5)/100)-1];
     p95_encrypt = th_encrypt[((repet*95)/100)-1];
     p99_encrypt = th_encrypt[((repet*99)/100)-1];
  
     p1_decrypt  = th_decrypt[((repet)/100)-1];
     p5_decrypt  = th_decrypt[((repet*5)/100)-1];
     p95_decrypt = th_decrypt[((repet*95)/100)-1];
     p99_decrypt = th_decrypt[((repet*99)/100)-1];

  }
  
  th_encrypt.clear();
  th_decrypt.clear();

  FILE *file;
  //Open input file
  file = fopen("aes_throughput_encrypt_results.txt", "a");

  //input files size
  unsigned int file_size = get_filesize(file);
  if (file_size == 0)
     fprintf(file, "file_size repet min max p1 p5 p25 p50 p75 p95 p99 \n");
 
  fseek(file, file_size, SEEK_END); 
  unsigned int total_size = rounded_size*augment;
  fprintf(file,"%u %u %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",total_size, repet, min_encrypt, max_encrypt, p1_encrypt, p5_encrypt, p25_encrypt, p50_encrypt, p75_encrypt, p95_encrypt, p99_encrypt);
   
  fclose(file);

  file = fopen("aes_throughput_decrypt_results.txt", "a");
  //input files size
  file_size = get_filesize(file);
  if (file_size == 0)
   	fprintf(file, "file_size repet min max p1 p5 p25 p50 p75 p95 p99 \n");
   
  fseek(file, file_size, SEEK_END);
  fprintf(file,"%u %u %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",total_size, repet, min_decrypt, max_decrypt, p1_decrypt, p5_decrypt, p25_decrypt, p50_decrypt, p75_decrypt, p95_decrypt, p99_decrypt);
   
  fclose(file);
    
  //free buffers
  //??free(nodes);
  alignedFree(input);
  alignedFree(output_aes_en);
  alignedFree(output_aes_de);
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
  aes_queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
  checkError(status, "Failed to create command queue AES");

  // Create the program.
  std::string path = xstr(BUILD_FOLDER);
  path += "/encrypt_decrypt";
  
  std::string binary_file = getBoardBinaryFile(path.c_str(), device);
  std::cout << "Programming FPGA with " << binary_file << std::endl;
  program = createProgramFromBinary(context, binary_file.c_str(), &device, 1);

  // Build the program that was just created.
  status = clBuildProgram(program, 0, NULL, "", NULL, NULL);
  checkError(status, "Failed to build program");

  aes_kernel_en = clCreateKernel(program, "aes_encrypt", &status);
  checkError(status, "Failed to create AES Encryption kernel");

  aes_kernel_key_en = clCreateKernel(program, "aes_keygen_encrypt", &status);
  checkError(status, "Failed to create AES KEY Encryption kernel");
  
  aes_kernel_de = clCreateKernel(program, "aes_decrypt", &status);
  checkError(status, "Failed to create AES Decryption kernel");

  aes_kernel_key_de = clCreateKernel(program, "aes_keygen_decrypt", &status);
  checkError(status, "Failed to create AES KEY Decryption kernel");
  return true;
}


//---------------------------------------------------------------------------------------
//  CLEANUP
//---------------------------
// Free the resources allocated during initialization
//---------------------------------------------------------------------------------------

void cleanup()
{ 
  if (aes_kernel_en)
    clReleaseKernel(aes_kernel_en);
  if (aes_kernel_key_en)
    clReleaseKernel(aes_kernel_key_en);
  if (aes_kernel_de)
    clReleaseKernel(aes_kernel_de);
  if (aes_kernel_key_de)
    clReleaseKernel(aes_kernel_key_de);
  if (program)
    clReleaseProgram(program);
  if (context)
    clReleaseContext(context);  

  //free in/out buffers
  if (input_buf)
    clReleaseMemObject(input_buf);
  if (output_aes_en_buf)
    clReleaseMemObject(output_aes_en_buf);
  if(output_aes_de_buf)
    clReleaseMemObject(output_aes_de_buf);

}

//---------------------------------------------------------------------------------------
//  Offload to FPGA
//---------------------------
// 
//---------------------------------------------------------------------------------------
void offload_to_FPGA(unsigned int *input, unsigned int insize, unsigned int *output_aes_de, cl_ulong &time_ns_aes_encrypt, cl_ulong &time_ns_aes_decrypt, unsigned int cache_lines)
{
  cl_int status;
  std::cout<<"offload insize:"<<insize<<std::endl; 
  // --- AES ---
  aes_config aes_config_run;
  
  aes_config_run.elements[0] = cache_lines;// N is number of lines, one line = 512b=64B 
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

  #ifdef DEBUG
    for (int i=7; i>=0; i--)
      printf("%d=%08x\n", i, key_config_run.key[i]);
  #endif
  // --- AES ---

  // Input buffers
  input_buf = clCreateBuffer(context, CL_MEM_READ_ONLY, insize, NULL, &status);
  checkError(status, "Failed to create buffer for input");

  output_aes_en_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, insize, NULL, &status);
  checkError(status, "Failed to create buffer for output_aes_encryption"); 

  output_aes_de_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, insize, NULL, &status);
  checkError(status, "Failed to create buffer for output_aes_decryption");

  // Transfer inputs. Each of the host buffers supplied to clEnqueueWriteBuffer here
  // should be "aligned" to ensure that DMA is used for the host-to-device transfer.
  cl_event write_event; 

  status = clEnqueueWriteBuffer(aes_queue, input_buf, CL_FALSE, 0,
      insize, input, 0, NULL, &write_event);
  checkError(status, "Failed to transfer raw input");

  //-------------------------------------------------------------------------------------
  // [openCL] Set kernel arguments and enqueue commands into kernel
  //-------------------------------------------------------------------------------------
  unsigned int line_size = insize/64;
  // AES-256-encryption
  unsigned int argi = 0;
   
  status = clSetKernelArg(aes_kernel_en, argi++, sizeof(cl_mem), &input_buf); 
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  status = clSetKernelArg(aes_kernel_en, argi++, sizeof(cl_mem), &output_aes_en_buf);
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  status = clSetKernelArg(aes_kernel_en, argi++, sizeof(cl_int8), &aes_config_run);
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  status = clSetKernelArg(aes_kernel_en, argi++, sizeof(cl_int), (void *) &line_size);
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  argi = 0;
  status = clSetKernelArg(aes_kernel_key_en, argi++, sizeof(cl_int8), &key_config_run);
  checkError(status, "Failed to set kernel_key arg %d", argi - 1);
  
  // AES-256-decryption
  argi = 0;

  status = clSetKernelArg(aes_kernel_de, argi++, sizeof(cl_mem), &output_aes_en_buf); 
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  status = clSetKernelArg(aes_kernel_de, argi++, sizeof(cl_mem), &output_aes_de_buf);
  checkError(status, "Failed to set kernel arg %d",argi-1);

  status = clSetKernelArg(aes_kernel_de, argi++, sizeof(cl_int8), &aes_config_run);
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  status = clSetKernelArg(aes_kernel_de, argi++, sizeof(cl_int), (void *) &line_size);
  checkError(status, "Failed to set kernel arg %d", argi - 1);

  argi = 0;
  status = clSetKernelArg(aes_kernel_key_de, argi++, sizeof(cl_int8), &key_config_run);
  checkError(status, "Failed to set kernel_key arg %d", argi - 1);

  // Launch kernels
  cl_event key_event[2];
  cl_event kernel_event[2]; 
  cl_event finish_event;
  
  const size_t global_work_size = 1;
  const size_t local_work_size  = 1;
  
  // DEBUG
  unsigned int *output_aes_encr = (unsigned int *)alignedMalloc(insize);

  // Launch kernel_key
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel_key_en, 1, NULL, &global_work_size,
      &local_work_size, 1, &write_event, &key_event[0]); 
  checkError(status, "Failed to launch AES KEY Encryption kernel");
  if(!status)
    std::cout<<"Launched kernel: aes_kernel_key"<<std::endl;
  
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel_key_de, 1, NULL, &global_work_size,
      &local_work_size, 1, &write_event, &key_event[1]); 
  checkError(status, "Failed to launch AES KEY Decryption kernel");
  if(!status)
    std::cout<<"Launched kernel: aes_kernel_key_de"<<std::endl;
  
  // Launch aes kernels
  
  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel_en, 1, NULL, &global_work_size,
      &local_work_size, 2, key_event, &kernel_event[0]);
  checkError(status, "Failed to launch AES Encryption kernel");
  if(!status)
    std::cout<<"Launched kernel: aes_kernel encryption"<<std::endl;

  status = clEnqueueNDRangeKernel(aes_queue, aes_kernel_de, 1, NULL, &global_work_size,
      &local_work_size, 1, &kernel_event[0], &kernel_event[1]);
  checkError(status, "Failed to launch AES Decryption kernel");
  if(!status)
    std::cout<<"Launched kernel: aes_kernel decryption"<<std::endl;

  status = clFinish(aes_queue);
  checkError(status, "Failed to finish aes_queue decryption");

  //-------------------------------------------------------------------------------------
  // [openCL] Dequeue read buffers from kernel and verify results from them
  //-------------------------------------------------------------------------------------

  status = clEnqueueReadBuffer(aes_queue, output_aes_de_buf, CL_FALSE, 0,
           insize, output_aes_de, 1, &kernel_event[1], &finish_event);
  checkError(status, "Failed to read back decrypted data");

  //DEBUG
  //status = clEnqueueReadBuffer(aes_queue, output_aes_en_buf, CL_FALSE, 0,
  //         insize, output_aes_encr, 0, NULL, NULL);
  // checkError(status, "Failed to read back decrypted data");
  
  // Release local events.
  clReleaseEvent(write_event);

  // Wait for all devices to finish.
  clWaitForEvents(1, &finish_event);

  // Get kernel times using the OpenCL event profiling API from the Huffman kernel.
  time_ns_aes_encrypt = getStartEndTime(&kernel_event[0],1);
  time_ns_aes_decrypt = getStartEndTime(&kernel_event[1],1);

  // Release all events
  for (unsigned k = 0; k < 2; k++){  
    clReleaseEvent(kernel_event[k]);
    clReleaseEvent(key_event[k]);
  }

  clReleaseEvent(finish_event); 

}


