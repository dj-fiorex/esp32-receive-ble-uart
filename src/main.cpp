#include <Arduino.h>
#include "BLEDevice.h"

static const char *LOG_TAG = "RECEIVER";

// The remote Nordic UART service service we wish to connect to.
// This service exposes two characteristics: one for transmitting and one for receiving (as seen from the client).
static BLEUUID serviceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

// The characteristics of the above service we are interested in.
// The client can send data to the server by writing to this characteristic.
static BLEUUID charUUID_RX("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // RX Characteristic

// If the client has enabled notifications for this characteristic,
// the server can send data to the client as notifications.
static BLEUUID charUUID_TX("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // TX Characteristic

static boolean doConnect = false;
static boolean connected = false;
static BLERemoteCharacteristic *pTXCharacteristic;
static BLERemoteCharacteristic *pRXCharacteristic;
static BLEAdvertisedDevice *myDevice;

static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
  Serial.write(pData, length);
}

class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
#ifdef CFG_DEBUG
    Serial.println("MyClientCallback::onConnect");
#endif
  }

  void onDisconnect(BLEClient *pclient)
  {
    connected = false;
#ifdef CFG_DEBUG
    Serial.println("MyClientCallback::onDisconnect");
#endif
  }
};

#ifdef CFG_DEBUG

static void my_gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
  ESP_LOGW(LOG_TAG, "custom gattc event handler, event: %d", (uint8_t)event);
  if (event == ESP_GATTC_DISCONNECT_EVT)
  {
    Serial.print("Disconnect reason: ");
    Serial.println((int)param->disconnect.reason);
  }
}

static void my_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gatts_cb_param_t *param)
{
  ESP_LOGI(LOG_TAG, "custom gatts event handler, event: %d", (uint8_t)event);
}

static void my_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  ESP_LOGI(LOG_TAG, "custom gap event handler, event: %d", (uint8_t)event);
}

#endif

bool connectToServer()
{
#ifdef CFG_DEBUG
  Serial.print("Establishing a connection to device address: ");
  Serial.println(myDevice->getAddress().toString().c_str());
#endif

  BLEClient *pClient = BLEDevice::createClient();

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  auto connectionOk = pClient->connect(myDevice);
  if (!connectionOk)
  {
    return false;
  }

  // Obtain a reference to the Nordic UART service on the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    pClient->disconnect();
    return false;
  }

  // Obtain a reference to the TX characteristic of the Nordic UART service on the remote BLE server.
  pTXCharacteristic = pRemoteService->getCharacteristic(charUUID_TX);
  if (pTXCharacteristic == nullptr)
  {
    pClient->disconnect();
    return false;
  }

#ifdef CFG_DEBUG
  Serial.println(" - Remote BLE TX characteristic reference established");

  // Read the value of the TX characteristic.
  std::string value = pTXCharacteristic->readValue();
  Serial.print("The characteristic value is currently: ");
  Serial.println(value.c_str());
#endif

  pTXCharacteristic->registerForNotify(notifyCallback);

  // Obtain a reference to the RX characteristic of the Nordic UART service on the remote BLE server.
  pRXCharacteristic = pRemoteService->getCharacteristic(charUUID_RX);
  if (pRXCharacteristic == nullptr)
  {
    return false;
  }
#ifdef CFG_DEBUG

  Serial.println(" - Remote BLE RX characteristic reference established");
  // Write to the the RX characteristic.
  String helloValue = "Hello Remote Server";
  pRXCharacteristic->writeValue(helloValue.c_str(), helloValue.length());
#endif

  connected = true;
  return true;
}

/**
   Scan for BLE servers and find the first one that advertises the Nordic UART service.
*/
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  /**
      Called for each advertising BLE server.
  */
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
#ifdef CFG_DEBUG
    Serial.print("BLE Advertised Device found - ");
    Serial.println(advertisedDevice.toString().c_str());
#endif

    // We have found a device, check to see if it contains the Nordic UART service.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(serviceUUID))
    {
#ifdef CFG_DEBUG
      Serial.println("Found a device with the desired ServiceUUID!");
#endif

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;

    } // Found our server
  }   // onResult
};    // MyAdvertisedDeviceCallbacks

/*
 * Retrieve a Scanner and set the callback we want to use to be informed when we
 * have detected a new device. Specify that we want active scanning and start the
 * scan to run for 30 seconds.
 */
void scanForBLEServer()
{
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(2000);
  pBLEScan->setWindow(1500);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30, false);
}

void setup()
{
  Serial.begin(115200);
#ifdef CFG_DEBUG
  Serial.println("Starting Haptica BLE Central Receiver based on Nordic UART Service");
#endif

#ifdef CFG_DEBUG
  BLEDevice::setCustomGapHandler(my_gap_event_handler);
  BLEDevice::setCustomGattsHandler(my_gatts_event_handler);
  BLEDevice::setCustomGattcHandler(my_gattc_event_handler);
#endif

  BLEDevice::init("Haptica Receiver");

  scanForBLEServer();
} // End of setup.

void loop()
{

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true)
  {
    if (connectToServer())
    {
#ifdef CFG_DEBUG
      Serial.println("We are now connected to the BLE Server.");
#endif
      doConnect = false;
    }
    else
    {
#ifdef CFG_DEBUG
      Serial.println("We have failed to connect to the server; we will retry.");
#endif
      doConnect = false;
      scanForBLEServer();
    }
  }
  if (!connected)
  {
    scanForBLEServer();
  }
} // End of loop
