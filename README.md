# Keya-Soft-Box




## Tested build for: 
[ESP32-S3-DevKitM](https://docs.zephyrproject.org/latest/boards/espressif/esp32s3_devkitm/doc/index.html)
[ESP32-C3-DevKitM](https://docs.zephyrproject.org/latest/boards/espressif/esp32c3_devkitm/doc/index.html)
[XIAO ESP32C3](https://docs.zephyrproject.org/latest/boards/seeed/xiao_esp32c3/doc/index.html)


## Install Zephyr 
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## Init the workspace 
* open KSB.code-workspace *
```
cd .. 
west init 
west update 

# If you have issues with esptool when building
pip3 install --user --upgrade esptool>=5.0.2 --break-system-packages
export PATH=$HOME/.local/bin:$PATH
esptool.py version
```

