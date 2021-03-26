#include <SPI.h>
#include <MFRC522.h>
#include "Adafruit_VL53L0X.h"
#include <OrangeForRN2483.h>
#define RST_PIN         9           // Configurable, see typical pin layout above
#define SS_PIN          10          // Configurable, see typical pin layout above
MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

#define DHTPIN A8     // what pin we're connected to

// The following keys are for structure purpose only. You must define YOUR OWN.
const uint8_t appEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
const uint8_t appKey[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

boolean systemStatus = false;
int defaultRange = -1;
int triggerTemp = 29;
boolean oneLora = true;

//*****************************************************************************************//
void setup() {

  // RFID INIT --------------------------------------------------------------------------------------
  SerialUSB.begin(9600);                                           // Initialize serial communications with the PC
  SPI.begin();                                                  // Init SPI bus
  mfrc522.PCD_Init();                                              // Init MFRC522 card
  SerialUSB.println(F("Read personal data on a MIFARE PICC:"));    //shows in serial that it is ready to read

  // Laser INIT --------------------------------------------------------------------------------------
  SerialUSB.println("Adafruit VL53L0X test");
  if (!lox.begin()) {
    SerialUSB.println(F("Failed to boot VL53L0X"));
    while (1);
  }
  // power
  SerialUSB.println(F("VL53L0X API Simple Ranging example\n\n"));

  // Temperature sensor INIT --------------------------------------------------------------------------------------
  //define temperature pin as input
  pinMode(TEMP_SENSOR, INPUT);
  OrangeForRN2483.init();
}

bool joinNetwork()
{
  OrangeForRN2483.setDataRate(DATA_RATE_1); // Set DataRate to SF11/125Khz
  return OrangeForRN2483.joinNetwork(appEUI, appKey);
}

//*****************************************************************************************//
void loop() {
  // SerialUSB.println("Starting...");
  boolean rfid = true;
  // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  byte block;
  byte len;
  //-------------------------------------------
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

    SerialUSB.println(F("**Card Detected:**"));
    byte buffer1[18];
    block = 4;
    len = 18;
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 4, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
      SerialUSB.print(F("Authentication failed: "));
      SerialUSB.println(mfrc522.GetStatusCodeName(status));
      return;
    }
    status = mfrc522.MIFARE_Read(block, buffer1, &len);
    if (status != MFRC522::STATUS_OK) {
      SerialUSB.print(F("Reading failed: "));
      SerialUSB.println(mfrc522.GetStatusCodeName(status));
      return;
    }

    String value = "";
    for (uint8_t i = 0; i < 16; i++)
    {
      value += (char)buffer1[i];
    }
    value.trim();
    SerialUSB.print(value);
    SerialUSB.println(F("\n**End Reading**\n"));

    // Vérification de l'encodage du badge
    if (value.equals("00000000002483")) {
      // Active ou désactive le système
      SerialUSB.println("Valid key");
      systemStatus = !systemStatus;

    } else {
      SerialUSB.println("Invalid key");
    }
  }
  if (systemStatus) {
    // SerialUSB.println("System status : ON");
    // On écoute le laser
    if (defaultRange == -1) {
      initLaser();
    } else {
      checkSignal();
    }
  }
  delay(1000);
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // Alerte incendie
  //10mV per C, 0C is 500mV
  float mVolts = (float)analogRead(TEMP_SENSOR) * 3300.0 / 1023.0;
  float temp = (mVolts - 500.0) / 10.0;

  SerialUSB.print(temp);
  SerialUSB.println(" C");
  if (temp >= triggerTemp) {
    // On envoie une alerte incendie
    alertFire();
    SerialUSB.println("Alerte incendie");
  }
  delay(1000);

}

void initLaser() {
  VL53L0X_RangingMeasurementData_t measure;

  SerialUSB.println("Reading a measurement... ");
  lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!
  if (measure.RangeStatus != 4) {  // phase failures have incorrect data
    SerialUSB.println("Distance (mm): ");
    SerialUSB.println(measure.RangeMilliMeter);

    // On set la valeur par défaut
    if (defaultRange == -1) {
      defaultRange = measure.RangeMilliMeter;
      SerialUSB.print("defaultRange : ");
      SerialUSB.print(defaultRange);
      SerialUSB.println(" - Saved!");
    } else {
      SerialUSB.println(" out of range ");
    }
    delay(1000);
  }
}

void checkSignal() {
  VL53L0X_RangingMeasurementData_t measure;

  SerialUSB.println("Reading a measurement... ");
  lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!
  if (measure.RangeStatus != 4) {  // phase failures have incorrect data
    SerialUSB.println("Distance (mm): ");
    SerialUSB.println(measure.RangeMilliMeter);
    if (measure.RangeMilliMeter < (defaultRange - 15)) {
      oneLora = alertIntrusion();
      if (!oneLora) SerialUSB.println("error on send alert");
    }
  }
}


bool alertIntrusion() {
  const uint8_t size = 16;
  uint8_t port = 5;
  uint8_t data[size] = { 0x61, 0x6c, 0x65, 0x72, 0x74, 0x65, 0x20, 0x69, 0x6e, 0x74, 0x72, 0x75, 0x73, 0x69, 0x6f, 0x6e }; // alerte intrusion
  bool res = joinNetwork();
  if (res) {
    SerialUSB.println("Join Success");
    OrangeForRN2483.enableAdr();
    return OrangeForRN2483.sendMessage(data, size, port); // send unconfirmed message
  }
  return false;
}

bool alertFire() {
  const uint8_t size = 16;
  uint8_t port = 5;
  uint8_t data[size] = { 0x61, 0x6c, 0x65, 0x72, 0x74, 0x65, 0x20, 0x69, 0x6e, 0x63, 0x65, 0x6e, 0x64, 0x69, 0x65 }; // alerte incendie
  bool res = joinNetwork();
  if (res) {
    SerialUSB.println("Join Success");
    OrangeForRN2483.enableAdr();
    return OrangeForRN2483.sendMessage(data, size, port); // send unconfirmed message
  }
  return false;
}
