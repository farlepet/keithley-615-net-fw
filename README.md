Keithley 615 Network Interface Board FW
=======================================

This repository contains the FW that is designed to run on the (WIP) custom network
interface board that can be found in https://github.com/farlepet/keithley-615-printer-boards

More details to come as work progresses.

Building
--------

TODO: Add details on setting up Zephyr

Source zephyr script:
```bash
source <zephyr dir>/zephyr-env.sh
```

Build project
```bash
west build -p auto -b board-stm32g0b1re .
```
