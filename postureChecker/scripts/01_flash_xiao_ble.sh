source 00_prepare.sh

# flash
cp ../build/zephyr/zephyr.uf2 $XIAO_BLE_UF2_FOLDER

#upload memfault symbols
if [ "$MEMFAULT_PASSWORD" != "__REPLACE_ME__" ]; then
    memfault --org $MEMFAULT_ORG --project $MEMFAULT_PROJECT --email $MEMFAULT_EMAIL --password $MEMFAULT_PASSWORD upload-mcu-symbols ../build/zephyr/zephyr.elf
else
    echo Please configure your Memfault credentials to upload the symbols.
fi