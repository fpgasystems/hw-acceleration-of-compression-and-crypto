#ifndef HELPERS_H
#define HELPERS_H

#include "AOCLUtils/aocl_utils.h"

//-----------------
// Data definition
//-----------------
// Data types
typedef struct {
    uint cntr_nonce[2];
    uint elements[2];
    uint iv[4];
} aes_config;

typedef struct {
    uint key[8];
} key_config;

typedef struct {
    cl_ulong gzip_com;
    cl_ulong gzip_dec;
    cl_ulong aes_enc;
    cl_ulong aes_dec;
    cl_ulong compNcrypt;
} time_profiles_s;

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

cl_ulong getStartEndTime(std::vector<cl_event*> &events, unsigned int in, unsigned num_events) {
  cl_int status;

  cl_ulong min_start = 0;
  cl_ulong max_end = 0;
  for(unsigned i = in; i < (in + num_events); ++i) {
    cl_ulong start, end;
    status = clGetEventProfilingInfo(*events.at(i), CL_PROFILING_COMMAND_START, sizeof(start), &start, NULL);
    checkError(status, "Failed to query event start time");
    status = clGetEventProfilingInfo(*events.at(i), CL_PROFILING_COMMAND_END, sizeof(end), &end, NULL);
    checkError(status, "Failed to query event end time");

    if(i == in) {
      min_start = start;
      max_end = end;
    }
    else {
      if(start < min_start) {
        min_start = start;
      }
      if(end > max_end) {
        max_end = end;
      }
    }
  }

  return max_end - min_start;
}

#endif
