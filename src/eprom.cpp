#include <Arduino.h>
#include <EEPROM.h> //Lib AT24C32
#include <String.h>
#include <Wire.h>
#include <DS3231.h>

#include "eprom.h"
#include "radom.h"

//Variable de DEBUG
extern const bool DEBUG;

void initWire()
{
  Wire.begin();
}

//Ecrtiture par byte dans l'EEPROM
void i2c_eeprom_write_byte(int deviceaddress, unsigned int eeaddress, byte data)
{
  int rdata = data;
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.write(rdata);
  Wire.endTransmission();
}

//Lecture par Byte dans l'EEPROM
byte i2c_eeprom_read_byte(int deviceaddress, unsigned int eeaddress)
{
  byte rdata = 0xFF;
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.endTransmission();
  Wire.requestFrom(deviceaddress, 1);
  if (Wire.available())
    rdata = Wire.read();
  return rdata;
}

//Ecriture de la value dans l'EEPROM
void eepromWriteData(float value)
{
  String stringValue = String(value);
  int valueLength = sizeof(stringValue);
  if (valueLength < 32)
  { //A priori il existe une limite, voir dans AT24C32_examples
    if (0)
    { // ATTENTION: génère une erreur quand actif
      Serial.print("**DEBUG :: eepromWriteData()\t");
      Serial.print("Longueur de la consigne : ");
      Serial.println(valueLength - 1);
      Serial.print("Valeur de la consigne : ");
      Serial.println(stringValue);
    }
    for (int i = 0; i < valueLength - 1; i++)
    { // -1 pour ne pas récupérer le \n de fin de string
      i2c_eeprom_write_byte(0x57, i, stringValue[i]);
      delay(10);
    }
  }
}

//Renvoie la valeur de la consigne lue dans l'EEPROM
float eepromReadSavedConsigne()
{
  String value;
  for (int i = 0; i < 5; i++) // la valeur sera "normalement" toujours 5 pour une consigne
  {
    int b = i2c_eeprom_read_byte(0x57, i); //access an address from the memory
    value += char(b);
  }
  if (DEBUG)
  {
    Serial.print("**DEBUG :: eepromReadSavedConsigne()");
    Serial.print("\tRead value: "); //ATTENTION : le 18/5/19 ce message provoquait une erreur quand il était plus long
    Serial.println(value);
  }
  return value.toFloat();
}
