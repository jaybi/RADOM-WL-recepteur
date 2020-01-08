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
extern bool program;
extern int currentSource;
extern float temperature;
extern int lastRefresh;
extern int batteryLevel;
extern float consigne;
extern const bool DEBUG;

void initGSM(SoftwareSerial &gsm)
{
  //Demarrage gsm
  Serial.print("SIM800 Connecting...");
  gsm.begin(9600);
  delay(5000);
  Serial.println("Connected");

  //Config gsm
  gsm.print("AT+CREG?\r\n");
  delay(1000);
  gsm.print("AT+CMGF=1\r\n");
  delay(1000);
  gsm.println("AT+CNMI=2,2,0,0,0\r\n"); //This command selects the procedure
  delay(3000);                          //for message reception from the network.
                                        //gsm.println("AT+CMGD=4\r\n"); //Suppression des SMS
                                        //delay(1000);
}

//Envoie par sms le paramètre de la fonction
void sendMessage(Stream &gsm, String message)
{ //Envoi du "Message" par sms
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  gsm.print(message);
  gsm.write(0x1a); //Permet l'envoi du sms
}

//Envoie par SMS le statut
void sendStatus(Stream &gsm)
{
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  gsm.print("Mode : ");
  gsm.println(program ? "AUTO" : "MANUEL");
  gsm.print("Source : ");
  gsm.println(currentSource ? "Commune" : "Indiv.");
  gsm.print("Temp: ");
  gsm.print(temperature);
  gsm.print(" *C (il y a ");
  gsm.print(lastRefresh);
  gsm.print(" min, batt: ");
  gsm.print(batteryLevel);
  gsm.println("%)");
  gsm.print("Consigne: ");
  gsm.println(consigne);
  gsm.println(getDate());
  gsm.write(0x1a);
}