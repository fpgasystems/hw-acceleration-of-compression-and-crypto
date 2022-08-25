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
INC_DIRS := inc inc/lib

MODE := 1
DIR  := /local/chiosam

#Files
INCS := $(wildcard ./inc/*.h)
SRCS := $(wildcard ./src/*.cpp ./inc/lib/*.cpp)
LIBS := rt pthread

.PHONY: all clean clean_results sw_bench

all: $(SRCS) $(INCS)
	$(CXX) $(CXXFLAGS) $(foreach D,$(INC_DIRS),-I$D) -lz $(SRCS) -o flate_test 

clean:
	rm flate_test

clean-results:
	rm zlib_*

# Run benchmark
sort-results:
	for f in ./*.dat; do \
		v=name.dat; \
		sort -n -k 2 "$$f" > "$$v"; \
		mv "$$v" "$$f"; \
	done;

.PHONY: sw-bench
sw-bench: all
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		base64 "$$f" > $(DIR)/sample.txt; \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		w=15; \
		l=1; \
		for th in 1 2; do \
			./flate_test $(DIR)/sample.txt "$$p" "$$w" "$$l" "$$th" 100; sync; \
		done; \
		rm $(DIR)/sample.txt; \
	done;
	$(MAKE) sort-results

.PHONY: sw-bench-single-r100
sw-bench-single-r100: all
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		base64 "$$f" > $(DIR)/fsample.txt; \
		w=15; \
		l=1;  \
		for g in /mnt/scratch/mchiosa/Datasets/Hana/files_ingo/"$$p"/*; do \
			base64 "$$g" > $(DIR)/gsample.txt; \
			val=`wc -c $(DIR)/gsample.txt`; \
			for th in 1 2; do \
				./flate_test $(DIR)/fsample.txt "$$val" "$$w" "$$l" "$$th" 100; sync; \
			done; \
			rm $(DIR)/gsample.txt; \
			break; \
		done; \
		rm $(DIR)/fsample.txt; \
	done;
	$(MAKE) sort-results

.PHONY: sw-bench-100
sw-bench-100: all
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		i=0; \
		w=15; \
		l=1;  \
		for g in /mnt/scratch/mchiosa/Datasets/Hana/files_ingo/"$$p"/*; do \
			base64 "$$g" > $(DIR)/sample.txt; \
			val=`wc -c $(DIR)/sample.txt`; \
			for th in 1 2; do \
				./flate_test $(DIR)/sample.txt "$$val" "$$w" "$$l" "$$th" 1; sync; \
			done; \
			rm $(DIR)/sample.txt; \
			((i="$$i"+1)); \
			if [ "$$i" -ge 100 ]; then \
				break; \
			fi; \
		done; \
	done;
	$(MAKE) sort-results

.PHONY: sw-test
sw-test: all
	for f in ../../files_page/*; do \
		base64 "$$f" > $(DIR)/sample.txt; \
		p=`basename "$$f" | awk -F'[_.]' '{print $$2}'`; \
		w=15; \
		l=9; \
		for th in 1 2; do \
			./flate_test $(DIR)/sample.txt "$$p" "$$w" "$$l" "$$th" 100; sync; \
		done; \
		rm $(DIR)/sample.txt; \
	done;
	$(MAKE) sort-results
