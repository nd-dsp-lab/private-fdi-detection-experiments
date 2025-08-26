Number of inputs read per second depending on device count.

For some reason, 1000 devices sometimes produced higher speeds in SGX, but all other numbers of devices consistently produce lower speeds in SGX.

| devices | native (rps) | sgx (rps) |
| --- | --- | --- |
| 200 | 197.49 | 191.46 |
| 1000 | 170.29 | 178.27 |
| 5000 | 66.45 | 50.25 |
