.PHONE: help
help:
	$(ECHO) "Makefile Usage:"
	$(ECHO) "  make all"
	$(ECHO) "      Command to generate the design"
	$(ECHO) ""
	$(ECHO) "  make clean"
	$(ECHO) "      Command to remove the generated non-hardware files."
	$(ECHO) ""
	$(ECHO) "  make cleanall"
	$(ECHO) "      Command to remove all the generated files."

# Where is the Intel(R) FPGA SDK for OpenCL(TM) software?
ifeq ($(wildcard $(INTELFPGAOCLSDKROOT)),)
$(error You forgot to run `source /opt/inteldevstack/init_env.sh`)
endif
ifeq ($(wildcard $(INTELFPGAOCLSDKROOT)/host/include/CL/opencl.h),)
$(error You forgot to run `source /opt/inteldevstack/init_env.sh`)
endif

##############################################################################
## DEVICE
##############################################################################

BOARD := pac_s10_dc
MODE  := CBC

KERNEL_CL = hw/compNcrypt.cl

GZIP_ENGINES = 1
GZIP_STORE = false
GZIP_VEC = 16
GZIP_LBD = 1
GZIP_SOURCES = hw/gzip.cl

GZIP_FLAGS := -DVEC=$(GZIP_VEC)
GZIP_FLAGS += -DLOW_BANDWIDTH_DEVICE=$(GZIP_LBD)
GZIP_FLAGS += -DGZIP_ENGINES=$(GZIP_ENGINES)
GZIP_FLAGS += -DGZIP_STORE=$(GZIP_STORE)

AES_SOURCES = hw/aes/*.sv hw/aes/*.vhd
AES_KERNEL_CL = hw/aes.cl

ifeq ($(MODE),CBC)
AES_KERNEL_XML = hw/aes/aes_kernels.xml
else ifeq ($(MODE),ECB)
AES_KERNEL_XML = hw/aes/aes_kernels_ecb.xml
else ifeq ($(MODE),CTR)
AES_KERNEL_XML = hw/aes/aes_kernels_ctr.xml
else
$(error Invalid TARGET)
endif

AES_KERNEL_AOC = $(TARGET_DIR)/aes.aoco
AES_KERNEL_LIB = aes.aoclib

TARGET := hw
TARGET_HW := compNcrypt.aocx
TARGET_TAG := build
TARGET_DIR := $(TARGET_TAG).$(TARGET).$(GZIP_ENGINES)e.$(GZIP_VEC)v.$(GZIP_LBD)ldb

GZIP_FLAGS += -DBUILD_FOLDER=$(TARGET_DIR)

ifeq ($(TARGET),hw_emu)
AOC_FLAGS := -march=emulator
else ifeq ($(TARGET),hw_simu)
AOC_FLAGS := -march=simulator
else ifeq ($(TARGET),hw)
AOC_FLAGS :=
else
$(error Invalid TARGET)
endif

AOC_CMD = aoc
AOCL_CMD = aocl
AOC_CMD_X = qsub-aoc

##############################################################################
## HOST
##############################################################################

# OpenCL compile and link flags.
AOCL_COMPILE_CONFIG := $(shell aocl compile-config )
AOCL_LINK_LIBS := $(shell aocl ldlibs )
AOCL_LINK_FLAGS := $(shell aocl ldflags )
# Linking with defences enabled
AOCL_LINK_FLAGS += -z noexecstack
AOCL_LINK_FLAGS += -Wl,-z,relro,-z,now
AOCL_LINK_FLAGS += -Wl,-Bsymbolic
AOCL_LINK_FLAGS += -pie
AOCL_LINK_CONFIG := $(shell aocl link-config ) $(AOCL_LINK_FLAGS) $(AOCL_LINK_LIBS)

# Compilation flags
ifeq ($(DEBUG),1)
CXXFLAGS += -g
else
CXXFLAGS += -O2
endif

# Compiling with defences enabled
CXXFLAGS += -fstack-protector
CXXFLAGS += -D_FORTIFY_SOURCE=2
CXXFLAGS += -Wformat -Wformat-security
CXXFLAGS += -fPIE

# We must force GCC to never assume that it can shove in its own
# sse2/sse3 versions of strlen and strcmp because they will CRASH.
# Very hard to debug!
CXXFLAGS += -fPIC

# Compiler
CXX := g++

# Target
TARGET_SW := host

# Directories
INC_DIRS := sw/inc sw/common/inc
LIB_DIRS := 

# Files
INCS := $(wildcard sw/inc/*.h)
SRCS := $(wildcard sw/src/*.cc sw/src/*.cpp sw/common/src/AOCLUtils/*.cpp)
LIBS := rt pthread

.PHONY: all
all : host fpga

.PHONY: host
host : $(TARGET_DIR)/$(TARGET_SW)

.PHONY: fpga
fpga : $(TARGET_DIR)/$(TARGET_HW)

# HW
$(TARGET_DIR)/$(TARGET_HW) : $(TARGET_DIR)/$(AES_KERNEL_LIB) $(GZIP_SOURCES) $(AES_KERNEL_CL)
	$(AOC_CMD) $(AOC_FLAGS) $(KERNEL_CL) \
		-L $(TARGET_DIR) -l $(AES_KERNEL_LIB) \
		-o $(TARGET_DIR)/$(TARGET_HW) \
		$(GZIP_FLAGS) -board=$(BOARD)

# SW
$(TARGET_DIR)/$(TARGET_SW) : $(SRCS) $(INCS) $(TARGET_DIR)
	$(ECHO)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(GZIP_FLAGS) \
		$(foreach D,$(INC_DIRS),-I$D) \
		$(AOCL_COMPILE_CONFIG) $(SRCS) $(AOCL_LINK_CONFIG) \
		$(foreach D,$(LIB_DIRS),-L$D) \
		$(foreach L,$(LIBS),-l$L) \
		-o $(TARGET_DIR)/$(TARGET_SW)

# AES LIBRARY
$(TARGET_DIR)/$(AES_KERNEL_LIB) : $(AES_KERNEL_XML) $(AES_SOURCES) $(TARGET_DIR)
	$(AOC_CMD) -c $(AES_KERNEL_XML) -o $(AES_KERNEL_AOC)
	$(AOCL_CMD) library create -o $(TARGET_DIR)/$(AES_KERNEL_LIB) $(AES_KERNEL_AOC)

$(TARGET_DIR) :
	$(ECHO)mkdir -p $(TARGET_DIR)

.PHONY: clean
clean :
	rm -f $(TARGET_DIR)/$(TARGET_SW)

.PHONY: cleanall
cleanall:
	rm -rf $(TARGET_TAG)*
