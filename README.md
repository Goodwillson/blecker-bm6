# BLEcker BM6
**Bluetooth low energy (BLE) tracker with BM6 car battery monitor support for ESP32**

This software is written for ESP32 boards to track BLE devices. It can be used for your smart home, scan BLE devices and send their presence to your smart home hub over MQTT. From version 1.04 webhook call is also possible.
This is a ready-to-use program, you don't need to modify the code (add your wifi, mqtt credentials whatever). Settings can be done on a nice web interface. BM6 version adds support for BM6 (Ancel BM200) car battery monitor. It reads voltage, temperature, power and adds it to MQTT advanced report for devices with MAC address starting with 50547B, which is BM6 vendor MAC range. Other devices are checked just for presence and no active connection is established to save battery. By default, only presence status of each device is reported. This software is based on Blecker 1.11 by Redakker https://github.com/redakker/blecker/. 

## What does it exactly
This is a very simple tracker software which creates an MQTT topic for each scanned device mac address (without ":" ) under the base topic and sends the availability as payload.
I personally use it for presence detection of family members. Every family member has a BLE device on her/his keyring and smart home can do automations depends on the members' availability. For example: turn on the alarm system if nobody at home.

The default topic is **/blecker-bm6**\
The default payload for available device: **present**\
The default payload for not available device: **not_present**
### Example

Device MAC is: `12:34:56:ab:cd:ef`
Send an MQTT message if available: 
```
topic: /blecker-bm6/123456abcdef
payload: present
```

Send an MQTT message if NOT available: 
```
topic: /blecker-bm6/123456abcdef
payload: not_present
```

The scan is running in every 2 seconds for 5 seconds and collects the devices into a list. In case of an available device, it sends an MQTT message as available. If the device is gone or can't be scanned after 120 seconds then a "not available" message will be sent out.

The administrator can define an observable device list in a web frontend. These devices will be uploaded to the inside device list. With this mechanism, the "not available" message can be sent out even if the device was not available right after the reboot.

### Explanation
If the device is not available after the system start, "not available" message will be never sent out just in case the BLE device becomes available and will be gone again.

### Status messages
-- System sends a detailed status message about the BLE device periodically: **/blecker-bm6/[device-mac]/status**\ --

-- System sends a detailed status message about the BLE device: **/blecker-bm6/status/[device-mac]**\ --
```
{
  "name": "",
  "rssi": "",
  "mac": "50547b123456",
  "presence": "home",
  "observed": "true",
  "volts": "12.270000",
  "temp": "29.000000",
  "power": "0.000000",
  "lastSeenMs": "12492"  
}
```

This message is coming together with the normal availability message.
The payload is a JSON object structure which contains detailed data like **name**, **rssi**, **observed**, **lastSeenMs**, etc. for more possibilities.
This function is off by default. It can be changed on a web administration UI.

### Format of BM6 message
BM6 device sends all data in hexadecimal string starting with d15507, which is then parsed to obtain specific values (eg. d15507001b011004800000000002ffff). So far known values are:

d15507 00  1b    01   10   0 480    0000000002ffff

          temp      power   volts
           27        16     11.52

### Webhook
This feature is available in and over version 1.04. It was implemented under ticket [#10](/../../issues/10).

In every device state change (available -> not available and not available -> available) ESP32 calls the configured webhook url. Configuration field is available on the web administration of the software.

Webhook can be configured for dinamic usage. Wildcards in the URL will be replaced.

Currently available wildcards in the URL:
 - {presence} : Presence string (available) / Presence string (not available) (See the details about presence string in the Web configuration section)
 - {device} : name of the device

Example URL:
 - http://192.168.1.1/?p={presence}&d={device}


### Home Assistant MQTT autodiscovery (MQTT Discovery)
Autodiscovery for Home Assistant is implemented with version 1.03. Idea was coming from [@leonardpitzu](https://github.com/leonardpitzu). Thanks!
Details: https://www.home-assistant.io/integrations/device_tracker.mqtt/#discovery-schema
On the web administration page autodiscovery can be set. (See the details in Web Configuration section)
In this case Home Assistant automatically can set up the observed deivces. To use this function "Observed devices" field must be set at least with one observed device. Observed devices appears in the state page (Developer tools / States)

Home Assistant and the BLEcker should connected to the same MQTT broker.

Discovery message is sent out every 60 seconds.


## Upload to ESP32
1. **Using VSCode and PlatformIO**
  * download the source and put it into a folder
  * install VSCode (https://code.visualstudio.com/download) and PlatformIO inside (https://platformio.org/install/ide?install=vscode)
  * open the folder of the source code in VSCode
  * connect your ESP32 to your computer using the USB cable
  * on the bottom of the window there should be a PlatformIO tool: Platform IO upload
  * wait while the code is being built and uploaded

2. **Use the ESPtool to upload the prebuilt binary**
  * download the binary here
  * read how to install and use esptool: https://github.com/espressif/esptool
  * upload the binary with something like this: esptool.py --chip esp32 image_info blecker.bin

## First steps
Upload and start the code on ESP32. If there is no configuration yet then it offers an access point. The name of the accesspoint can be found in this file: definitions.h It is "blecker" now.
* connect to this access point with your smartphone and call the IP address **192.168.4.1** for web administration.
You can set your WiFi and MQTT credentials on that administration page. See the details below.
* with the version 1.05 Captive portal is available. It means when you connect to the access point of the microcontroller then a notification pops up which brings you to the configuration page. It is easier to configure the device. Captive portal was tested in Android and iPhone devices.

Later you can find the web administration tool on the IP address which was set to the ESP32. Check it in your own router or WiFi manager tool.

Thanks for the mDNS support, you can find your board in the local area network if you call the URL: http://blecker-bm6

or chek the IP address of it with the following command in the commmand line: *nslookup blecker*

## Web configuration
Web configuration UI is available to change some parameters in the system. It can be reached in a browser. Call the IP address of the board. (See the network settings in your router or WiFi manager)
The following settings are available:
* WiFi name: your WiFi network name where the board should connect
* Password: password of your WiFi network
* MQTT server: your MQTT server address
* MQTT port: port of the MQTT connection
* Base topic: you can define a prefix for your messages. Example: /home/presence -> /home/presence/blecker/[device-mac] topic will be used
* Username: MQTT server username
* Password: MQTT server password
* Observed devices: you can define your own devices for more accuracy, see the reason above (explanation). Use the mac address without ":" and separate them with ";" Please do not use space characters and user lowercase letters
* Home Assistant Auto discovery: set to "Send" to enable this function
* Auto discovery prefix: should be the same what is configured in HA. Default is: homeassistant
* Reboot after (hours): device reboot this amount of hours (Value should be integer and greater than 0)
* Detailed report: default off. See the details in section **Status messages**

If you click to the advanced text, you can find more options
* Presence string (available): a custom payload to send an available state
* Presence string (not available) a custom payload to send a not available state
* Device status string (on) (default on): this string will be sent with the status report in case the device is becoming online
* Device status string (off) (default off): this string will be sent with the status report in case the device is becoming offline
* Device status retian: you can choose if the status message would be retained MQTT message
* Device ID: MQQT device identity string. Leave it empty for default. If this is not defined or empty, MQTT broker will set it as a random string.

## Update
There are two ways to update your board:
* build and upload a new code like the first time (Upload to ESP32)
* use web OTA. Web administration interface offers you an update mechanism. You can update your board with a new .bin update file. Browse the update file from your PC and press the upload button. Some minutes later the new firmware will run on your ESP32.

## Devices for usage
Tested boards:
 - ESP32-S (dev board)
 - ESP32-S2 (dev board)
 - espcam32 (Tested by [@ozett](https://github.com/ozett))
 - ESP32-D1-MINI
 - AZDelivery ESP-32 Dev Kit C V4 (Tested by [@maarten682](https://github.com/maarten682))

Possible suitable boards (not tested):
 - All ESP32 (BLE capable) board

Tested BLE beacons:
 - Nut Bluetooth Beacon https://www.amazon.com/Nut-F6-world-Smallest-Trackers/dp/B01B0WRC4I/
 - Long Range (500m) Bluetooth Beacon https://www.amazon.com/programmable-Battery-Bluetooth-eddystone-Technology/dp/B07PT9758D
 - Holyiot nRF52810 beacon (Tested by [@maarten682](https://github.com/maarten682))
 
Possible suitable beacons:
 - any BLE beacon which can provide a mac address

Please if you tested with any kind of boards/beacons and the test was successful, contact me and I'll update the list

## For developers

## Latest build
Under #55 a Github action has been implemented. This action creates a build after each commit to the master branch. With this, you can try the latest and the greatest version of BLEcker.
Go the the [Actions tab](https://github.com/redakker/blecker/actions) and find the latest successful build with Blecker build (PlatformIO CI). Open it and you can find the build artifact which can be used for updating your ESP32 board.

${{\color{red}\Huge{\textsf{  !!!\ Important\ !!!\ \}}}}\$

Be aware these builds could contain not stable solutions.

## Build the project
After some investigation, the project descriptors are made to make the development environment ready automatically in VSCode.
All you need to do is wait till VSCode reads the settings files with recommended plugins and then install them.
Before the build you need to have inside VSCode:

- PlatformIO installed
- Live server installed
- Python installed

Prebuild script(s) should install the dependecies automaticaly.

## Build the project (if the previous step is not working)
- Download the project from GitHub
- unzip to a folder
- install VS Code
- install PlatformIO IDE extension in it
- reload VSCode
- Open Platform IO
- Click to the Platforms tab
- Inside the Platforms tab click on the Embedded tab
- search for "Espressif 32"
- install it
- open the code folder (File -> Open folder)
- build the project

### Complicated solution develop the Web interface
HTML code in /html folder is built to the source code. It is done by PlatformIO build mechanism. (pre_build.py, pre_build_web.py)
Python removes the trailing spaces and compile into a PROGMEM variable.
To live edit the web UI make a symlink from /html to your webserver folder. If you modify the code then refresh your browser by F5. You should not change the HTML code in a webcontent.h file.

### Easier solution develop the Web interface
You can use the live server to edit the HTML on-the-fly.
Install this plugin:
https://marketplace.visualstudio.com/items?itemName=ritwickdey.LiveServer

./vscode/settings.json contains the configuration data for that.

## Debug
The code contains a lot of logs which send messages over the serial connection (for example in VS Code) and Bluetooth as well. Bluetooth Serial for Android is one of the apps which was tried in this way.
Each part of the code has a related log prefix, so it is easy to see which part of the code sends logs.

With https://redakker.github.io/blecker/ you can check the device logs too for debugging. Connect your device and in the modal click to "Logs & Console". You might click the "Reset device" to see the logs.

## Example for Home Assistant
Let's say you have a BLE beacon with this device id (mac address): `12:34:56:ab:cd:ef`
### Settings on ESP32
* Upload the code to your ESP32 and let it run.
* Call the ESP32 web interface its IP address (Web frontend should appear)
* Set the credentials of your WiFi and MQTT, let the base topic field empty for now
* Click to the 'advanced' text and set the presence strings to the following -> *home* | *not_home*
* put your BLE device address into the Observed devices input field without ":". In tis case: 123456abcdef
* Press the submit button (device will reboot)

### Settings in Home Assistant
* open your configuration file of  Home Assistant instance to configure the device tracker module. It is usually in the configuration.yaml file
* complete your device tracker configuration with the MQTT presence option. Details: https://www.home-assistant.io/integrations/device_tracker.mqtt/
* reboot/reload the HA
* At the end of the day you should have something like this

```
!!! deprecated in the newest HA !!!
device_tracker:
     - platform: mqtt
       devices:
         redakker: '/blecker/123456abcdef'
!!! deprecated in the newest HA !!!

mqtt:
  device_tracker:
  - name: "redakker"
    state_topic: "/blecker/123456abcdef"
  - name: "otheruser"
    state_topic: "/blecker/987654fedcba"
```
* after restarting HA you will find among states the presence of your BLE device with this name:  **device_tracker.redakker**

### Contributors

## Thank you for
- [@BalazsM](https://github.com/BalazsM)
- [@Goodwillson](https://github.com/Goodwillson)
- [@YogoGit](https://github.com/YogoGit)

And a big thanks and thumbs up to [@Seinuh](https://github.com/Seinuh) for beta tests!

### Release notes

## 1.01
- #2 has been solved: if you define observed devices in the web frontend then just that devices will be monitored and sent messages about their presence
- reboot timer introduced: it is actually a workaround. I experienced the ESP32 stuck after some days (network ping is okay, but web frontend and message sending are dead). The administrator can define a reboot time in hours (web frontend). After defined hours the ESP will reboot. It has no effect on presence detection.
The format is a single number (integer): "1" or "2". You cannot use float numbers

## 1.02
- #4 has been solved: hostname is propagated for the routers. Easier to determine the IP address in the router using the hostname "blecker"
- typo fixes

## 1.03
- #7 feature is implemented: Home assistant auto discovery
- typo fixes

## 1.04
- #10 feature is implemented: Webhook
- typo, minor issue fixes
- #14 fix

## 1.05
- #11 feature is implemented: Captive portal
- #18 issue is fixed, now MQTT tries to connect even if the username and password not defined. Somebody uses MQTT without user and password
- typo, minor issue fixes

## 1.06 (Breaking change)
Status message now is coming with the normal presence message (availability change). MQTT message topic is also changed

Status message topic from this version is /blecker/status/[device-mac]

## 1.07
- #23 version is compiled to the HTML source directly, user always see the current version and GIT revision
- binary file name changed. it does not affect the update process but contains the revision number as well
- build code cleaning, htmlmin, jsmin, cssmin do not need anymore for the build process

## 1.08
- #27 bugfix - still bug
- #29 MQTT reconnect bugfix
- #30 Added feature status string customization
- #31 Added feature custom client id

## 1.09
- #27 fix

## 1.10
- Logging bugfix and improvements (thanks to [@BalazsM](https://github.com/BalazsM))
- Webserver initialization cleanup (thanks to [@BalazsM](https://github.com/BalazsM))
- Some logging improvements for less dynamic string construction and destruction in runtime (thanks to [@BalazsM](https://github.com/BalazsM))
- Bugfixes and enhancements

## 1.11
- #37 MQTT last will fix (it was not sent before)
- #35 On admin page user can choose if the last will should be retained or not
- restart fix, disconnect wifi before restart
- #54 send periodic MQTT status message
- #52 Last will retain status cannot be set back to default
- #24 Display the chip info
- #57 Add unique hostname
- adjust scan time
- adjust status message (lastSeenMs is added)
- wifi reconnect changes (thanks to [@Goodwillson](https://github.com/Goodwillson)) https://github.com/redakker/blecker/pull/59
- run the BLE scanner on another core of the ESP32 CPU, it probably eliminates the bug #58 (thanks for the idea to [@dpnebert](https://github.com/dpnebert))
- #61 Make the beacon presence retain changeable

Not product logic-related
- #55 Create an action to create a new build from the master
- #56 Code restructure

## 1.12 (Not released yet)
- Merge pull request #66 from YogoGit/main



Buy me a coffee: https://www.buymeacoffee.com/redakker

