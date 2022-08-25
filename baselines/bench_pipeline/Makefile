# MIT License

# Copyright (c) 2021 FPGA @ Systems Group, ETH Zurich

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#*************************************************************************

.PHONE: help
help:
	$(ECHO) "Makefile Usage:"
	$(ECHO) "  make all"
	$(ECHO) "      Command to generate the design"
	$(ECHO) ""
	$(ECHO) "  make clean"
	$(ECHO) "      Command to remove the generated non-hardware files."

# Compilation flags
ifeq ($(DEBUG),1)
CXXFLAGS += -g
else
CXXFLAGS += -O3
endif

# Compiling with defences enabled
CXXFLAGS += -std=c++11 -Wall -msse2 -msse -march=native -pthread -maes

# Compiler
CXX := g++

#Directories
INC_DIRS := ../bench_aes/inc inc/lib inc

#CBC:1 CTR:2 ECB:3
MODE := 1    
DIR  := /local/chiosam

#Files
INCS := $(wildcard ../bench_aes/inc/*.h ./inc/*.h)
SRCS := $(wildcard ./inc/lib/*.cpp ./src/*.cpp)
LIBS := rt pthread

.PHONY: all clean clean-results sw-bench sw-bench-test

all: $(SRCS) $(INCS)
	$(CXX) $(CXXFLAGS) $(foreach D,$(INC_DIRS),-I$D) -lz $(SRCS) -o pipe_test 

clean:
	rm pipe_test

clean-results:
	rm pipe_thr_*

sort-results:
	for f in ./*.txt; do \
		v=name.txt; \
		sort -n -k 2 "$$f" > "$$v"; \
		mv "$$v" "$$f"; \
	done;

sort-batch-results:
	for f in ./*.txt; do \
		v=name.txt; \
		sort -n -k2 -k3 "$$f" > "$$v"; \
		mv "$$v" "$$f"; \
	done;

# Run benchmark
#./pipe_test <file> <page_size> <window_bits> <level> <mode> <threads> <repeat> 

.PHONY: sw-bench-single-r100
sw-bench-single-r100: all
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		base64 "$$f" > $(DIR)/fsample.txt; \
		w=15; \
		l=1;  \
		for g in /mnt/scratch/mchiosa/Datasets/Hana/files_ingo/"$$p"/*; do \
			base64 "$$g" > $(DIR)/gsample.txt; \
			val=`wc -c $(DIR)/gsample.txt | awk -F'[/]' '{print $$1}'`; \
			for th in 1 2; do \
				for m in 1 2 3; do \
					./pipe_test $(DIR)/fsample.txt "$$val" "$$w" "$$l" "$$m" "$$th" 100; sync; \
				done; \
			done; \
			rm $(DIR)/gsample.txt; \
			break; \
		done; \
		rm $(DIR)/fsample.txt; \
	done;
	$(MAKE) sort-results

# 64ki - 65536 | 128k - 131072 | 256k - 262144 | 64M - 67108864
.PHONY: sw-bench-batch
sw-bench-batch: all
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		base64 "$$f" > $(DIR)/fsample.txt; \
		w=15; \
		l=1;  \
		for g in /mnt/scratch/mchiosa/Datasets/Hana/files_ingo/"$$p"/*; do \
			base64 "$$g" > $(DIR)/gsample.txt; \
			val=`wc -c $(DIR)/gsample.txt | awk -F'[/]' '{print $$1}'`; \
			for th in 1; do \
				for b in 65536 131072 262144 67108864; do \
					for m in 1 2 3; do \
						./pipe_test $(DIR)/fsample.txt "$$val" "$$w" "$$l" "$$m" "$$th" 4 "$$b"; sync; \
					done; \
				done; \
			done; \
			rm $(DIR)/gsample.txt; \
			break; \
		done; \
		rm $(DIR)/fsample.txt; \
	done;
	$(MAKE) sort-batch-results

.PHONY: sw-bench-page-test
sw-bench-page-test: all
	for f in ../../files_page/*; do \
		p=`basename "$$f" | awk -F'[_.]' '{print $$2}'`; \
		w=15; \
		l=1; \
		for th in 1 2; do \
			./pipe_test "$$f" "$$p" "$$w" "$$l" $(MODE) "$$th" 100; sync; \
		done; \
	done;

.PHONY: sw-bench-batch-test
sw-bench-batch-test: all
	for f in ../../files_batch/*; do \
		p=`basename "$$f" | awk -F'[_.]' '{print $$2}'`; \
		w=15; \
		l=1; \
		for th in 1; do \
			./pipe_test "$$f" "$$p" "$$w" "$$l" $(MODE) "$$th" 100; sync; \
		done; \
	done;

sw-bench-test: 
	$(MAKE) sw-bench-page-test 
	$(MAKE) sw-bench-batch-test

size-correlation:
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		for g in /mnt/scratch/mchiosa/Datasets/Hana/files_ingo/"$$p"/*; do \
			base64 "$$g" > $(DIR)/gsample.txt; \
			val=`wc -c $(DIR)/gsample.txt | awk -F'[/]' '{print $$1}'`; \
			echo "$$p" "$$val"; \
			rm $(DIR)/gsample.txt; \
			break; \
		done; \
	done;