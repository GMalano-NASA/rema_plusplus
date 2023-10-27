# REMA Remote Terminal Unit  
Firmware for **REMA** Heat Exchanger Inspection Robot  

## Requirements  
arm-none-eabi  
CMake  
OpenOCD ver > 0.12  
Doxygen  

## Getting Started  
Create a directory  

```bash
mkdir REMA
cd REMA
```  

Clone this repos  
```bash
git clone https://github.com/GMalano-NASA/lpc_chip_43xx.git  
git clone https://github.com/GMalano-NASA/CIAA_NXP_board.git  
git clone https://github.com/GMalano-NASA/rema_plusplus.git
```
Configure the project  
```bash
cd rema_plusplus
cmake -S . -B ./build
```

Compile the project  
```bash
cmake --build ./build
```

Flash the microcontroller  
```bash
cmake --build ./build --target flash
```

Change project settings  
```bash
ccmake ./build
```

Clean the project  
```bash
cmake --build ./build --target clean
```
In order to generate documentation for the project, you need to configure the build
to use Doxygen. This is easily done, by modifying the workflow shown above as follows:

```bash
ccmake ./build
cmake --build ./build --target doxygen-docs
```

> ***Note:*** *This will generate a `docs/` directory in the **project's root directory**.*
