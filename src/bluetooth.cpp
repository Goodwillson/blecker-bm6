#include "bluetooth.h"

BlueTooth::BlueTooth(Log& rlog, Led& led) : logger(rlog, "[BLUE]") {

    lastRun = 0;
    lastClear = 0;
    lastSendDeviceData = 0;        
    sendAutoDiscovery = false;
    lastSendAutoDiscovery = 0;
    autoDiscoveryPrefix = "";
    beaconPresenceRetain = MQTT_BEACON_PRESENCE_DEFAULT_RETAIN;
    // This is not the best place here. This object should not know this, but autodiscover must use it.
    // You mut not use any other place in the object
    mqttBaseTopic = "";
    detailedReport = false;
    monitorObservedOnly = false; // Monitor devices only which are configured in the database
    networkConnected = false; // Connected to the network (Wifi STA)
    devices = LinkedList<Device>();
    devicesToRemove = LinkedList<int>();

    boolean mqttConnected = false; // Connected to MQTT server
    this -> led = &led;
}

void BlueTooth::setup(Database &database, Signal<MQTTMessage> &mqttMessageSend, Signal<Device> &deviceChanged) {

    this -> mqttMessageSend = &mqttMessageSend;
    this -> deviceChanged = &deviceChanged;
    this -> database = &database;

    logger << "Initializing Bluetooth";
    
    NimBLEDevice::init(BOARD_NAME);
    this->pBLEScan = NimBLEDevice::getScan(); // create new scan            
    pBLEScan->setAdvertisedDeviceCallbacks(this);
    // pBLEScan->setActiveScan(true); // active scan uses more power, but gets results faster
    pBLEScan->setActiveScan(false); // set to passive mode
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // less than or equal to setInterval value

    this -> beaconPresenceRetain = this -> database -> getValueAsBoolean(String(DB_BEACON_PRESENCE_RETAIN), false, MQTT_BEACON_PRESENCE_DEFAULT_RETAIN);
    logger << "Beacon presence retain is set to " << (String) this -> beaconPresenceRetain;

    logger << "Bluetooth is initialized with name:" << BOARD_NAME;

    // Prefill the device list with the user's devices
    // In case of accidently reboot it will send a "not_home" message if the device is gone meanwhile
    // Example:
    // Device is available -> system sends a "home" MQTT message as retained
    // Device is phisically gone (owner go to work) and before the microcontorller sends the "not_home" message microcontroller reboots (power outage)
    // MQTT retained message will be never changed and device stays at home in the MQTT system.
    // Solution:
    // Owner upload the device names which must be observed, so we prefill the list and on the next scan will set them as gone if the story happens above

    // Devices in this format: 317234b9d2d0;15172f81accc;d0e003795c50            
    fillDevices(database.getValueAsString(DB_DEVICES));
    // Parse the sting, split by ;

    // Auto discovery for Home Assistant is available.
    // Set it tre if the user enabled it
    sendAutoDiscovery = (database.getValueAsInt(DB_HA_AUTODISCOVERY) > 0) ? true : false;
    if (sendAutoDiscovery) {
        autoDiscoveryPrefix = database.getValueAsString(DB_HA_AUTODISCOVERY_PREFIX);
    }

    // This is not the best place here. This object should not know this, but autodiscover must use it.
    // You mut not use any other place in the object
    this -> mqttBaseTopic = this -> database -> getValueAsString(String(DB_MQTT_TOPIC_PREFIX), false) + MQTT_TOPIC;

    detailedReport = (database.getValueAsInt(DB_DETAILED_REPORT) > 0) ? true : false;

    precomputeEncryptedCommand(); // Precompute the encrypted command

    // Create the semaphore
    bluetoothMutex = xSemaphoreCreateMutex();
    if (bluetoothMutex == NULL) {
        logger << "Failed to create bluetoothMutex";
        return; // Exit if semaphore creation failed
    }
    logger << "bluetoothMutex created successfully";

    // Run the BLE scanner on another core as a separated task
    if (xTaskCreatePinnedToCore(bluetoothScanner,       // Method name
                                "BLE Scan Task",        // Only for humans for debug
                                1024*2,                 // How many bytes should be alloted.
                                this,                   // Pass in variable reference here (or NULL)
                                8,                      // Priority of task
                                &scan_handle,           // Reference to Task handle.  Ex: to delete the scan task, it would look like: "vTaskDelete(scan_handle);"
                                0) != pdPASS) {
        logger << "Failed to create bluetoothScanner task";
    } else {
        logger << "bluetoothScanner task created successfully";
    }
    
    // Run the BM6 data retrieval task
    if (xTaskCreatePinnedToCore(bm6DataTask, // Function to implement the task
                                "BM6 Data Task", // Name of the task
                                4096, // Stack size in bytes
                                this, // Parameter to pass to the task
                                1, // Task priority
                                &bm6_task_handle, // Task handle
                                0) != pdPASS) {
        logger << "Failed to create bm6DataTask task";
    } else {
        logger << "bm6DataTask task created successfully";
    }    
}

void BlueTooth::loop() {

    // Find the expired devices
    for (int i = 0; i < this -> devices.size(); i++) {
        
        Device dev = devices.get(i);
        
        if (millis() - dev.lastSeen > BT_DEVICE_TIMEOUT || (long) millis() - (long) dev.lastSeen < 0) {

            // Give another chance to the device to appear (Device has DEVICE_DROP_OUT_COUNT lives in the beginning)
            dev.mark--;
            dev.lastSeen = millis();

            if (dev.mark == 0) {
                logger << "Device is gone. MAC: " << dev.mac;
                
                // Virtually remove the device
                dev.available = false;
                dev.rssi = "0";
                devices.set(i, dev);

                // Send an MQTT message about this device is NOT at home
                handleDeviceChange(dev);

            } 
            if (dev.mark > 0) {
                logger << "Device marked as gone. MAC: " << dev.mac << " Current mark is: " << (String)dev.mark;
                devices.set(i, dev);
            }
        } 
    }
    
    if ((millis() - lastClear > BT_LIST_REBUILD_INTERVAL && devices.size() > 0) || (long) millis() - (long) lastClear < 0) {
        lastClear = millis();
        logger << "Clear the device list. (This is normal operation. :))";
        // Clear the list, it will be rebuilt again. Resend the (available) status should not be a problem.
        devices.clear();
        fillDevices(this-> database -> getValueAsString(DB_DEVICES));
        mqttMessageSend->fire(MQTTMessage{"selfclean", "true", false});
    }
 
    if (sendAutoDiscovery) {
        if ((millis() - lastSendAutoDiscovery > HA_AUTODISCOVERY_INTERVAL && devices.size() > 0) || (long) millis() - (long) lastSendAutoDiscovery < 0) {
            lastSendAutoDiscovery = millis();
            logger << "Send autodicovery data.";
            for (int i = 0; i < this -> devices.size(); i++) {
                Device dev = devices.get(i);
                // Example
                // mosquitto_pub -h 127.0.0.1 -t home-assistant/device_tracker/a4567d663eaf/config -m '{"state_topic": "a4567d663eaf/state", "name": "My Tracker", "payload_home": "home", "payload_not_home": "not_home"}'

                if (dev.mac != NULL) {
                    String payload = "{\"state_topic\": \"" + mqttBaseTopic + "/" + dev.mac + "\", \"name\": \"" + dev.mac + "\", \"payload_home\": \"" + 
                    this -> database->getValueAsString(DB_PRECENCE) + "\", \"payload_not_home\": \"" + 
                    this -> database->getValueAsString(DB_NO_PRECENCE) + "\", \"source_type\": \"bluetooth_le\"}";

                    MQTTMessage autoDiscMessage = MQTTMessage{autoDiscoveryPrefix + "/device_tracker/" + dev.mac + "/config", payload, false, true};
                    mqttMessageSend->fire(autoDiscMessage);
                }
            }
        }
    }

}

void BlueTooth::setConnected(boolean connected) {
    this -> networkConnected = connected;
}

void BlueTooth::setMqttConnected(boolean connected) {
    this -> mqttConnected = connected;
}
        
void BlueTooth::onResult(NimBLEAdvertisedDevice* advertisedDevice) {

    boolean newFound = true;
    String deviceMac = advertisedDevice->getAddress().toString().c_str();
    deviceMac.toLowerCase();
    String deviceName = advertisedDevice->getName().c_str();
    String deviceRSSI = (String) advertisedDevice->getRSSI();
    deviceMac.replace(":","");

    for (int i = 0; i < this -> devices.size(); i++) {
        Device dev = devices.get(i);
        if (deviceMac == dev.mac) {

            dev.available = true;
            handleDeviceChange(dev);
            dev.lastSeen = millis();
            dev.mark = DEVICE_DROP_OUT_COUNT;
            
            devices.set(i, dev);                   
            newFound = false;
            
        }
    }

    if (!monitorObservedOnly) {
        if (newFound) {
            Device dev = {deviceName, deviceRSSI, deviceMac, true, NAN, NAN, NAN, millis(), DEVICE_DROP_OUT_COUNT, false };
            devices.add(dev);
            logger << "New device found. MAC: " << deviceMac;
            // Send an MQTT message about this device is at home
            handleDeviceChange(dev);
        }
    }
}

void BlueTooth::fillDevices(String devicesString) {

    if (devicesString.length() == 0) { return; }

    this->monitorObservedOnly = true;
    char *devicesChar = new char[devicesString.length() + 1];
    strcpy(devicesChar, devicesString.c_str());
    String devMac = "";
    while ((devMac = strtok_r(devicesChar, PARSE_CHAR, &devicesChar)) != NULL) { // delimiter is the semicolon
        if (devMac.length() > 0) {
            devMac.toLowerCase();
            Device device = {
                "", // name
                "", // rssi
                devMac, // mac
                false, // available
                NAN, // volts
                NAN, // temp
                NAN, // power
                millis(), // lastSeen
                DEVICE_DROP_OUT_COUNT, // mark
                true //observed
            };
            this -> devices.add(device);
            logger << "Device added as observed device. MAC: " << devMac;
        }
    }

    delete [] devicesChar;
}

void BlueTooth::handleDeviceChange(Device dev) {
    mqttMessageSend->fire(MQTTMessage{dev.mac, database -> getPresentString(dev.available), beaconPresenceRetain});
    // TODO: need to refactor, send only one message for the consumers
    // This will call the webhook
    deviceChanged->fire(dev);

    if (detailedReport) {
        String payload = "{\"name\":\"" + ((dev.name == NULL) ? "" : dev.name) + "\", \"rssi\":\"" + ((dev.rssi == NULL) ? "" : dev.rssi) + "\", \"mac\":\"" + ((dev.mac == NULL) ? "" : dev.mac) + "\", \"presence\":\"" + database -> getPresentString(dev.available) + "\", \"observed\":\"" + ((dev.observed) ? "true" : "false") + "\", \"volts\":\"" + (std::isnan(dev.volts) ? "N/A" : std::to_string(dev.volts).c_str()) + "\", \"temp\":\"" + (std::isnan(dev.temp) ? "N/A" : std::to_string(dev.temp).c_str()) + "\", \"power\":\"" + (std::isnan(dev.power) ? "N/A" : std::to_string(dev.power).c_str()) + "\", \"lastSeenMs\":\"" + (millis() - dev.lastSeen) + "\"}";
        MQTTMessage message = MQTTMessage{ String ("status/" + dev.mac), payload, beaconPresenceRetain };
        mqttMessageSend->fire(message);
    }
}


// ------------------------------- BM6 -----------------------------------------

void BlueTooth::bm6TaskWrapper(void* parameter) {
    BlueTooth* bt = static_cast<BlueTooth*>(parameter);
    for (int i = 0; i < bt->devices.size(); i++) {
        Device dev = bt->devices.get(i);
        bt->getBM6Data(dev.mac.c_str());
        delay(1000); // Small delay between each device poll
    }
    
    // Delete the task once done
    vTaskDelete(nullptr);
}

void BlueTooth::precomputeEncryptedCommand() {
    // The d15507 command tells the BM6 to start sending voltage/temp notifications
    uint8_t command[] = {0xd1, 0x55, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    // Encrypt the command once and store it in the global encryptedCommandBytes array
    encrypt(command, sizeof(command), encryptedCommandBytes);
    logger << "Encrypted command precomputed.";
}

void BlueTooth::getBM6Data(const char* address) {
    logger << "Starting BM6 data retrieval...";
    bm6_data.voltage = 0;
    bm6_data.temperature = 0;
    bm6_data.power = 0;

    NimBLEClient* client = nullptr;
    const int maxRetries = 3;
    int attempt = 0;
    bool connected = false;

    try {
        
        client = NimBLEDevice::createClient();
        std::string formattedMAC = this->formatMACAddress(address);
        NimBLEAddress bleAddress(formattedMAC);

        while (attempt < maxRetries && !connected) {
            logger << "Attempting to connect to BLE device at address: " << formattedMAC.c_str() << ", attempt " << std::to_string(attempt + 1).c_str();
            
            connected = client->connect(bleAddress);
            if (connected) {
                logger << "Connected to BLE device.";
                break;
            } else {
                logger << "Failed to connect to the BLE device. Retrying...";
                attempt++;
            }

            delay(1000);  // Wait a bit before retrying
        }

        if (!connected) {
            logger << "Failed to connect to the BLE device after " << std::to_string(maxRetries).c_str() << " attempts.";
            NimBLEDevice::deleteClient(client);
            return;
        }

        NimBLERemoteService* service = client->getService("FFF0");
        if (service == nullptr) {
            logger << "Failed to find the service with UUID FFF0.";
            client->disconnect();
            NimBLEDevice::deleteClient(client);
            return;
        }

        NimBLERemoteCharacteristic* charFF3 = service->getCharacteristic("FFF3");
        NimBLERemoteCharacteristic* charFF4 = service->getCharacteristic("FFF4");
        if (charFF3 == nullptr || charFF4 == nullptr) {
            logger << "Failed to find the characteristics with UUID FFF3 or FFF4.";
            client->disconnect();
            NimBLEDevice::deleteClient(client);
            return;
        }

        // Write the precomputed encrypted command to the characteristic
        charFF3->writeValue(encryptedCommandBytes, sizeof(encryptedCommandBytes), true);
        logger << "Sent encrypted command to start sending notifications.";

        // Register for notifications using the subscribe method
        if (charFF4->canNotify()) {
            bool subscribeResult = charFF4->subscribe(true, [this](BLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
                this->notificationHandler(characteristic, data, length, isNotify);
            });

            if (subscribeResult) {
                logger << "Subscribed for notifications successfully.";
            } else {
                logger << "Failed to subscribe for notifications.";
                client->disconnect();
                NimBLEDevice::deleteClient(client);
                return;
            }
        } else {
            logger << "Characteristic does not support notifications.";
            client->disconnect();
            NimBLEDevice::deleteClient(client);
            return;
        }

        // Wait for data
        unsigned long startTime = millis();
        while (bm6_data.voltage == 0 && bm6_data.temperature == 0) {
            if (millis() - startTime > 10000) {
                logger << "Timeout: No data received.";
                client->disconnect();
                NimBLEDevice::deleteClient(client);
                return;
            }
            delay(100);
        }
        
        logger << "Voltage: " << std::to_string(bm6_data.voltage).c_str();
        logger << "Temp: " << std::to_string(bm6_data.temperature).c_str();
        logger << "Power: " << std::to_string(bm6_data.power).c_str();

        String strAddress = String(address);

        for (int i = 0; i < this -> devices.size(); i++) {
            Device dev = devices.get(i);
            if (strAddress.equalsIgnoreCase(dev.mac)) {
                dev.available = true;            
                dev.lastSeen = millis();
                dev.volts =  bm6_data.voltage;
                dev.temp =  bm6_data.temperature;
                dev.power = bm6_data.power;      
                devices.set(i, dev);
                logger << "Updated device battery data: " << dev.mac;
            }
        }

        // Unsubscribe from notifications before disconnecting
        if (charFF4->canNotify()) {
            bool unsubscribeResult = charFF4->unsubscribe();
            if (unsubscribeResult) {
                logger << "Unsubscribed from notifications.";
            } else {
                logger << "Failed to unsubscribe from notifications.";
            }
            delay(1000);  // Wait for the unsubscribe operation to complete
        }

    } catch (const std::exception& e) {
        logger << "Exception: " << e.what();
        if (client) {
            client->disconnect();
            NimBLEDevice::deleteClient(client);
        }
        return;
    } catch (...) {
        logger << "An unknown error occurred.";
        if (client) {
            client->disconnect();
            NimBLEDevice::deleteClient(client);
        }
        return;
    }

    if (client) {
        if (client->isConnected()) {
            client->disconnect();
        }
        NimBLEDevice::deleteClient(client);
        logger << "Disconnected from BLE device.";
        delay(2000); // Wait for the deinit and disconnect to complete
    }
}

void BlueTooth::notificationHandler(BLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
    if (data == NULL || length == 0) {
        logger << "Received NULL data in notification.";
        return;
    }

    String message = decrypt(data, length);
    logger << "Message received: " << message;

    if (message.startsWith("d15507")) {
        bm6_data.voltage = strtol(message.substring(15, 18).c_str(), NULL, 16) / 100.0;
        bm6_data.temperature = strtol(message.substring(8, 10).c_str(), NULL, 16);
        bm6_data.power = strtol(message.substring(12, 14).c_str(), NULL, 16);
    }
}

String BlueTooth::decrypt(uint8_t* crypted, size_t length) {
    if (length != 16) {
        logger <<  "Error: Decrypt function received incorrect length data.";
        return "";
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 128); // Set key for decryption

    uint8_t decrypted[16]; // Buffer for decrypted data
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, crypted, decrypted);

    mbedtls_aes_free(&aes); // Clean up

    // Convert decrypted data to a hex string
    String decryptedHex = "";
    for (int i = 0; i < 16; i++) {
        if (decrypted[i] < 0x10) {
            decryptedHex += '0';
        }
        decryptedHex += String(decrypted[i], HEX);
    }

    return decryptedHex;
}

void BlueTooth::encrypt(uint8_t* plaintext, size_t length, uint8_t* outputBuffer) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128); // Set key for encryption

    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plaintext, outputBuffer);
    mbedtls_aes_free(&aes); // Clean up
}

void BlueTooth::bm6DataTask(void* parameter) {
    BlueTooth* bt = static_cast<BlueTooth*>(parameter);
    bt->logger << "bm6DataTask started";

    if (bt == nullptr) {
        bt->logger << "bm6DataTask - Error: bt is nullptr";
        vTaskDelete(nullptr);
    }

    for (;;) {
        if (xSemaphoreTake(bt->bluetoothMutex, portMAX_DELAY) == pdTRUE) {
            //bt->logger << "bluetoothMutex taken by bm6DataTask";
            for (int i = 0; i < bt->devices.size(); i++) {
                Device dev = bt->devices.get(i);
                if (dev.available) {
                    String macPrefix = dev.mac.substring(0, 6);
                    if (macPrefix.equalsIgnoreCase(BM6_VENDOR_MAC)) {
                        bt->getBM6Data(dev.mac.c_str());
                        delay(1000); // Small delay between each device poll
                    }
                    else {
                        bt->logger << "Device is not BM6: " << dev.mac;
                    }
                }
            }
            xSemaphoreGive(bt->bluetoothMutex);
            //bt->logger << "bluetoothMutex given by bm6DataTask";
        } else {
            //bt->logger << "bm6DataTask failed to take bluetoothMutex";
        }
        // Wait for x minutes before running again
        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MINUTES * 60000));
    }
}

void BlueTooth::bluetoothScanner(void* parameter) {
    BlueTooth* bt = static_cast<BlueTooth*>(parameter);
    bt->logger << "bluetoothScanner started";

    if (bt == nullptr) {
        bt->logger << "bluetoothScanner - Error: bt is nullptr";
        vTaskDelete(nullptr);
    }

    NimBLEScan* pBLEScan = bt->pBLEScan;
    if (pBLEScan == nullptr) {
        bt->logger << "bluetoothScanner - Error: pBLEScan is nullptr";
        vTaskDelete(nullptr);
    }

    for (;;) {
        if (xSemaphoreTake(bt->bluetoothMutex, portMAX_DELAY) == pdTRUE) {
            //bt->logger << "bluetoothMutex taken by bluetoothScanner";
            pBLEScan->start(BT_DEFAULT_SCAN_DURATION_IN_SECONDS, false);
            xSemaphoreGive(bt->bluetoothMutex);
            //bt->logger << "bluetoothMutex given by bluetoothScanner";
        } else {
            //bt->logger << "bluetoothScanner failed to take bluetoothMutex";
        }

        // Delay for 2 seconds
        vTaskDelay(2000 / portTICK_PERIOD_MS);

        pBLEScan->clearResults(); // Clear scan results to free memory
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

std::string BlueTooth::formatMACAddress(const std::string& mac) {
    std::string formattedMAC;
    for (size_t i = 0; i < mac.length(); i++) {
        formattedMAC += mac[i];
        if (i % 2 == 1 && i != mac.length() - 1) {
            formattedMAC += ":";
        }
    }
    return formattedMAC;
}

