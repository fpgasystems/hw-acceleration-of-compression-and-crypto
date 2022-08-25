// Copyright (C) 2021 - Systems Group, ETH Zurich

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//***

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cmath>
#include <stack>
#include <fstream>
#include <cstring>
#include <map>

#include <chrono>
#include <vector>
#include <algorithm>

#include <thread>
#include <exception>

#include <zlc/zlibcomplete.hpp>
#include "compute_flate.h"

using namespace zlibcomplete;

unsigned long get_filesize(FILE *f)
{
    long size;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size < 0)
      exit(1);

    return size;
}

int main(int argc, char **argv)
{  
  FILE *f, *g, *h;

  if (argc < 7) {

    std::cerr << argv[0] << " <file> <page_size> <window_bits> <level> <threads> <repeat> " << std::endl;
    return -1;
  }

  const char* filename = argv[1];
  //Open input file
  f = fopen(filename, "rb");
  if (f == NULL){

    printf("Unable to open %s\n", filename);
    return -1;
  }
  
  size_t page_size = atoi(argv[2]);
  int window_bits  = atoi(argv[3]);
  int level        = atoi(argv[4]);
  
  if (window_bits < 8 || window_bits > 15){

    window_bits = 15;
    std::cout << "Window bits size is between [8,15]." << std::endl;
    std::cout << "Default size for window bits: 15" << std::endl;

    return -1;
  }

  if (level < 1 || level > 9){

    level = 9;
    std::cout << "Level size is between [0,9]." << std::endl;
    std::cout << "Default level size: 9" << std::endl;
    return -1;
  }

  unsigned const num_threads = atoi(argv[5]);
  unsigned const repeat      = atoi(argv[6]);
  unsigned const num_cores   = std::thread::hardware_concurrency();

  std::cout<< "Input file : " << filename << std::endl;
  std::cout<< "Level      : " << level << std::endl;
  std::cout<< "Window bits: " << window_bits << std::endl;
  std::cout<< "Repeat     : " << repeat << std::endl;
  std::cout<< "Threads    : " << num_threads << " (mod " << num_cores << " cores)" <<std::endl;
  
  size_t total_size, file_size;
  
  file_size = get_filesize(f);

  if(file_size < 1){

    printf("File %s is empty\n", filename);
    fclose(f);
    return -1;
  }
  else{

    std::cout<<"Page size [B] : "<< page_size <<std::endl;  
    std::cout<<"File size [B] : "<< file_size <<std::endl;
  }
  
  total_size = page_size * num_threads * repeat;

  unsigned int augment = 1;

  if(total_size>file_size)
    augment = total_size/file_size;
  else
    total_size = file_size;
  
  std::cout<<"Total size [B]: "<<(unsigned long)page_size * num_threads * repeat<<std::endl;
  std::cout<<"Augment       : "<<augment<<std::endl;

  char *input          = (char *)malloc(total_size);
  char *output_inflate = (char *)malloc(total_size);
  
  // Read and close input file 
  if (fseek(f, 0, SEEK_SET) != 0)
    exit(1);
  if (fread(input, 1, file_size, f) != file_size)
    exit(1);
  fclose(f);
  
  for(unsigned i=1; i<augment; i++)
    memcpy((input+i*file_size), input, (size_t)file_size);

  std::string in[num_threads];
  std::string output[num_threads];
  std::string result[num_threads];
  
  std::vector<RawDeflater *> deflater;
  for(unsigned i=0; i<num_threads; i++) deflater.push_back(new RawDeflater(level, auto_flush, window_bits));

  std::vector<RawInflater *> inflater; 
  for(unsigned i=0; i<num_threads; i++) inflater.push_back(new RawInflater(window_bits));
   
  int numerrors = 0;
  
  std::vector<float> th_deflate;
  std::vector<float> th_inflate;
  std::vector<float> comp_ratio;
   
  size_t ofs = 0;
  
  for(unsigned int r=0; r<repeat; r++) {
      
    std::vector<std::thread> threads_def, threads_inf;  // def-deflate(compression); inf-inflate(decompression)
   
    auto const t0 = std::chrono::system_clock::now();
 
    for(unsigned int i=0; i<num_threads; i++){
            
      in[i].assign(input+ofs+i*page_size, page_size);
     
      threads_def.emplace_back(std::thread(&RawDeflater::deflate_file, deflater[i],
          std::cref(in[i]), std::ref(output[i]), page_size, page_size));
      
      cpu_set_t  cpus;
      CPU_ZERO(&cpus);
      CPU_SET((2*i)%num_cores, &cpus);
      pthread_setaffinity_np(threads_def.back().native_handle(), sizeof(cpus), &cpus);
    }
 
    // Wait for all threads to finish
    for(std::thread& t : threads_def) t.join();

    auto const t1 = std::chrono::system_clock::now();
    
    for(unsigned int i=0; i<num_threads; i++){
              
      threads_inf.emplace_back(std::thread(&RawInflater::inflate_file, inflater[i],
          std::cref(output[i]), std::ref(result[i])));
      
      cpu_set_t  cpus;
      CPU_ZERO(&cpus);
      CPU_SET((2*i)%num_cores, &cpus);
      pthread_setaffinity_np(threads_inf.back().native_handle(), sizeof(cpus), &cpus);    
    }
    
    // Wait for all to finish
    for(std::thread& t : threads_inf) t.join();

    auto const t2 = std::chrono::system_clock::now();
    
    size_t compressed_sizes = 0;
    size_t compressed_min   = page_size;

    for(unsigned int i=0; i<num_threads; i++){

      result[i].copy(output_inflate+ofs+i*page_size, result[i].size());
      compressed_sizes += output[i].size();
      
      if(output[i].size()<compressed_min)
        compressed_min = output[i].size();
    }
    
    ofs += num_threads*page_size;

    float const d0 = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    float const d1 = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
   
    th_deflate.push_back((double)(num_threads*page_size)/d0);
    th_inflate.push_back((double)(compressed_sizes)/d1);
    comp_ratio.push_back((double)(page_size)/compressed_min);

    for(unsigned i=0; i<num_threads; i++){

      in[i].clear();
      output[i].clear();
      result[i].clear();
    }
  }

  for(unsigned i=0; i<num_threads; i++) {
    
    delete(deflater[i]);
    delete(inflater[i]);
  }

  numerrors = memcmp((const void *)input, (const void *)output_inflate, (size_t)(page_size*num_threads*repeat));
  
  if(numerrors!=0){

    g = fopen("input.txt", "a");
    h = fopen("output.txt", "a");
    std::cout<<"Position:"<<numerrors<<std::endl;

    fwrite(input, 1, page_size*num_threads*repeat, g);
    fwrite(output_inflate, 1, page_size*num_threads*repeat, h);

    std::cerr<<"Compression and decompression don't give equal results!"<<std::endl;
    return -1;
  }
    
  std::sort(th_deflate.begin(),th_deflate.end());
  std::sort(th_inflate.begin(),th_inflate.end());

  std::sort(comp_ratio.begin(),comp_ratio.end());

  float min_deflate = th_deflate[0];
  float min_inflate = th_inflate[0];
  float max_deflate = th_deflate[repeat-1];
  float max_inflate = th_inflate[repeat-1];

  float p25_deflate = 0.0;
  float p50_deflate = 0.0;
  float p75_deflate = 0.0;
  
  float p25_inflate = 0.0;
  float p50_inflate = 0.0;
  float p75_inflate = 0.0;

  if(repeat>=4){
      p25_deflate = th_deflate[(repeat/4)-1];
      p50_deflate = th_deflate[(repeat/2)-1];
      p75_deflate = th_deflate[(repeat*3)/4-1];

      p25_inflate = th_inflate[(repeat/4)-1];
      p50_inflate = th_inflate[(repeat/2)-1];
      p75_inflate = th_inflate[(repeat*3)/4-1];
  }

  float p1_deflate  = 0.0;
  float p5_deflate  = 0.0;
  float p95_deflate = 0.0;
  float p99_deflate = 0.0;
    
  float p1_inflate  = 0.0;
  float p5_inflate  = 0.0;
  float p95_inflate = 0.0;
  float p99_inflate = 0.0;

  if (repeat >= 100) {
    p1_deflate  = th_deflate[((repeat)/100)-1];
    p5_deflate  = th_deflate[((repeat*5)/100)-1];
    p95_deflate = th_deflate[((repeat*95)/100)-1];
    p99_deflate = th_deflate[((repeat*99)/100)-1];
    
    p1_inflate  = th_inflate[((repeat)/100)-1];
    p5_inflate  = th_inflate[((repeat*5)/100)-1];
    p95_inflate = th_inflate[((repeat*95)/100)-1];
    p99_inflate = th_inflate[((repeat*99)/100)-1];
  } 

  float min_ratio = comp_ratio[0];
  float max_ratio = comp_ratio[repeat-1];

  float p25_ratio = 0.0;
  float p50_ratio = 0.0;
  float p75_ratio = 0.0;
  
  if(repeat>=4){

      p25_ratio = comp_ratio[(repeat/4)-1];
      p50_ratio = comp_ratio[(repeat/2)-1];
      p75_ratio = comp_ratio[(repeat*3)/4-1];
  }

  float p1_ratio  = 0.0;
  float p5_ratio  = 0.0;
  float p95_ratio = 0.0;
  float p99_ratio = 0.0;
    
  if (repeat >= 100) {

    p1_ratio  = comp_ratio[((repeat)/100)-1];
    p5_ratio  = comp_ratio[((repeat*5)/100)-1];
    p95_ratio = comp_ratio[((repeat*95)/100)-1];
    p99_ratio = comp_ratio[((repeat*99)/100)-1];
  } 

  FILE *file;
  //Open input file
  std::string name = "zlib_thr_deflate_" + std::to_string(window_bits) + "_" + std::to_string(level) + "_" + std::to_string(num_threads) + ".dat";

  file = fopen(name.c_str(), "a");

  //input files size
  unsigned int file_size_result = get_filesize(file);
  if (file_size_result == 0)
    fprintf(file, "file_size page_size window level repeat min max p1 p5 p25 p50 p75 p95 p99 \n");
 
  fseek(file, file_size_result, SEEK_END); 
  fprintf(file,"%zu %zu %u %u %u %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n",total_size, page_size, window_bits, level, repeat, 
    min_deflate, max_deflate, p1_deflate, p5_deflate, p25_deflate, p50_deflate, p75_deflate, p95_deflate, p99_deflate);
  fclose(file);
  
  name = "zlib_thr_inflate_" + std::to_string(window_bits) + "_" +std::to_string(level) + "_" + std::to_string(num_threads) + ".dat";
  file = fopen(name.c_str(), "a");

  //input files size
  file_size_result = get_filesize(file);
  if (file_size_result == 0)
    fprintf(file, "file_size page_size window level repeat min max p1 p5 p25 p50 p75 p95 p99 \n");
   
  fseek(file, file_size_result, SEEK_END);
  fprintf(file,"%zu %zu %u %u %u %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n",total_size, page_size, window_bits, level, repeat, 
    min_inflate, max_inflate, p1_inflate, p5_inflate, p25_inflate, p50_inflate, p75_inflate, p95_inflate, p99_inflate);
  fclose(file);  

  name = "zlib_compression_ratio_" + std::to_string(window_bits) + "_" +std::to_string(level) + ".dat";
  file = fopen(name.c_str(), "a");

  //input files size
  file_size_result = get_filesize(file);
  if (file_size_result == 0)
    fprintf(file, "file_size page_size window level repeat min max p1 p5 p25 p50 p75 p95 p99 \n");
   
  fseek(file, file_size_result, SEEK_END);
  fprintf(file,"%zu %zu %u %u %u %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n",total_size, page_size, window_bits, level, repeat, 
    min_ratio, max_ratio, p1_ratio, p5_ratio, p25_ratio, p50_ratio, p75_ratio, p95_ratio, p99_ratio);
  fclose(file);  

  th_deflate.clear();
  th_inflate.clear();
  comp_ratio.clear();

  return 0;
}
