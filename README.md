# CompNcrypt
FPGA OpenCL offloading pipelines for C|Dec-ompression and En|De-cryption 

## Prerequisites
Project tested on Intel FPGA Programmable Acceleration Card D5005 within a local server and IL Academic Compute Environment (ACE). 

### Local server
```bash
ssh d5005.ethz.ch 
lspci | grep "accelerator"
# af:00.0 Processing accelerators: Intel Corporation Device 0b2b (rev 01)
source /opt/inteldevstack/init_env.sh
source /opt/intelFPGA_pro/quartus_19.2.0b57/hld/init_opencl.sh		
cd compress-and-encrypt
```

### IL Academic Compute Environment (ACE)
```bash
source /export/fpga/bin/setup-fpga-env fpga-pac-s10
qsub-fpga
cd compress-and-encrypt
```
	
## Dependencies

## Build `HW`
```bash
make fpga
```

## Build `SW (host)`
```
make host
```

## Test 
```
./build.hw.1e.16v.1ldb/host file0.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file1.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file2.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file3.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file4.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file5.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file6.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file7.txt
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file8.txt
```

## Simulation
