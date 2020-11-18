#!/bin/bash

rm lte_poc_v*.tar.gz
rm -rf lte_poc_v*

DD=$(date +"%Y%m%d")
TT=$(date +"%H%M%S")
NAME="lte_poc_v${DD}_${TT}"

mkdir $NAME

# copy built files
cp ./build/lte_poc.bin ./$NAME/
mkdir ./$NAME/partition_table
cp ./build/partition_table/partition-table.bin ./$NAME/partition_table/
mkdir ./$NAME/bootloader
cp ./build/bootloader/bootloader.bin ./$NAME/bootloader/
cp ./build/flash_project_args ./$NAME/

# copy ELF file as well to be able to analyse crash logs later
# Inspection command example:
# $ xtensa-esp32-elf-addr2line -pfiaC -e aip_hub.elf <addr>
cp ./build/lte_poc.elf ./$NAME/

echo "python \$IDF_PATH/components/esptool_py/esptool/esptool.py --chip esp32 --baud 115200 --before default_reset --after hard_reset write_flash -z @flash_project_args" > ./$NAME/flash.sh
echo "python \$IDF_PATH/components/esptool_py/esptool/esptool.py erase_flash" > ./$NAME/erase_flash.sh
chmod +x ./$NAME/flash.sh
chmod +x ./$NAME/erase_flash.sh
tar -czpvf $NAME.tar.gz $NAME/
