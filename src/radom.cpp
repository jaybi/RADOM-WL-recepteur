//Importation des librairies
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <String.h>
//Installer les librairies Adafruit Unified Sensors by Adafruit
//et DHT sensor library by Adafruit
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
//installer la librairie DS3231 by Jean-Claude Wippler
#include <DS3231.h>
#include <Wire.h>
#include <PersonalData.h>
#include <EEPROM.h>

// Liste des fonctions
void readSMS(String message);
void sendMessage(String message);
void setConsigne(String message, int indexConsigne);
void heatingProg();
void turnOn() ;
void turnOnWithoutMessage() ;
void turnOff() ;
void turnOffWithoutMessage() ;
String getMeteo() ;
String getDate() ;
void sendStatus() ;
void i2c_eeprom_write_byte( int deviceaddress, unsigned int eeaddress, byte data );
byte i2c_eeprom_read_byte( int deviceaddress, unsigned int eeaddress );
void eepromWriteData(float value);
float eepromReadSavedConsigne();
int getBijunctionState();
float readDHT();

//Définition des pinouts
#define BIJUNCTION_PIN 3
enum {
  ENABLED = 1,
  DISABLED = 0
};
#define DHT_PIN 6 //Renseigne la pinouille connectée au DHT
SoftwareSerial gsm(10, 11); // Pins TX,RX du Arduino
#define RELAY_PIN 2 // Pin connectée au relai
#define LED_PIN 13
//pin 4,5 -> I2C DS3231

//Variables de texte
String textMessage;
int index = 0;

//Variable et constantes pour la gestion du DHT
#define DHTTYPE DHT22 // Remplir avec DHT11 ou DHT22 en fonction
DHT_Unified dht(DHT_PIN, DHTTYPE);
uint32_t delayMS;

//Variables pour la gestion du temps
DS3231 Clock;
bool Century=false;
bool h12;
bool PM;

//Variables de mémorisation d'état
int previousState = ENABLED; // Etat de présence précédent du secteur commun
int heating; // Variable d'état du relais (et du chauffage)
int program = DISABLED; // Programmation active ou non

//Programmation de la consigne de Programmation
#define hysteresis 1.0
float consigne;
float newConsigne = 1.0;
const float temperatureOffset = -3.0; /*Dépend des conditions extérieures.
Ici pour corriger l'issue #2 : la température mesurée était de 21° pour une
température réelle de 18°*/

//MODE DEBUG
//Permet d'afficher le mode débug dans la console
//Beaucoup plus d'infos apparaissent
#define DEBUG 0 // 0 pour désactivé et 1 pour activé

//Récupération des données privées qui ne sont pas uploadées dans GITHUB
PersonalData PersonalData; // Objet contenant les données sensibles
String phoneNumber = PersonalData.getPhoneNumber();
String pinNumber = PersonalData.getPinNumber();

/*SETUP************************************************************************/
void setup() {
  // Start the I2C interface
  Wire.begin();
  //Configuration des I/O
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BIJUNCTION_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // The current state of the RELAY_PIN is Off Passant à l'état repos (connecté en mode normalement fermé NC)
  heating = false;

  dht.begin();//Demarrage du DHT
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);

  //Set delay between sensor readings based on sensor details.
  delayMS = sensor.min_delay / 1000;

  Serial.begin(9600);//Demarrage Serial
  Serial.print("Connecting...");
  delay(5000);

  gsm.begin(9600);//Demarrage GSM

  Serial.println("Connected");

  gsm.print("AT+CREG?\r\n");
  delay(1000);
  gsm.print("AT+CMGF=1\r\n");
  delay(1000);
  gsm.println("AT+CNMI=2,2,0,0,0\r\n"); //This command selects the procedure
  delay(1000);                          //for message reception from the network.
  // gsm.println("AT+CMGD=4\r\n"); //Suppression des SMS
  // delay(1000);

  if(DEBUG) {// Test de la configuration du numéro de téléphone
    Serial.print("**DEBUG :: Phone number :");
    Serial.print(phoneNumber);
    Serial.println(".");
    Serial.print("**DEBUG :: Pin number :");
    Serial.print(pinNumber);
    Serial.println(".");
  }

  if(DEBUG) {// Test de la configuration
    Serial.print("**DEBUG :: DHT infos :");
    Serial.println(F("Temperature Sensor"));
    Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
    Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
    Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
    Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
    Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
    Serial.println(F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
  }

  consigne = eepromReadSavedConsigne(); //Récupération de la consigne enregistrée
  sendStatus(); //Envoie un SMS avec le statut
}

/*LOOP************************************************************************/
void loop() {
  if (gsm.available() > 0) {
    textMessage = gsm.readString();
    if (DEBUG) {
      Serial.println(textMessage);
    }
    //Cas nominal avec le numéro de tel par défaut
    if ( (textMessage.indexOf(phoneNumber)) < 10 && textMessage.indexOf(phoneNumber) > 0) { // SMS arrived
      readSMS(textMessage);
    }
    //Permet de prendre la main pour le controle du système
    else if (textMessage.indexOf(pinNumber) < 51 && textMessage.indexOf(pinNumber) > 0) {
      int indexOfPhoneNumber = textMessage.indexOf("+",5);
      int finalIndexOfPhoneNumber = textMessage.indexOf("\"", indexOfPhoneNumber);
      String newPhoneNumber = textMessage.substring(indexOfPhoneNumber,finalIndexOfPhoneNumber);
      String information = "Nouveau numero enregistre : ";
      information.concat(newPhoneNumber);
      sendMessage(information);
      phoneNumber=newPhoneNumber;
      if (DEBUG) {
        Serial.print("First index : ");
        Serial.println(indexOfPhoneNumber);
        Serial.print("Last index : ");
        Serial.println(finalIndexOfPhoneNumber);
        Serial.print("New Phone number : ");
        Serial.println(phoneNumber);
      }
      readSMS(textMessage);
    }
  }

  if (program == ENABLED) {
    heatingProg();
  }

  int bijunction = getBijunctionState();
  if ((bijunction == ENABLED) && (previousState == ENABLED)) { // si commun present et état précedent non présent
    if (heating == DISABLED) {
      digitalWrite(RELAY_PIN, LOW); // Relai passant
      previousState = DISABLED;
      if (DEBUG) {
        Serial.println("Marche forcée secteur commun activée");
      }
    }
  }
  if ((bijunction == DISABLED) && (previousState == DISABLED)) { // si plus de commun
    if (heating == DISABLED) { // et si pas de chauffage en cours
      digitalWrite(RELAY_PIN, HIGH); // Relai bloqué
      previousState = ENABLED;
      if (DEBUG) {
        Serial.println("Marche forcée secteur commun désactivée");
      }
    }
  }
  delay(100);
}

/*FUNCTIONS*******************************************************************/
void readSMS(String textMessage) {
  const char* commandList[] = {"Ron", "Roff", "Status", "Progon", "Progoff", "Consigne"};
  int command = -1;

  for(int i = 0; i<6; i++) {
    if (textMessage.indexOf(commandList[i]) > 0) {
      command = i;
      index = textMessage.indexOf(commandList[i]);
      break; //Permet de sortir du for des que le cas est validé
    }
  }
  switch (command) {
    case 0: // Ron
    turnOn();
    break;
    case 1: // Roff
    turnOff();
    break;
    case 2: // Status
    sendStatus();
    break;
    case 3: // Progon
    program = ENABLED;
    sendMessage("Programme actif");
    digitalWrite(LED_PIN, HIGH);
    break;
    case 4: // Progoff
    program = DISABLED;
    sendMessage("Programme inactif");
    digitalWrite(LED_PIN, LOW);
    turnOff();
    break;
    case 5: // Consigne
    setConsigne(textMessage, index);
    break;
    default:
    break;
  }
}

void sendMessage(String message) {//Envoi du "Message" par sms
gsm.print("AT+CMGS=\"");
gsm.print(phoneNumber);
gsm.println("\"");
delay(500);
gsm.print(message);
gsm.write( 0x1a ); //Permet l'envoi du sms
}

void setConsigne(String message, int indexConsigne) {//Réglage de la consigne contenue dans le message à l'indexConsigne
newConsigne = message.substring(indexConsigne + 9, message.length()).toFloat(); // On extrait la valeur et on la cast en float // 9 = "Consigne ".length()
Serial.print("nouvelle consigne :");
Serial.println(newConsigne);
if (!newConsigne) {// Gestion de l'erreur de lecture et remontée du bug
if (DEBUG) {
  Serial.println("Impossible d'effectuer la conversion de la température String -> Float. Mauvais mot-clé? Mauvais index?");
  Serial.print("indexConsigne = ");
  Serial.println(indexConsigne);
  Serial.print("consigne lenght (>0)= ");
  Serial.println(message.length()- indexConsigne + 9);
  Serial.print("newConsigne = ");
  Serial.println(newConsigne);
} else {
  sendMessage("Erreur de lecture de la consigne envoyee");
}
} else if (consigne != newConsigne) { //Si tout se passe bien et la consigne est différente la consigne actuelle
  consigne = newConsigne;
  message = "La nouvelle consigne est de ";
  message.concat(consigne);
  sendMessage(message);
  eepromWriteData(consigne);//Enregistrement dans l'EEPROM
} else {
  sendMessage("Cette consigne est deja enregistree");
}
}

void heatingProg(){//Vérification de le temp, comparaison avec la consigne, et activation/désactivation en fonction
  float temperature = readDHT() + temperatureOffset ; //lecture du DHT afin de mettre à jour les variables de météo
  if ((temperature < (consigne - 0.5*hysteresis)) && (heating == DISABLED)) {
    turnOnWithoutMessage();
  }
  if ((temperature > (consigne + 0.5*hysteresis)) && (heating == ENABLED)) {
    turnOffWithoutMessage();
  }
}

void turnOn() {//allumage du radiateur si pas de consigne et envoi de SMS
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  if (program) {
    gsm.println("Le programme est toujours actif !!");
  } else {
    // Turn on RELAY_PIN and save current state
    gsm.println("Chauffage en marche.");
    digitalWrite(RELAY_PIN, LOW);
    heating = ENABLED;
  }
  gsm.write( 0x1a ); //Permet l'envoi du sms
}

void turnOnWithoutMessage() {//allumage du radiateur si pas de consigne
  // Turn on RELAY_PIN and save current state
  digitalWrite(RELAY_PIN, LOW);
  heating = ENABLED;
}

void turnOff() {//Extinction du rad si pas de consigne et envoie de SMS
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  if (program) {
    gsm.println("Le programme est toujours actif !!");
  } else {
    // Turn off RELAY_PIN and save current state
    gsm.println("Le chauffage est eteint.");
    digitalWrite(RELAY_PIN, HIGH);
    heating = DISABLED;
  } //Emet une alerte si le programme est toujours actif
  gsm.write( 0x1a ); //Permet l'envoi du sms
  previousState = ENABLED;
}

void turnOffWithoutMessage() {//Extinction du rad si pas de consigne
  // Turn on RELAY_PIN and save current state
  digitalWrite(RELAY_PIN, HIGH);
  heating = DISABLED;
  previousState = ENABLED;
}

float readDHT() { // Se connecte au DHT et renvoie la temperature
  delay(delayMS);
  sensors_event_t event;
  float temperature = 100.00;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println(F("Error reading temperature!"));
  } else {
    temperature = event.temperature;
  }
  // Check if any reads failed and exit early (to try again).
  if (DEBUG) {
    Serial.print("**DEBUG :: readDHT()\t");
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature);
    Serial.print(F("*C"));
    Serial.print(" Consigne: ");
    Serial.print(consigne);
    Serial.println(" *C ");
  }
  return temperature;
}

String getMeteo() { //Renvoie un string contenant le message météo
  String meteo = "";
  float t = readDHT() + temperatureOffset;
  if (t != 100) {
    meteo += "Temp: ";
    meteo += t;
    meteo += " *C";
    return meteo;
  } else {
    return "Error reading DHT";
  }
  // meteo += "Hyg: ";
  // meteo += h;
  // meteo += " % ";
}

//Renvoie la date
String getDate() {
  //Ce code concatène dans "date" la date et l'heure courante
  //dans le format 20YY MM DD HH:MM:SS
  String date ="";
  date +="2";
  date +="0";
  date += String(Clock.getYear());
  date += " ";
  date += String(Clock.getMonth(Century));
  date += " ";
  date += String(Clock.getDate());
  date += " ";
  date += String(Clock.getHour(h12, PM));
  date += ":";
  date += String(Clock.getMinute());
  date += ":";
  date += String(Clock.getSecond());

  //Ce code est exécuté si la variable DEBUG est a TRUE
  //Permet d'afficher dans la console la date et l'heure au format
  // YYYY MM DD w HH MM SS 24h
  if (DEBUG) {
    Serial.print("**DEBUG :: getDate()\t");
    Serial.print("2");
    if (Century) {      // Won't need this for 89 years.
    Serial.print("1");
  } else {
    Serial.print("0");
  }
  Serial.print(Clock.getYear(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getMonth(Century), DEC);
  Serial.print(' ');
  Serial.print(Clock.getDate(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getDoW(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getHour(h12, PM), DEC);
  Serial.print(' ');
  Serial.print(Clock.getMinute(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getSecond(), DEC);
  if (h12) {
    if (PM) {
      Serial.print(" PM ");
    } else {
      Serial.print(" AM ");
    }
  } else {
    Serial.println(" 24h ");
  }
}
return date;
}

//Envoie par SMS le statut
void sendStatus() {
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  gsm.print("Le chauffage est actuellement ");
  gsm.println(heating ? "ON" : "OFF"); // This is to show if the light is currently switched on or off
  gsm.println(getMeteo());
  gsm.println(getDate());
  gsm.print("Consigne: ");
  gsm.println(consigne);
  gsm.write( 0x1a );
}

//Ecrtiture par byte dans l'EEPROM
void i2c_eeprom_write_byte( int deviceaddress, unsigned int eeaddress, byte data ) {
  int rdata = data;
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8)); // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.write(rdata);
  Wire.endTransmission();
}

//Lecture par Byte dans l'EEPROM
byte i2c_eeprom_read_byte( int deviceaddress, unsigned int eeaddress ) {
  byte rdata = 0xFF;
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8)); // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.endTransmission();
  Wire.requestFrom(deviceaddress,1);
  if (Wire.available()) rdata = Wire.read();
  return rdata;
}

//Ecriture de la value dans l'EEPROM
void eepromWriteData(float value) {
  String stringValue = String(value);
  int valueLength = sizeof(stringValue);
  if (valueLength < 32) { //A priori il existe une limite, voir dans AT24C32_examples
    if (0) {// ATTENTION: génère une erreur quand actif
      Serial.print("**DEBUG :: eepromWriteData()\t");
      Serial.print("Longueur de la consigne : ");
      Serial.println(valueLength-1);
      Serial.print("Valeur de la consigne : ");
      Serial.println(stringValue);
    }
    for (int i = 0; i < valueLength - 1; i++) { // -1 pour ne pas récupérer le \n de fin de string
      i2c_eeprom_write_byte(0x57, i, stringValue[i]);
      delay(10);
    }
  }
}

//Renvoie la valeur de la consigne lue dans l'EEPROM
float eepromReadSavedConsigne() {
  int b;
  String value;
  for(int i=0;i<5;i++) // la valeur sera "normalement" toujours 5 pour une consigne
  {
    b = i2c_eeprom_read_byte(0x57, i); //access an address from the memory
    value += char(b);
  }
  if (0) {
    Serial.print("**DEBUG :: eepromReadSavedConsigne()");
    Serial.print("\tRead value: "); //ATTENTION : le 18/5/19 ce message provoquait une erreur quand il était plus long
    Serial.println(value);
  }
  return value.toFloat();
}

int getBijunctionState() {
  return (digitalRead(BIJUNCTION_PIN) ? DISABLED : ENABLED);
  //si le détecteur renvoie 1 (non présent), la fonction renvoie DISABLED (return 0)
  //Si le détecteur renvoie 0 (présent), la fonction renvoie ENABLED (return 1)
}
