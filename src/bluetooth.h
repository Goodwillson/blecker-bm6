#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "definitions.h"
#include "utilities.h"
#include "BluetoothSerial.h" // Header File for Serial Bluetooth
#include "LinkedList.h"
#include "log.h"
#include "led.h"
#include "database.h"
#include <Callback.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h>
#include <vector>
#include <stdexcept>

struct BM6Data {
  float voltage;
  int temperature;
  int power;
};

class BlueTooth: public BLEAdvertisedDeviceCallbacks {

    Logger logger;
    Led* led;
    Signal<MQTTMessage>* mqttMessageSend;
    Signal<Device>* deviceChanged;
    NimBLEScan* pBLEScan;
    Database* database;

    BluetoothSerial blueToothSerial; // Object for Bluetooth
    String command;
    long lastRun;
    long lastClear;
    long lastSendDeviceData;    
    
    boolean sendAutoDiscovery;
    long lastSendAutoDiscovery;
    String autoDiscoveryPrefix;
    // This is not the best place here. This object should not know this, but autodiscover must use it.
    // You mut not use any other place in the object
    String mqttBaseTopic;

    boolean detailedReport;
    boolean monitorObservedOnly; // Monitor devices only which are configured in the database
    boolean networkConnected; // Connected to the network (Wifi STA)
    boolean mqttConnected; // Connected to MQTT server

    boolean beaconPresenceRetain;


    LinkedList<Device> devices;
    LinkedList<int> devicesToRemove;

    // For Bluetooth scan task
    TaskHandle_t scan_handle;
    SemaphoreHandle_t bluetoothMutex;

    //BM 6
    BM6Data bm6_data;
    uint8_t encryptedCommandBytes[16]; // Precomputed encrypted command
    long lastBM6Scan;
    TaskHandle_t bm6_task_handle; // Task handle for BM6 data task    
    const uint8_t key[16] = {108, 101, 97, 103, 101, 110, 100, 255, 254, 48, 49, 48, 48, 48, 48, 57}; // AES key (16 bytes for AES-128)

    public:
        BlueTooth(Log& rlog, Led& led);
        void setup(Database &database, Signal<MQTTMessage> &mqttMessageSend, Signal<Device> &deviceChanged);
        void loop();
        void setConnected(boolean connected);
        void setMqttConnected(boolean connected);
        std::string formatMACAddress(const std::string& mac);
    
    private: 
        void onResult(NimBLEAdvertisedDevice* advertisedDevice) override;
        void fillDevices(String devicesString);
        void handleDeviceChange(Device dev);  

        void precomputeEncryptedCommand();
        void getBM6Data(const char* address);
        void notificationHandler(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify);
        String decrypt(uint8_t* crypted, size_t length);
        void encrypt(uint8_t* plaintext, size_t length, uint8_t* outputBuffer);
        static void bm6TaskWrapper(void* parameter);
        static void bm6DataTask(void* parameter);
        static void bluetoothScanner(void* parameter);   
};

#endif
