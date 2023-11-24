#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEAddress.h>
#include <BLE2902.h>
#include <ESP32Servo.h>
#include <Preferences.h>

#define SERVICE_UUID "f69317b5-a6b2-4cf4-89e6-9c7d98be8891"
#define CHARACTERISTIC_UUID "2ec829c3-efad-4ba2-8ce1-bad71b1040f7"
#define PUMP_STATUS_CHARACTERISTIC_UUID "1cd909de-3a8e-43e1-a492-82917ab0b662"

#define uS_TO_S_FACTOR 1000000  // Fator de conversão de micro segundos para segundos
#define TIME_TO_SLEEP  15

#define STATIC_PIN 123456

#define SERVO_PIN 33
#define LED_BUILTIN 2

Preferences preferences;

Servo servo;

BLEServer* pServer = nullptr;
BLECharacteristic* pPumpStatusCharacteristic = nullptr;
BLECharacteristic* pInsulinCharacteristic = nullptr;

uint32_t status = 0;

 int pos;

TaskHandle_t statusTask;

void sendPumpStatus(void*);
void setupBluethoothAuth();

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        Serial.println("Conectado!");
        BLEDevice::startAdvertising();
    };

    void onDisconnect(BLEServer* pServer) {
        Serial.println("desconectado!");
    }
};

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        if (pPumpStatusCharacteristic->getValue() == "0") {
            Serial.println("Bloqueio bomba");
            pPumpStatusCharacteristic->setValue("1");
            pPumpStatusCharacteristic->notify();

            const int value = std::floor(std::stoi(pCharacteristic->getValue()) * 2.5);

            Serial.print("Recebido: ");
            Serial.println(value);

            if ((value + pos) < 125) {
                Serial.println("aplicando insulina");
                int i = pos;
                for (; i <= value; ++i) {
                    digitalWrite(LED_BUILTIN, HIGH);
                    servo.write(i);
                    ++pos;
                    delay(900);
                    digitalWrite(LED_BUILTIN, LOW);
                    delay(100);
                }
                preferences.putUInt("pos", pos);

                pPumpStatusCharacteristic->setValue("0");
                pPumpStatusCharacteristic->notify();
                Serial.println("bomba liberada");
            } else {
                Serial.println("Quantidade de insulina maior que o estoque");
                pPumpStatusCharacteristic->setValue("2");
                pPumpStatusCharacteristic->notify();
            }
        } else {
            Serial.println("Bomba ocupada");
        }
        delay(1000);
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Configurando bluethooth...");

    BLEDevice::init("FreeFlowInsulinPump-esp32-v2");
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService* pService = pServer->createService(SERVICE_UUID);

    pInsulinCharacteristic = pService->createCharacteristic(
            CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pInsulinCharacteristic->setCallbacks(new MyCallbacks());
    pInsulinCharacteristic->addDescriptor(new BLE2902());

    pPumpStatusCharacteristic = pService->createCharacteristic(
            PUMP_STATUS_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    pService->start();

    BLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    pSecurity->setCapability(ESP_IO_CAP_OUT);
    pSecurity->setStaticPIN(STATIC_PIN);

    pPumpStatusCharacteristic->setValue("0");
    pPumpStatusCharacteristic->notify();

    xTaskCreatePinnedToCore(
            sendPumpStatus,    /* Task function. */
            "send pup status", /* name of task. */
            10000,             /* Stack size of task */
            nullptr,              /* parameter of the task */
            1,                 /* priority of the task */
            &statusTask,            /* Task handle to keep track of created task */
            0);                /* pin task to core 0 */
    delay(500);

    preferences.begin("freeFlow", false);
    pos = preferences.getUInt("pos", 0);

    Serial.println("Configurando servo motor...");
    servo.setPeriodHertz(50);  // Standard 50hz servo
    servo.attach(SERVO_PIN);
    servo.write(pos);

    Serial.printf("Bomba iniciada na posicao: %d\n", pos);

    pinMode(LED_BUILTIN, OUTPUT);

    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

    delay(10000);
}

void sendPumpStatus(void*) {
    for (;;) {
        pPumpStatusCharacteristic->notify();
        delay(1000);
    }
}

void loop() {
    if (pServer->getConnectedCount() == 0) {
        Serial.println("Desligando...");
        esp_deep_sleep_start();
    }
}