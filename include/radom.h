//MODE DEBUG *************************************************************************************************
//Permet d'afficher le mode débug dans la console
//Beaucoup plus d'infos apparaissent
const bool DEBUG = 0; // 0 pour désactivé et 1 pour activé

// Liste des fonctions
void readSMS(String message);
void setConsigne(String message, int indexConsigne);
void heatingProg();
void turnOn() ;
void turnOff() ;
String getDate() ;
void sendStatus() ;
int getBijunctionState();
void listen(int timeout);
void checkThermometer();
void heatingProcess();
void switchToIndividual();
void switchToCommon();