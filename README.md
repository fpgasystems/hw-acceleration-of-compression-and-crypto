# CompNcrypt
FPGA OpenCL offloading pipelines for C|Dec-ompression and En|De-cryption.

## Setup
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

## Build

#### Hardware
```bash
make fpga
```
| Parameter    | Values              | Default | Description       |
|--------------|---------------------|---------|-------------------|
| GZIP_ENGINES | int 1 -- 4          | 1       | # of GZIP engines |
| AES_ENGINES  | int 1 -- 8          | 1       | # of AES engines  |
| AES_MODE     | `CBC`, `EBC`, `CTR` | `CBC`   | Encryption mode   |

### Software
```
make host
```
| Parameter    | Values              | Default | Description       |
|--------------|---------------------|---------|-------------------|
| GZIP_ENGINES | int 1 -- 4          | 1       | # of GZIP engines |
| AES_ENGINES  | int 1 -- 8          | 1       | # of AES engines  |


## Execute
```
./build.hw.CBC.1g.1a/host --input=<> --n_pages=<>
```
| Parameter    | Values              | Default      | Description               |
|--------------|---------------------|--------------|---------------------------|
| --input      | path to a file      | `input.txt`  | Payload file              |
| --n_pages    | int > 0             | 1            | # of pages inside payload |
| --profilling | path to a file      | `output.csv` | Profilling output file    |
| --emulator   |                     | false        | Run as emulation          |

## Benchmarks
