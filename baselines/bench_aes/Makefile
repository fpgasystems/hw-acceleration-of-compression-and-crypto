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
INC_DIRS := inc

MODE := 1

#Files
INCS := $(wildcard ./inc/*.h)
SRCS := $(wildcard ./src/*.cpp)
LIBS := rt pthread

.PHONY: all clean clean_results sw_bench

all: $(SRCS) $(INCS)
	$(CXX) $(CXXFLAGS) $(foreach D,$(INC_DIRS),-I$D) $(SRCS) -o aes_test 

clean:
	rm aes_test

clean_results:
	rm aes_thr_*

# Run benchmark
sort_results:
	for f in ./*.dat; do \
	    v=name.dat; \
		sort -n -k 2 "$$f" > "$$v"; \
		mv "$$v" "$$f"; \
	done;

.PHONY: sw_bench
sw_bench: all 
	for f in /mnt/scratch/muellein/git/hana-io-tracing/results/2020-07-30-10-14-18/blocks/*; do \
		p=`basename "$$f" | awk -F'[-.]' '{print $$2}'`; \
		for m in 1 2 3 ; do \
			for th in 1 2; do \
				./aes_test "$$f" "$$p" "$$m" "$$th" 100; sync; \
			done; \
		done; \
	done;
	$(MAKE) sort_results
