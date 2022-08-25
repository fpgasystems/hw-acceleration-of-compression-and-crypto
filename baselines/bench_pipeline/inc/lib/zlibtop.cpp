#include <zlc/zlibcomplete.hpp>
#include <exception>
#include <zlib.h>
#include <string.h>
#include <iostream>
#include <stdint.h>

namespace zlibcomplete {

ZLibBaseCompressor::ZLibBaseCompressor(int level, flush_parameter autoFlush,
                                       int windowBits) {
  int retval;
  finished_ = false;
  autoFlush_ = autoFlush != no_flush;
  strm_.zalloc = Z_NULL;
  strm_.zfree = Z_NULL;
  strm_.opaque = Z_NULL;
  if (level < 1 || level > 9) {
    level = 9;
  }
  const int memLevel = 9;
  retval = deflateInit2(&strm_, level, Z_DEFLATED, windowBits,
                               memLevel, Z_DEFAULT_STRATEGY);
  if (retval != Z_OK) {
    throw std::bad_alloc();
  }
}

std::string ZLibBaseCompressor::baseCompress(const std::string& input) {
  std::string result;
  int retval;

  if (finished_) {
    std::cerr << "Cannot compress data after calling finish.\n";
    throw std::exception();
  }

  for (uint32_t i = 0; i < input.length(); i += ZLIB_COMPLETE_CHUNK) {
    uint32_t howManyLeft = (input.length() - i);
    const bool isLastRound = (howManyLeft <= ZLIB_COMPLETE_CHUNK);
    uint32_t howManyWanted = (howManyLeft > ZLIB_COMPLETE_CHUNK) ?
                           ZLIB_COMPLETE_CHUNK : howManyLeft;
    memcpy(in_, input.data()+i, howManyWanted);
    strm_.avail_in = howManyWanted;
    strm_.next_in = (Bytef *) in_;

    //std::cout<<"Length left: "<< howManyLeft<<std::endl;
    //int have_previous = 0;
    do {
      int have;
      
      strm_.avail_out = ZLIB_COMPLETE_CHUNK;
      strm_.next_out = (Bytef *) out_;
      retval = deflate(&strm_, (autoFlush_ && isLastRound) ? Z_SYNC_FLUSH : Z_NO_FLUSH);
      //std::string out_intrm = (std::string)(out_);
      //std::cout<<"Interm size: "<<out_intrm.size()<<std::endl;

      if (retval == Z_STREAM_ERROR) {
        throw std::bad_alloc();
      }
      //have_previous ++;
      have = ZLIB_COMPLETE_CHUNK - strm_.avail_out;
      result += std::string(out_, have);
      //std::cout<<"One: "<<result.size()<<" "<<have<<std::endl;
      //std::cout<<"Stream available out: "<<strm_.avail_out<<std::endl;
    } while (strm_.avail_out == 0);
    //std::cout<<"Out of do"<<" "<<have_previous<<std::endl;
  }

  //std::cout<<"Final: "<<result.size()<<std::endl;
  return result;
}

std::string ZLibBaseCompressor::baseFinish(void) {
  std::string result;
  int retval;
  int have;
  if (finished_) {
    std::cerr << "Cannot call finish more than once.\n";
    throw std::exception();
  }
  finished_ = true;
  strm_.avail_in = 0;
  strm_.next_in = (Bytef *) in_;
  do {
    strm_.avail_out = ZLIB_COMPLETE_CHUNK;
    strm_.next_out = (Bytef *) out_;
    retval = deflate(&strm_, Z_FINISH);
    if (retval == Z_STREAM_ERROR) {
      throw std::bad_alloc();
    }
    have = ZLIB_COMPLETE_CHUNK - strm_.avail_out;
    result += std::string(out_, have);
  } while (strm_.avail_out == 0);
  return result;
}

ZLibBaseCompressor::~ZLibBaseCompressor(void) {
  deflateEnd(&strm_);
}

ZLibBaseDecompressor::ZLibBaseDecompressor(int windowBits) {
  int retval;
  strm_.zalloc = Z_NULL;
  strm_.zfree = Z_NULL;
  strm_.opaque = Z_NULL;
  strm_.avail_in = 0;
  strm_.next_in = Z_NULL;
  retval = inflateInit2(&strm_, windowBits);
  if (retval != Z_OK) {
    throw std::bad_alloc();
  }
}

ZLibBaseDecompressor::~ZLibBaseDecompressor(void) {
  inflateEnd(&strm_);
}

std::string ZLibBaseDecompressor::baseDecompress(const std::string& input) {
  int retval;
  std::string result;
  for (uint32_t i = 0; i < input.length(); i += ZLIB_COMPLETE_CHUNK) {
    uint32_t howManyLeft = (input.length() - i);
    uint32_t howManyWanted = (howManyLeft > ZLIB_COMPLETE_CHUNK) ?
                           ZLIB_COMPLETE_CHUNK : howManyLeft;
    memcpy(in_, input.data()+i, howManyWanted);
    strm_.avail_in = howManyWanted;
    strm_.next_in = (Bytef *) in_;
    if (strm_.avail_in == 0) {
      break;
    }
    do {
      int have;
      strm_.avail_out = ZLIB_COMPLETE_CHUNK;
      strm_.next_out = (Bytef *) out_;
      retval = inflate(&strm_, Z_NO_FLUSH);
      if (retval == Z_STREAM_ERROR) {
        throw std::bad_alloc();
      }
      have = ZLIB_COMPLETE_CHUNK - strm_.avail_out;
      result += std::string(out_, have);
    } while (strm_.avail_out == 0);
  }
  return result;
}

} // namespace zlib_complete
