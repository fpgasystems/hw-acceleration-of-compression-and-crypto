//--------------------------------------------------------------------------------------------------
//  INCLUDES AND OPENCL VARIABLES
//--------------------------------------------------------------------------------------------------

#include <fstream>
#include <iostream>
#include <math.h>
#include <string>
#include <vector>

#include "CL/opencl.h"
#include "AOCLUtils/aocl_utils.h"

#include "gzip_tools.h"
#include "helpers.h"

static unsigned int HUFFTABLE_SIZE = 1024;

#define xstr(s) str(s)
#define str(s) #s

union header_u {
  unsigned int values[16];
};

//--------------------------------------------------------------------------------------------------
// ACL runtime configuration
//--------------------------------------------------------------------------------------------------

const char *kernel_name[] = {
  // GZIP
  "load_lz0",
  "load_huff_coeff0",
  "lz77_0",
#if GZIP_ENGINES > 1
  "lz77_1",
#endif
#if GZIP_ENGINES > 2
  "lz77_2",
#endif
#if GZIP_ENGINES > 3
  "lz77_3",
#endif
  // AES
  "aes_loadbalancer",
  "aes_decrypt0",
  "aes_keygen_dec0"
};

enum Kernels {
  // GZIP
  GZIP_LOAD_LZ77 = 0,
  GZIP_LOAD_HUFF,
  GZIP_LZ770,
#if GZIP_ENGINES > 1
  GZIP_LZ771,
#endif
#if GZIP_ENGINES > 2
  GZIP_LZ772,
#endif
#if GZIP_ENGINES > 3
  GZIP_LZ773,
#endif
  // AES
  AES_LOAD_BALANCER,
  AES_DECRYPT,
  AES_DECRYPT_KEY,
  NUM_KERNELS
};


// OpenCL runtime configuration
static cl_platform_id platform    = NULL;
static cl_device_id device        = NULL;
static cl_context context         = NULL;
static cl_program program         = NULL;
static cl_int status              = 0;

static cl_command_queue queue[NUM_KERNELS];
static cl_kernel kernel[NUM_KERNELS];

//--------------------------------------------------------------------------------------------------
//  DATA BUFFERS on FPGA
//--------------------------------------------------------------------------------------------------
// GZIP
static cl_mem input_buf          = NULL;
static cl_mem huftable_buf       = NULL;
// AES
static cl_mem output_aes_enc_buf = NULL;
static cl_mem output_aes_dec_buf = NULL;

//--------------------------------------------------------------------------------------------------
//  FUNCTION PROTOTYPES
//--------------------------------------------------------------------------------------------------

bool init(bool use_emulator);
void cleanup();

void compress_and_encrypt(const char *filename, unsigned int n_pages);

time_profiles_s offload_to_FPGA(unsigned char *input, unsigned int *huftable,
  unsigned int insize, unsigned int n_pages, unsigned int outsize, unsigned char marker,
  unsigned int *output_aes, gzip_out_info_t &gzip_out_info);

int decompress_on_host(unsigned char *input, huff_encodenode_t *tree, unsigned int insize,
  unsigned int outsize, unsigned char marker, unsigned short *output_huffman,
  gzip_out_info_t gzip_out_info, unsigned int remaining_bytes, double &time_decompress);

//--------------------------------------------------------------------------------------------------
//  MAIN FUNCTION
//--------------------------------------------------------------------------------------------------

int main(int argc, char **argv)
{
  aocl_utils::Options options(argc, argv);

  std::string input_filename = options.has("input") ? options.get("input") : "input.txt";
  unsigned int n_pages = options.has("n_pages") ? std::stoul(options.get("n_pages")) : 1;

  // Optional argument to specify whether the emulator should be used.
  bool use_emulator = options.has("emulator");

  if (!init(use_emulator))
    return -1;

  if (n_pages == 0)
    return -1;

  compress_and_encrypt(input_filename.c_str(), n_pages);

  cleanup();
  return 0;
}

//--------------------------------------------------------------------------------------------------
// COMPUTE TIME PROFILES
//--------------------------------------------------------------------------------------------------
time_profiles_s computeTimeProfiles(std::vector<cl_event> &vec)
{
  std::vector<cl_event*> events;

  for (unsigned int e = 0; e < vec.size(); ++e) {
    if (e == GZIP_LZ770)
      continue;
    events.push_back(&(vec.at(e)));
  }

  time_profiles_s profiles;
  profiles.gzip_com   = getStartEndTime(events, 0, 2);
  profiles.aes_enc    = getStartEndTime(events, 2, 1);

  profiles.aes_dec    = getStartEndTime(events, 3, 1);
  profiles.compNcrypt = getStartEndTime(events, 0, 3);

  return profiles;
}

//--------------------------------------------------------------------------------------------------
//  COMPRESS AND ENCRYPT
//---------------------------
//  1- Create huffman tree and select marker on host
//  2- Deflate input file on *FPGA*
//  3- Inflate FPGA output on host and compare to input for verification
//  4- Print result and cleanup
//--------------------------------------------------------------------------------------------------

void compress_and_encrypt(const char *filename, unsigned int n_pages)
{
  //------------------------------------------------------------------------------------------------
  // 0- Input file
  //------------------------------------------------------------------------------------------------

  unsigned int insize, outsize;
  FILE *f;

  f = fopen(filename, "rb");
  if (f == NULL) {
    std::cerr << "Unable to open " << filename << std::endl;
    exit(1);
  }

  insize = get_filesize(f);
  if (insize < 1) {
    std::cerr << "File "<< filename << " is empty" << std::endl;
    fclose(f);
    exit(1);
  }

  // Worst case output_huff buffer size (too pessimistic for now)
  outsize = insize * 2;

  // Host Buffers
  unsigned char  *input          = (unsigned char *)aocl_utils::alignedMalloc(insize);
  unsigned int   *output_aes     = (unsigned int *)aocl_utils::alignedMalloc(outsize);

  // Read and close input file
  if (fseek(f, 0, SEEK_SET) != 0)
    exit(1);
  if (fread(input, 1, insize, f) != insize)
    exit(1);
  fclose(f);

  // here truncate the file so that the number of chars is a multiple of VEC
  int remaining_bytes = insize % (2*VEC); // TODO padding 0
  insize -= remaining_bytes;

  //------------------------------------------------------------------------------------------------
  // 1- Create Huffman Tree on host
  //------------------------------------------------------------------------------------------------
  unsigned int *huftable   = (unsigned int *)aocl_utils::alignedMalloc(HUFFTABLE_SIZE);
  huff_encodenode_t *tree  = (huff_encodenode_t *)malloc(MAX_TREE_NODES * sizeof(huff_encodenode_t));
  huff_encodenode_t **root = &tree;
  unsigned char marker     = Compute_Huffman(input, insize, huftable, root);

  //------------------------------------------------------------------------------------------------
  // 2- Send input file on *FPGA* for Compresion and Encryption
  //------------------------------------------------------------------------------------------------

  // Stats
  std::cout << "GZIP_ENGINES  : " << GZIP_ENGINES << std::endl;
  std::cout << "AES_ENGINES   : " << AES_ENGINES << std::endl;
  std::cout << "input size [B]: " << insize << std::endl;
  std::cout << "pages         : " << n_pages << std::endl;
  std::cout << "page size [B] : " << insize / n_pages << std::endl;
  std::cout << "Remaining [B] : " << remaining_bytes << std::endl;
  std::cout << "Marker        : " << static_cast<unsigned>(marker) << std::endl;

  // [BATCH] Split input into independent pages
  unsigned int page_size = insize / n_pages;
  if ((page_size % (2*VEC)) != 0) {
    std::cerr << "[ERROR] page_size must be divisible by 2*VEC" << std::endl;
    exit(1);
  }

  gzip_out_info_t gzip_out_info;
  memset(output_aes, 0, outsize);

  time_profiles_s profiles = offload_to_FPGA(input, huftable, insize, n_pages, outsize, marker,
    output_aes, gzip_out_info);

  //------------------------------------------------------------------------------------------------
  // 3- Decrypt data on *host* and compare to compressed data for verification
  //------------------------------------------------------------------------------------------------
  // TODO

  //------------------------------------------------------------------------------------------------
  // 4- Decrypt and decompress FPGA output on host and compare to input for verification
  //------------------------------------------------------------------------------------------------
  unsigned short* output_aes_short = (unsigned short*) &output_aes[16]; // skip first line, header
  union header_u header;
  memcpy(header.values, output_aes, 64); // 512 bits
  unsigned int compressed_size = gzip_out_info.compsize_huffman[0] * n_pages;

  gzip_out_info.fvp[0] = header.values[1];
  gzip_out_info.compsize_lz[0] = header.values[2];
  gzip_out_info.compsize_huffman[0] = header.values[3];

  int numerrors = 0;
  double time_x_profile_decompress = 0.0;

  // TODO fpga works, but here we should take all the headers, decrypt and decompress individually,
  //      and then compare the result with input 
  if (n_pages == 1) {
    numerrors = decompress_on_host(input, tree, insize, outsize, marker, output_aes_short,
      gzip_out_info, remaining_bytes, time_x_profile_decompress);
  }

  //------------------------------------------------------------------------------------------------
  // 5- Print result and cleanup
  //------------------------------------------------------------------------------------------------
  if (numerrors == 0)
    std::cout << "PASSED, no errors" << std::endl;
  else
    std::cerr << "FAILED, " << numerrors << " errors" << std::endl;

  // TODO add HUFFTABLE_SIZE to compsize
  double throughput_compNcrypt = (double)insize / double(profiles.compNcrypt);
  double throughput_gzip_com   = (double)insize / double(profiles.gzip_com);
  double throughput_aes_enc    = (double)compressed_size / double(profiles.aes_enc);
  double throughput_aes_dec    = (double)compressed_size / double(profiles.aes_dec);
  double throughput_gzip_dec   = (double)insize / (time_x_profile_decompress*1.0e+9) ;
  double compression_ratio     = (double)compressed_size/insize*100;

  std::cout << "Compressed size[0]:  " << header.values[0] << " lines" << std::endl;
  std::cout << "Compressed size[0]:  " << header.values[0] * 64 << " bytes" << std::endl;
  std::cout << "Compsize huffman[0]: " << header.values[3] << " bytes"<< std::endl;

  printf("Compsize huffman = %u \n", compressed_size);
  printf("Exec time compNcrypt        = %.2f ns \n", double(profiles.gzip_com));
  printf("Exec time aes_enc           = %.2f ns \n", double(profiles.aes_enc));
  printf("Exec time aes decryption    = %.2f ns \n", double(profiles.aes_dec));
  printf("Exec time gzip decompress   = %.2f ns \n", double(profiles.compNcrypt));

  printf("Compression Ratio = %.2f %% \n", compression_ratio);
  printf("Throughput compNcrypt       = %.5f GB/s \n", throughput_compNcrypt);
  printf("Throughput gzip compress    = %.5f GB/s \n", throughput_gzip_com);
  printf("Throughput aes encryption   = %.5f GB/s \n", throughput_aes_enc);
  printf("Throughput aes decryption   = %.5f GB/s \n", throughput_aes_dec);
  printf("Throughput gzip decompress  = %.5f GB/s \n", throughput_gzip_dec);

  //free buffers
  aocl_utils::alignedFree(input);
  aocl_utils::alignedFree(output_aes);
}

//--------------------------------------------------------------------------------------------------
//  INIT FUNCTION
//---------------------------
//  1- Find device
//  2- Create context
//  3- Create command queues
//  4- Create/build program
//  5- Create kernels
//--------------------------------------------------------------------------------------------------

bool init(bool use_emulator)
{
  // Get the OpenCL platform.
  if (use_emulator) {
    platform = aocl_utils::findPlatform("Intel(R) FPGA Emulation Platform for OpenCL(TM)");
  } else {
    platform = aocl_utils::findPlatform("Intel(R) FPGA SDK for OpenCL(TM)");
  }
  if(platform == NULL) {
    std::cerr << "[Error] Unable to find Intel FPGA OpenCL platform" << std::endl;
    return false;
  }

  // Print platform information
  #define STRING_BUFFER_LEN 1024
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
  aocl_utils::scoped_array<cl_device_id> devices;
  cl_uint num_devices;
  devices.reset(aocl_utils::getDevices(platform, CL_DEVICE_TYPE_ALL, &num_devices));
  device = devices[0]; // We'll just use the first device.

  // Create the context.
  context = clCreateContext(NULL, 1, &device, NULL, NULL, &status);
  checkError(status, "Failed to create context");

  // Create the program.
  std::string path = xstr(BUILD_FOLDER);
  path += "/compNcrypt";

  std::string binary_file = aocl_utils::getBoardBinaryFile(path.c_str(), device);
  std::cout << "Programming FPGA with " << binary_file << std::endl;
  program = aocl_utils::createProgramFromBinary(context, binary_file.c_str(), &device, 1);
  status = clBuildProgram(program, 0, NULL, "", NULL, NULL);
  checkError(status, "Failed to build program");

  // Create command queues and kernels
  for (int k = 0; k < NUM_KERNELS; ++k) {
    queue[k] = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
    checkError(status, "Failed to create command queue for %s", kernel_name[k]);

    kernel[k] = clCreateKernel(program, kernel_name[k], &status);
    checkError(status, "Failed to create kernel %s", kernel_name[k]);
  }

  return true;
}

//--------------------------------------------------------------------------------------------------
//  CLEANUP
//---------------------------
// Free the resources allocated during initialization
//--------------------------------------------------------------------------------------------------

void cleanup()
{
  for (int k = 0; k < NUM_KERNELS; ++k) {
    if (kernel[k])
      clReleaseKernel(kernel[k]);
    if (queue[k])
      clReleaseCommandQueue(queue[k]);
  }

  if (program)
    clReleaseProgram(program);
  if (context)
    clReleaseContext(context);

  if (input_buf)
    clReleaseMemObject(input_buf);
  if (huftable_buf)
    clReleaseMemObject(huftable_buf);
  if (output_aes_enc_buf)
    clReleaseMemObject(output_aes_enc_buf);
  if(output_aes_dec_buf)
    clReleaseMemObject(output_aes_dec_buf);
}

//--------------------------------------------------------------------------------------------------
//  Offload to FPGA
//---------------------------
//
//--------------------------------------------------------------------------------------------------

time_profiles_s offload_to_FPGA(unsigned char *input, unsigned int *huftable,
  unsigned int insize, unsigned int n_pages, unsigned int outsize, unsigned char marker,
  unsigned int *output_aes, gzip_out_info_t &gzip_out_info)
{
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

  huftable_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, HUFFTABLE_SIZE, NULL, NULL);
  checkError(status, "Failed to create buffer for huftable");

  // Output buffers
  output_aes_enc_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, outsize, NULL, &status);
  checkError(status, "Failed to create buffer for output_aes_encryption");

  output_aes_dec_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY, outsize, NULL, &status);
  checkError(status, "Failed to create buffer for output_aes_decryption");

  cl_event write_event[2];

  // Input data
  status = clEnqueueWriteBuffer(queue[GZIP_LOAD_LZ77], input_buf, CL_FALSE, 0, insize, input, 0,
     NULL, &write_event[0]);
  checkError(status, "Failed to transfer raw input");

  // Huffman table
  status = clEnqueueWriteBuffer(queue[GZIP_LOAD_HUFF], huftable_buf, CL_TRUE, 0, HUFFTABLE_SIZE,
    huftable, 0, NULL, &write_event[1]);
  checkError(status, "Failed to transfer huftable");

  //------------------------------------------------------------------------------------------------
  // [openCL] Set kernel arguments and enqueue commands into kernel
  //------------------------------------------------------------------------------------------------
  unsigned argi, k;

  unsigned int page_size = insize / n_pages;
  unsigned int mem_offset = page_size * 2;

  argi = 0;
  k = GZIP_LOAD_LZ77;
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_mem), &input_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &insize);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &page_size);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_char), (void *) &marker);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);

  argi = 0;
  k = GZIP_LOAD_HUFF;
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_mem), &huftable_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &n_pages);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);

  // AES-256-encryption
  argi = 0;
  k = AES_LOAD_BALANCER;
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int8), &key_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_mem), &output_aes_enc_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int8), &aes_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &n_pages);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &mem_offset);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);

  //------------------------------------------------------------------------------------------------
  // [openCL] Launch kernels
  //------------------------------------------------------------------------------------------------
  std::cout << "=====> Launch kernels" << std::endl;
  std::vector<cl_event> kernel_event(NUM_KERNELS);
  std::vector<cl_event> lz77_events(n_pages);
  cl_event finish_event[2];

  const size_t global_work_size = 1;
  const size_t local_work_size  = 1;

  // gzip load data
  std::cout << kernel_name[GZIP_LOAD_LZ77] << std::endl;
  clEnqueueNDRangeKernel(queue[GZIP_LOAD_LZ77], kernel[GZIP_LOAD_LZ77], 1, NULL, &global_work_size,
    &local_work_size, 2, write_event, &kernel_event.at(GZIP_LOAD_LZ77));

  // gzip load tree
  std::cout << kernel_name[GZIP_LOAD_HUFF] << std::endl;
  clEnqueueNDRangeKernel(queue[GZIP_LOAD_HUFF], kernel[GZIP_LOAD_HUFF], 1, NULL, &global_work_size,
    &local_work_size, 2, write_event, &kernel_event.at(GZIP_LOAD_HUFF));

  // lz77
  std::cout << kernel_name[GZIP_LZ770] << " x" << n_pages << std::endl;
  for (int n=0; n < n_pages; ++n) {
    clEnqueueNDRangeKernel(queue[GZIP_LZ770], kernel[GZIP_LZ770], 1, NULL, &global_work_size,
      &local_work_size, 0, NULL, &lz77_events.at(n));
  }

  // aes load balancer
  std::cout << kernel_name[AES_LOAD_BALANCER] << std::endl;
  clEnqueueNDRangeKernel(queue[AES_LOAD_BALANCER], kernel[AES_LOAD_BALANCER], 1, NULL,
    &global_work_size, &local_work_size, 2, write_event, &kernel_event.at(AES_LOAD_BALANCER));

  //------------------------------------------------------------------------------------------------
  // [openCL] Dequeue read buffers from kernel and verify results from them
  //------------------------------------------------------------------------------------------------
  std::cout << "enqueue final encryption data read" << std::endl;
  unsigned int *out_aes_encr = (unsigned int *)aocl_utils::alignedMalloc(outsize);
  clEnqueueReadBuffer(queue[AES_LOAD_BALANCER], output_aes_enc_buf, CL_FALSE, 0, outsize,
    out_aes_encr, 1, &kernel_event.at(AES_LOAD_BALANCER), &finish_event[0]);

  std::cout << "=====> WAITS" << std::endl;
  // gzip load data
  clWaitForEvents(1, &kernel_event.at(GZIP_LOAD_LZ77));
  std::cout << "Finished: " << kernel_name[GZIP_LOAD_LZ77] << std::endl;

  // gzip load tree
  clWaitForEvents(1, &kernel_event.at(GZIP_LOAD_HUFF));
  std::cout << "Finished: " << kernel_name[GZIP_LOAD_HUFF] << std::endl;

  // lz77
  clWaitForEvents(1, &lz77_events.at(n_pages-1));
  std::cout << "Finished: " << kernel_name[GZIP_LZ770] << " x" << n_pages << std::endl;

  // aes load balancer
  clWaitForEvents(1, &kernel_event.at(AES_LOAD_BALANCER));
  std::cout << "Finished: " << kernel_name[AES_LOAD_BALANCER] << std::endl;

  // wait for read event to complete
  clWaitForEvents(1, finish_event);

  //------------------------------------------------------------------------------------------------
  // [DEBUG] Save compressed and encrypted data to a file 
  //------------------------------------------------------------------------------------------------
  /*std::fstream file;
  file.open("file_encrypted_results.txt", std::ios::out);

  unsigned char *ptr_compressedNencrypted = (unsigned char*) out_aes_encr;
  for (unsigned int i=0; i<(2*gzip_out_info.compsize_huffman[0]); i++) {
    file << *ptr_compressedNencrypted;
    ptr_compressedNencrypted++;
  }

  file.close();*/
  aocl_utils::alignedFree(out_aes_encr);

  //------------------------------------------------------------------------------------------------
  // [AES] Decryption
  //------------------------------------------------------------------------------------------------

  argi = 0;
  k = AES_DECRYPT;
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_mem), &output_aes_enc_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_mem), &output_aes_dec_buf);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int8), &aes_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &n_pages);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int), (void *) &mem_offset);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);

  argi = 0;
  k = AES_DECRYPT_KEY;
  status = clSetKernelArg(kernel[k], argi++, sizeof(cl_int8), &key_config_run);
  checkError(status, "Failed to set argument %d on kernel %s", argi - 1, kernel_name[k]);

  // Launch aes_key kernel
  clEnqueueNDRangeKernel(queue[AES_DECRYPT_KEY], kernel[AES_DECRYPT_KEY], 1, NULL,
    &global_work_size, &local_work_size, 0, NULL, NULL);

  // Launch aes decryption kernel
  clEnqueueNDRangeKernel(queue[AES_DECRYPT], kernel[AES_DECRYPT], 1, NULL,
    &global_work_size, &local_work_size, 0, NULL, &kernel_event.at(AES_DECRYPT));

  // Read result
  clEnqueueReadBuffer(queue[AES_DECRYPT], output_aes_dec_buf, CL_FALSE, 0, outsize, output_aes, 1,
    &kernel_event.at(AES_DECRYPT), &finish_event[1]);

  // Wait for decryption write event
  clWaitForEvents(1, &finish_event[1]);
  std::cout << "Finished: decryption" << std::endl;

  //------------------------------------------------------------------------------------------------
  // [openCL] Time profiles
  //------------------------------------------------------------------------------------------------
  time_profiles_s profiles  = computeTimeProfiles(kernel_event);

  // Release all events
  for (unsigned int k = 0; k < kernel_event.size(); ++k) {
    if (k == GZIP_LZ770)
      continue;
    clReleaseEvent(kernel_event.at(k));
  }

  clReleaseEvent(write_event[0]);
  clReleaseEvent(write_event[1]);
  clReleaseEvent(finish_event[0]);
  clReleaseEvent(finish_event[1]);

  return profiles;
}
