# KeyaSoftbox
This project is an open-source RGB LED softbox controlled via an ESP32. It creates a WiFi hotspot, allowing users to set colors and brightness from a simple web interface.

## WiFi-Controlled RGB Softbox
This project is an open-source RGB LED softbox controlled via an ESP32. It creates a WiFi hotspot, allowing users to set colors and brightness from a simple web interface.

## Features
* ESP32-based WiFi control
* RGB LED strip with customizable colors
* Powered by a 18650 battery with TP4056 charger
* Web interface for easy control


## Hardware
* ESP32
* RGB LED strip (WS2812 or similar)
* 18650 Li-ion battery
* TP4056 charging module
* 5V step-up converter (if needed)

![Schematic_KeyaSoftbox_2025-03-07](https://github.com/user-attachments/assets/44b183ed-8898-4d14-a6af-8821a819be53)


## Housing 
made in shapr3d and 3d Printed

![image](https://github.com/user-attachments/assets/de884f51-e5bb-4d59-80f5-c997be9d9305)
![image](https://github.com/user-attachments/assets/ea596406-5163-4396-8b1b-741b6d90ebff)
![image](https://github.com/user-attachments/assets/f4377492-7796-4047-adfb-ade0fa8e10aa)
![image](https://github.com/user-attachments/assets/dbaa8488-fde8-49cf-987b-ab4e2b5aac71)


## Installation
* Flash the provided code to your ESP32.
* Connect to the "Softbox-LED" WiFi network (password: 12345678).
* Open a browser and go to http://192.168.4.1/color?r=255&g=0&b=0 to set red color (adjust values as needed).

## License
This project is open-source under the MIT License.
