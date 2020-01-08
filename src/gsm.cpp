#include <Arduino.h>
#include <SoftwareSerial.h>

#include "PersonalData.h"
#include "radom.h"
#include "gsm.h"


//Variables
extern PersonalData personalData;
extern String phoneNumber;
extern String pinNumber;
extern String textMessage;
extern const bool DEBUG;

//Configure le SIM 800 L 
void initGSM(SoftwareSerial gsm)
{
//Demarrage GSM
    Serial.print("GSM Connecting...");
    gsm.begin(9600);
    delay(5000);
    Serial.println("Connected");

    //Config GSM
    gsm.print("AT+CREG?\r\n");
    delay(1000);
    gsm.print("AT+CMGF=1\r\n");
    delay(1000);
    gsm.println("AT+CNMI=2,2,0,0,0\r\n"); //This command selects the procedure
    delay(3000);                          //for message reception from the network.
    //gsm.println("AT+CMGD=4\r\n"); //Suppression des SMS
    //delay(1000);
}

//Permet de traiter la réception de sms, récupère le texte du message.
void receiveSMS(SoftwareSerial gsm)
{
  if (gsm.available() > 0) 
  {
    textMessage = gsm.readString();
    if (DEBUG) {
      Serial.println(textMessage);
    }
    //Cas nominal avec le numéro de tel par défaut
    if ( (textMessage.indexOf(phoneNumber)) < 10 && textMessage.indexOf(phoneNumber) > 0) {
      readSMS(textMessage);
    } 
    else if (textMessage.indexOf(pinNumber) < 51 && textMessage.indexOf(pinNumber) > 0){
      int indexOfPhoneNumber = textMessage.indexOf("+",5);
      int finalIndexOfPhoneNumber = textMessage.indexOf("\"", indexOfPhoneNumber);
      String newPhoneNumber = textMessage.substring(indexOfPhoneNumber,finalIndexOfPhoneNumber);
      String information = "Nouveau numero enregistre : ";
      information.concat(newPhoneNumber);
      sendMessage(gsm, information);
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
}

//Envoie par sms le paramètre de la fonction
void sendMessage(SoftwareSerial gsm, String message) 
{//Envoi du "Message" par sms
    gsm.print("AT+CMGS=\"");
    gsm.print(phoneNumber);
    gsm.println("\"");
    delay(500);
    gsm.print(message);
    gsm.write( 0x1a ); //Permet l'envoi du sms
}