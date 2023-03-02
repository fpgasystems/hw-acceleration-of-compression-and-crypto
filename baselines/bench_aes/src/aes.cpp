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
#include <string.h>
#include <string>
#include <cmath>
#include <stack>
#include <fstream>
#include <map>

#include <chrono>
#include <vector>
#include <algorithm>

#include <thread>

#include "compute_aes.h"
#include "../../../sw/src/helpers.h"

#define xstr(s) str(s)
#define str(s) #s

//#define DEBUG
void say_hello(){
  std::cout<<"Hello!" <<std::endl;
}
//---------------------------------------------------------------------------------------
//  MAIN FUNCTION
//---------------------------------------------------------------------------------------

int main(int argc, char **argv) 
{ 

  if (argc < 3) {
    std::cerr << argv[0] << " <file> <mode> <augument> <repet> <threads>" << std::endl;
    std::cout << "CBC:1 CTR:2 ECB:3" <<std::endl;
    return -1;
  }
  
  unsigned int mode = atoi(argv[2]);
  if (mode > 3) {
    std::cerr << "Unknown mode '" << argv[2] << '\'' << std::endl;
    std::cout << "CBC:1 CTR:2 ECB:3" <<std::endl;
    return  -1;
  }
  
  unsigned int augment = atoi(argv[3]);
  unsigned int repet   = atoi(argv[4]);
  unsigned const num_threads = (unsigned)std::min((size_t)augment, (size_t)strtoul(argv[5], nullptr, 0));
  unsigned const num_cores   = std::thread::hardware_concurrency();

  std::cout << "Input file: " << argv[1] << std::endl;
  std::cout << "AES Mode  : " << argv[2] << std::endl;
  std::cout << "Augument  : " << argv[3] << std::endl;
  std::cout << "Repet     : " << argv[4] << std::endl;
  std::cout << "#Threads  : " << num_threads << " (mod " << num_cores << " cores)" <<std::endl;
  
  const char* filename = argv[1];
  unsigned int insize;
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
    std::cout<<"File size [B] : "<< insize <<std::endl;
  
  unsigned int total_size = insize*augment;
  std::cout<<"Total size [B]: " << total_size <<std::endl;

  unsigned char *input      = (unsigned char *)malloc(total_size);
  unsigned char *output_enc = (unsigned char *)malloc(total_size);
  unsigned char *output_dec = (unsigned char *)malloc(total_size);
  
  // Read and close input file 
  if (fseek(f, 0, SEEK_SET) != 0)
    exit(1);
  if (fread(input, 1, insize, f) != insize)
    exit(1);
  fclose(f);
  
  for(unsigned i=1; i<augment; i++)
    memcpy((input+i*insize), input, (size_t)insize);

  int numerrors = 0;

  std::vector<float> th_encrypt;
  std::vector<float> th_decrypt;
  
  //std::vector<compute_aes> aes_app;
  compute_aes aes_app[32];
  
   for(unsigned int r=0; r<repet; r++) {

      //for(unsigned i = 0; i < num_threads; i++)  
      //    aes_app.emplace_back();
      
      std::vector<std::thread> threads_enc, threads_dec;
  
      size_t ofs = 0;
 
      auto const t0 = std::chrono::system_clock::now();
 
      for(unsigned int i=0; i<num_threads; i++){
         size_t const nofs  = (((i+1)*total_size)/num_threads);
         size_t const cnt = nofs-ofs;
         
         threads_enc.emplace_back(std::thread(&compute_aes::encrypt_file,&aes_app[i],
           input+ofs, cnt, output_enc+ofs, mode));
 
         cpu_set_t  cpus;
         CPU_ZERO(&cpus);
         CPU_SET(i%num_cores, &cpus);
         pthread_setaffinity_np(threads_enc.back().native_handle(), sizeof(cpus), &cpus);
         
         ofs = nofs;
      }
 
     // Wait for all to finish
     for(std::thread& t : threads_enc) t.join();
     
     auto const t1 = std::chrono::system_clock::now();

     ofs = 0;

     for(unsigned int i=0; i<num_threads; i++){
         size_t const nofs = (((i+1)*total_size)/num_threads);
         size_t const cnt  = nofs-ofs;
         
         threads_dec.emplace_back(std::thread(&compute_aes::decrypt_file,&aes_app[i],
           output_enc+ofs, cnt, output_dec+ofs, mode));
 
         cpu_set_t  cpus;
         CPU_ZERO(&cpus);
         CPU_SET(i%num_cores, &cpus);
         pthread_setaffinity_np(threads_dec.back().native_handle(), sizeof(cpus), &cpus);
         
         ofs = nofs;
     }
     
     // Wait for all to finish
     for(std::thread& t : threads_dec) t.join();
 
     auto const t2 = std::chrono::system_clock::now();
 
     float const d0 = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
     float const d1 = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
   
     th_encrypt.push_back((double)(total_size)/d0);
     th_decrypt.push_back((double)(total_size)/d1);

   }
 
   numerrors = memcmp((const void *)input, (const void *)output_dec, (size_t)(total_size));
  
  if(numerrors!=0){
    std::cerr<<"Encryption and decryption don't give equal results!"<<std::endl;
    return -1;
  }
  else{
    
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

    FILE *file;
    //Open input file
    file = fopen("aes_throughput_encrypt_results.txt", "a");

    //input files size
    unsigned int file_size = get_filesize(file);
    if (file_size == 0)
      fprintf(file, "file_size mode repet min max p1 p5 p25 p50 p75 p95 p99 \n");
 
    fseek(file, file_size, SEEK_END); 
    fprintf(file,"%u %u %u %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",total_size, mode, repet, min_encrypt, max_encrypt, p1_encrypt, p5_encrypt, p25_encrypt, p50_encrypt, p75_encrypt, p95_encrypt, p99_encrypt);
    fclose(file);

    file = fopen("aes_throughput_decrypt_results.txt", "a");
    //input files size
    file_size = get_filesize(file);
    if (file_size == 0)
      fprintf(file, "file_size repet min max p1 p5 p25 p50 p75 p95 p99 \n");
   
    fseek(file, file_size, SEEK_END);
    fprintf(file,"%u %u %u %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",total_size, mode, repet, min_decrypt, max_decrypt, p1_decrypt, p5_decrypt, p25_decrypt, p50_decrypt, p75_decrypt, p95_decrypt, p99_decrypt);
    fclose(file); 
  }  

  th_encrypt.clear();
  th_decrypt.clear();

  return 0;
}