#include "zlc/zlibcomplete.hpp"

#include <exception>
#include <functional>
#include <iostream>

#include <zlib.h>
#include <string>
#include <vector>

#include "compute_aes.h"

namespace zlibcomplete {

RawDeflater::RawDeflater(int level, flush_parameter autoFlush,
                               int windowBits) :
  ZLibBaseCompressor(level, autoFlush, -windowBits) {
}

std::string RawDeflater::deflate(const std::string& input) {
  return baseCompress(input);
}

void RawDeflater::deflate_file(const std::string& input, std::string& output, std::size_t insize, std::size_t total_size) {
  
  std::string in_split;
  std::string out_split;
  std::string header;

  std::size_t cnt = 0;
  std::size_t cmp_size = 0;

  while(cnt<total_size){
     
    in_split  = input.substr(cnt, insize);
    out_split = baseCompress(in_split);

    cmp_size  = out_split.size();
    header = std::to_string(cmp_size);
    header.resize(64);
  
    output.append(header);
    if(cmp_size%64 != 0)
      out_split.resize((cmp_size/64+1)*64);
    
    output.append(out_split);

    cnt += insize;
  }
}

void RawDeflater::defenc_file(const std::string& input, std::string& output, std::size_t insize, std::size_t total_size, unsigned int mode) {

  std::string in_split;
  std::string out_split;
  std::string header;
  
  compute_aes aes_local;
  std::size_t cnt = 0;
  std::size_t cmp_size = 0;
  
  unsigned char *encrypted = (unsigned char*) malloc(2*insize);

  while(cnt<total_size){
    
    in_split  = input.substr(cnt, insize);
    out_split = baseCompress(in_split);

    cmp_size  = out_split.size();
    header    = std::to_string(cmp_size);
    header.resize(64);

    output.append(header);
    
    if(cmp_size%64 != 0)
      out_split.resize((cmp_size/64+1)*64);
    
    aes_local.encrypt_file((unsigned char*)out_split.c_str(), out_split.size(), encrypted, mode);

    output.append((const char*)encrypted, out_split.size());
    
    cnt += insize;
  }
}

std::string RawDeflater::finish(void) {
  return baseFinish();
}

RawDeflater::~RawDeflater(void) {
}

RawInflater::RawInflater(int window_bits) : ZLibBaseDecompressor(-window_bits) {
}

RawInflater::~RawInflater(void) {
}

std::string RawInflater::inflate(const std::string& input) {
  return baseDecompress(input);
}

void RawInflater::inflate_file(const std::string& input, std::string& output) {
  
  std::string header;
  std::string in_split;

  int cmp_size = 0;
  int extra_size;

  std::size_t total_size = input.size();
  std::size_t cnt = 0;

  while(cnt<total_size){

    header = input.substr(cnt, 64);
    cmp_size = std::stoi(header, nullptr, 10);
    
    in_split = input.substr(cnt+64,cmp_size);
    
    output.append(baseDecompress(in_split));

    extra_size = 0;
    if(cmp_size%64 != 0)
     extra_size = (cmp_size/64+1)*64 - cmp_size;

    cnt += (64 + cmp_size + extra_size);
  }
}

void RawInflater::decinf_file(const std::string& input, std::string& output, std::size_t insize, unsigned int mode) {
 
  std::string header;
  std::string in_split;
  std::string cmp, decmp;

  int cmp_size = 0;
  int extra_size;

  std::size_t total_size = input.size();
  std::size_t cnt = 0;
  
  compute_aes aes_local;
  
  unsigned char *decrypted = (unsigned char*) malloc(2*insize);
  
  while(cnt<total_size){

    header   = input.substr(cnt, 64);
    cmp_size = std::stoi(header, nullptr, 10);
    
    extra_size = 0;
    if(cmp_size%64 != 0)
      extra_size = (cmp_size/64+1)*64 - cmp_size;

    in_split = input.substr(cnt+64, (cmp_size+extra_size));
   
    aes_local.decrypt_file((unsigned char*)in_split.c_str(), (cmp_size+extra_size), decrypted, mode);

    cmp = std::string((const char*)decrypted, cmp_size);

    output.append(baseDecompress(cmp));

    cnt += (64 + cmp_size + extra_size);
  }
}

} // namespace zlib_complete