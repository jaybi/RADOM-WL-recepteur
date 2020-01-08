//Importation des librairies
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <String.h>
#include <Wire.h>
#include <PersonalData.h>
#include <VirtualWire.h> //Lib for wireless
#include <DS3231.h>

#include "radom.h"
#include "gsm.h"
#include "eprom.h"

//Enumerations
enum {
  ENABLED = 1,
  DISABLED = 0
};
enum {
  INDIVIDUAL = 0,
  COMMON = 1
};

//Définition des pinouts
#define RELAYS_PERSO 2 // Pin connectée au relai
#define RELAYS_COMMON 3 // Pin de commandes des relais du commun
#define RX_PIN 6 //Renseigne la pinouille connectée au module radio 433MHz
#define BIJUNCTION_PIN 4
#define LED_PIN 13
//pin 4,5 -> I2C DS3231
SoftwareSerial gsm(10, 11); // Pins TX,RX du Arduino

//Variable de DEBUG
extern const bool DEBUG;

//Variables de texte
String textMessage = "";
int index = 0;

//Variables pour la gestion du temps
bool Century=false;
bool h12;
bool PM;
//Pour le comptage du temps
unsigned long lastTempMeasureMillis = 0;
int lastRefresh = 0;
#define THERMOSTAT_LISTENING_TIME 200 // En millisecondes
#define ELEC_NETWORK_SWITCHING_TIME 200

//Variables de mémorisation d'état
bool currentState = DISABLED; // Etat de présence précédent du secteur commun
bool heating; // Variable d'état du relais (et du chauffage)
bool program = DISABLED; // Programmation active ou non
bool currentProgramState = DISABLED;
bool alertNoSignalSent = false;
bool alertBatteryLowSent = false;
bool alertBatteryCriticalSent = false;
bool forcedHeating = false;
bool currentBijunctionState = DISABLED; // Etat de présence précédent du secteur commun
bool previousTempState;
int currentSource = COMMON;

//Programmation de la consigne de Programmation
#define hysteresis 1.0
float consigne;
float newConsigne = 1.0;
//const float temperatureOffset = 0.0; // pour corriger un éventuel offset de temps
float temperature = 33.3; // température par défaut
int batteryLevel = 101;
struct ThermostatData{
  float temp;
  int batt;
};

//Objet donnant accès aux données de temps
DS3231 Clock;

//Structure de données contenant un float et un int
ThermostatData receivedData;

//Variable pour le wireless
byte messageSize = sizeof(ThermostatData);

//Récupération des données privées qui ne sont pas uploadées dans GITHUB
PersonalData personalData; // Objet contenant les données sensibles
String phoneNumber = personalData.getPhoneNumber();
String pinNumber = personalData.getPinNumber();

/*SETUP************************************************************************/
void setup() {
  //Configuration des I/O
  pinMode(RELAYS_PERSO, OUTPUT);
  pinMode(RELAYS_COMMON, OUTPUT);
  pinMode(BIJUNCTION_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAYS_PERSO, LOW); // The current state of the RELAYS_PERSO is off NO, donc ne laisse pas passser
  digitalWrite(RELAYS_COMMON, LOW); // NC donc laisse passer au repos

  //Demarrage Serial
  Serial.begin(9600);
  
  //Initialisation de la bibliothèque VirtualWire
  vw_set_rx_pin(RX_PIN);
  vw_set_tx_pin(7);
  vw_set_ptt_pin(8);
  vw_setup(2000);
  vw_rx_start(); // On peut maintenant recevoir des messages

  //Start the I2C interface
  initWire();

  //Init the GSM
  initGSM(gsm);

  if(DEBUG) {// Test de la configuration du numéro de téléphone
    Serial.print("**DEBUG :: Phone number :");
    Serial.print(phoneNumber);
    Serial.println(".");
    Serial.print("**DEBUG :: Pin number :");
    Serial.print(pinNumber);
    Serial.println(".");
  }

  consigne = eepromReadSavedConsigne(); //Récupération de la consigne enregistrée

  sendStatus(); //Envoie un SMS avec le statut
}

/*LOOP************************************************************************/
void loop() {
  //Recevoir et traiter les sms
  receiveSMS(gsm);
  
  //Fonction de chauffage
  heatingProcess();

  //Vérifier le ping timeout, le niveau de batterie, envoie des alertes.
  checkThermometer();
}

/*FUNCTIONS*******************************************************************/



//Déconnecte du réseau commun, puis connecte sur l'individuel
void switchToIndividual(){
  if (currentSource != INDIVIDUAL) 
  {
    digitalWrite(RELAYS_COMMON, HIGH); // ouvrir le commun qui est NC à LOW
    delay(200);
    digitalWrite(RELAYS_PERSO, HIGH); // fermer le perso
    currentSource = INDIVIDUAL;
  }
}

//Déconnecte du réseau individuel, puis connecte le réseau commun
void switchToCommon() {
  if (currentSource != COMMON) 
  {
    digitalWrite(RELAYS_PERSO, LOW); // ouvrir le perso, qui est NO à LOW
    delay(200);
    digitalWrite(RELAYS_COMMON, LOW); // Connecter le commun
    currentSource = COMMON;
  }
}

//Fonction qui gère les conditions de chauffe du
void heatingProcess() {
  int bijunction = getBijunctionState();

 //Pour activer
  if (bijunction && !currentBijunctionState ) 
  {
    currentBijunctionState = ENABLED;
    switchToCommon();
  } 
  else if (!bijunction && program && !currentProgramState)
  {
    currentProgramState = ENABLED; 
  } 
  else if (!bijunction && forcedHeating && !heating && !program)
  {
    switchToIndividual();
    heating = ENABLED;
  }
  if (!bijunction && currentProgramState) 
  {
        heatingProg();
  }

  //Bascule si pendant une marche forcée, le commun devient présent puis absent
  if (!bijunction && currentBijunctionState)
  {
    if (forcedHeating)
    {
      switchToIndividual();
    }
    currentBijunctionState = DISABLED;
  }
  //Pour désactiver dans le cas d'un programme qui s'arrete
  if (!program && currentProgramState)
  {
    switchToCommon();
    currentProgramState = DISABLED;
    heating = DISABLED;
    if (forcedHeating){ //Supprime le flag forcedHeating lors de l'arret de la programmation
      forcedHeating = DISABLED; 
      heating = DISABLED;
    }
  } 
  //Pour désactiver une marche forcée
  if (!forcedHeating && heating){
    heating = DISABLED;
    switchToCommon();
  }
}

//Analyse la temperature actuelle et la compare à la consigne, demande la chauffe en cas de besoin
void heatingProg(){//Vérification de le temp, comparaison avec la consigne, et activation/désactivation en fonction
  if ((temperature < (consigne - 0.5*hysteresis))) {
    switchToIndividual();
  }
  if ((temperature > (consigne + 0.5*hysteresis))) {
    //Désactiver le chauffage par le perso
    switchToCommon();
  }
}

//Met à jour le compteur de temps depuis le dernier contact avec le thermometre, envoie les aletres en cas de batterie faible
void checkThermometer() 
{
  //Attente paquet du thermostat, timout 8000 ms
  listen(THERMOSTAT_LISTENING_TIME);

  // Mise à jour du chrono
  lastRefresh = (int)((millis() - lastTempMeasureMillis) / 60000);

  /*Permet d'envoyer un unique message (jusqu'au redémarrage) si le signal n'a pas
    été reçu depuis 30min*/
    if (!alertNoSignalSent) {
      if (lastRefresh > 30) {// si plus mis à jour depuis 30min désactivation de la prog
        program = DISABLED; // Coupe la programmation pour empécher de rester en chauffe indéfiniement
        digitalWrite(LED_PIN, LOW); //Extinction de la led
        alertNoSignalSent = true; // Flag de message parti, ce flag empeche de pouvoir relancer un programme. Il faut redémarrer l'appareil
        sendMessage(gsm, "Plus de signal du thermostat.");
      }
    }
  /*Permet d'envoyer un unique message jusqu'au redémarrage; si le niveau de batterie
  du termostat passs en dessous de 20°C*/
    if(!alertBatteryLowSent) {
      if (batteryLevel < 20) {
        alertBatteryLowSent = true;
        sendMessage(gsm, "Niveau de batterie faible.");
      }
    }
  /*Permet d'envoyer un unique message jusqu'au redémarrage; si le niveau de batterie
    du termostat passs en dessous de 10 °C*/
    if (!alertBatteryCriticalSent) {
      if (batteryLevel < 10) {
        alertBatteryCriticalSent = true;
        sendMessage(gsm, "Niveau de batterie critique.");
      }
    }
}

//Ecoute pendant le TIMEOUT si on reçoit une trame du thermometre, puis enregistre les valeurs si trame reçue
void listen(int timeout) 
{
  vw_wait_rx_max(timeout);
   // On copie le message, qu'il soit corrompu ou non
   if (vw_have_message()) {//Si un message est pret a etre lu
     if (vw_get_message((byte *) &receivedData, &messageSize)) { // Si non corrpompu
       lastTempMeasureMillis = millis();
       temperature = receivedData.temp;
       batteryLevel = receivedData.batt;
       if (DEBUG) {
         Serial.print("Temp transmise : ");
         Serial.println(temperature); // Affiche le message
         Serial.print("Batt transmise : ");
         Serial.println(batteryLevel);
         Serial.println();
       }
     }
     else if (DEBUG) {
       Serial.println("Message du thermostat corrompu.");
     }
   }
 }

//Traite le contenu du sms et effectue les actions en fonctions
void readSMS(String textMessage) 
{
  const char* commandList[] = {"Ron", "Roff", "Status", "Progon", "Progoff", "Consigne"};
  int command = -1;
  for(int i = 0; i<6; i++) 
  {
    if (textMessage.indexOf(commandList[i]) > 0) 
    {
      command = i;
      index = textMessage.indexOf(commandList[i]);
      break; //Permet de sortir du for des que le cas est validé
    }
  }
  switch (command) 
  {
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
      if (!alertNoSignalSent) //Empeche de relancer une programmation quand le thermometre n'a plus de batterie ou ne répond plus
      {
        program = ENABLED;
        sendMessage(gsm, "Programme actif");
        digitalWrite(LED_PIN, HIGH);
      }
      break;
    case 4: // Progoff
      program = DISABLED;
      sendMessage(gsm, "Programme inactif");
      digitalWrite(LED_PIN, LOW);
      break;
    case 5: // Consigne
      setConsigne(textMessage, index);
      break;
    default:
      break;
  }
}

//Récupère la data consigne dans le sms et l'enregistre dans le DS3231
void setConsigne(String message, int indexConsigne) 
{//Réglage de la consigne contenue dans le message à l'indexConsigne
  newConsigne = message.substring(indexConsigne + 9, message.length()).toFloat(); // On extrait la valeur et on la cast en float // 9 = "Consigne ".length()
  Serial.print("nouvelle consigne :");
  Serial.println(newConsigne);
  if (!newConsigne) {// Gestion de l'erreur de lecture et remontée du bug
    if (DEBUG) 
    {
      Serial.println("Impossible d'effectuer la conversion de la température String -> Float. Mauvais mot-clé? Mauvais index?");
      Serial.print("indexConsigne = ");
      Serial.println(indexConsigne);
      Serial.print("consigne lenght (>0)= ");
      Serial.println(message.length()- indexConsigne + 9);
      Serial.print("newConsigne = ");
      Serial.println(newConsigne);
    } else {
      sendMessage(gsm, "Erreur de lecture de la consigne envoyee");
    }
  } else if (consigne != newConsigne){ //Si tout se passe bien et la consigne est différente la consigne actuelle
    consigne = newConsigne;
    message = "La nouvelle consigne est de ";
    message.concat(consigne);
    sendMessage(gsm, message);
    eepromWriteData(consigne);//Enregistrement dans l'EEPROM
  } else {
    sendMessage(gsm, "Cette consigne est deja enregistree");
  }
}

//Allume le radiateur en mode marche forcée
void turnOn() 
{//allumage du radiateur si pas de consigne et envoi de SMS
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  if (program) {
    gsm.println("Le programme est toujours actif !!");
  } else {
    // Turn on RELAYS_PERSO and save current state
    forcedHeating = ENABLED;
    gsm.println("Chauffage en marche.");
  }
  gsm.write( 0x1a ); //Permet l'envoi du sms
}

//Efface l'état de marche forcée
void turnOff() 
{//Extinction du rad si pas de consigne et envoie de SMS
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  if (program) {
    gsm.println("Le programme est toujours actif !!");
  } else {
    // Turn off RELAYS_PERSO and save current state
    gsm.println("Le chauffage est eteint.");
    forcedHeating = DISABLED;
  } //Emet une alerte si le programme est toujours actif
  gsm.write( 0x1a ); //Permet l'envoi du sms
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
    if (Century) 
    {      // Won't need this for 89 years.
      Serial.print("1");
    } else 
    {
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
  gsm.print("Temp: ");
  gsm.print(temperature);
  gsm.print(" *C (");
  gsm.print(lastRefresh);
  gsm.print(" min ago, batt: ");
  gsm.print(batteryLevel);
  gsm.println("%)");
  gsm.print("Consigne: ");
  gsm.println(consigne);
  gsm.println(getDate());
  gsm.write( 0x1a );
}


//Récupère l'état de présence d'électricité sur le réseau commun
int getBijunctionState() {
  return (digitalRead(BIJUNCTION_PIN) ? DISABLED : ENABLED);
  //si le détecteur renvoie 1 (non présent), la fonction renvoie DISABLED (return 0)
  //Si le détecteur renvoie 0 (présent), la fonction renvoie ENABLED (return 1)
}