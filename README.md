# CompNcrypt
FPGA OpenCL offloading pipelines for C|Dec-ompression and En|De-cryption 

## Prerequisites
Project tested on Intel FPGA Programmable Acceleration Card D5005 within a local server and IL Academic Compute Environment (ACE). 

### IL Academic Compute Environment (ACE)
```bash
source /export/fpga/bin/setup-fpga-env fpga-pac-s10
qsub-fpga
cd hw-acceleration-of-compression-and-crypto
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
./build.hw.1e.16v.1ldb/host file0.txt 1178
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file1.txt 14266048
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file2.txt 722240
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file3.txt 1932
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file4.txt 135040
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file5.txt 2176
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file6.txt 34816
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file7.txt 8704
./build.hw.1e.16v.1ldb/host /tmp/monica/input_files/file8.txt 17408
```

