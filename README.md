# M5-AtomMatrix-Companion-v4-Satellite
A single-button Companion v4 satellite built for the M5 Atom Matrix. Includes 5x5 LED-matrix icons, external RGB LED output, WiFi configuration portal, OTA updates, and automatic MAC-based device ID. Designed for tally, triggers, and broadcast or live-event control surfaces.

## Features
- Uses the M5 Atom Matrix (ESP32)
- 5x5 matrix icon system for Boot, WiFi Setup, Waiting for Companion, Ready and Error states
- External RGB LED control on G33 (Red), G22 (Green), G19 (Blue) with G23 Ground
- External LED mirrors Companion key colour with brightness scaling
- WiFiManager configuration portal (hold button for 5 seconds)
- OTA firmware updates via ArduinoOTA
- Auto-generated deviceID “M5ATOM_xxxxxxxxxxxx” based on full MAC address
- Companion API support: KEY-STATE, COLOR, BRIGHTNESS, TEXT, PING, DEVICE-ADD
- Codebase aligned with the AtomS3 and AtomS3R versions for consistent behaviour
- Ready for release on GitHub and M5Burner

## Hardware Connections
Pin  Function  
G33  External LED Red  
G22  External LED Green  
G19  External LED Blue  
G23  External LED Ground  

Use a common-cathode RGB LED with G23 as ground.

## Installation and Usage
1. Clone the repository.  
2. Open the M5AtomMatrix_Companionv4_SingleButtonSatellite.ino file in Arduino IDE.  
3. Select the correct board: M5 Atom (ESP32).  
4. Install libraries: M5Atom, WiFiManager, Preferences, ArduinoOTA.  
5. Upload firmware to the device.  
6. On first boot the LED matrix shows the boot sequence, then WiFi setup or Companion-wait status.  
7. In Companion v4: add the device under Surfaces and select the shown deviceID.  
8. Press the button to trigger Companion key events.  
9. Hold button for 5 seconds to open the WiFi config portal (SSID equals deviceID).  

## OTA Firmware Update
OTA updates are enabled.  
Use the deviceID as the upload hostname.  
Default OTA password: companion-satellite

## Troubleshooting
Matrix LEDs blank: confirm Companion is sending COLOR or BITMAP data.  
External LED not working: verify wiring and that the LED is common-cathode.  
DeviceID shows zeros: clear Preferences or reboot to reinitialise MAC.  
Cannot connect to Companion: check host IP and port in WiFi portal settings.  
Brightness mismatches: ensure Companion is sending BRIGHTNESS commands.

## Version
v1.2.0

## License
MIT License
