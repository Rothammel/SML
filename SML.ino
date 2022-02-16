//spart RAM ein
char StringBuffer[250];
#define P(str) strncpy_P(StringBuffer, PSTR(str), sizeof(StringBuffer))

#include <Ethernet.h>        //für w5100 im arduino "built in" enthalten
#include <PubSubClient.h>    //MQTT Bibliothek von Nick O'Leary
#include <SPI.h>             //für w5100 im arduino "built in" enthalten
#include <avr/wdt.h>         //Watchdog
#include <TimeLib.h>         //Uhrzeit & Datum Library von Michael Margolis
#include <FastCRC.h>         //CRC Library von Frank Boesing https://github.com/FrankBoesing/FastCRC


// Globale Variablen & Konstanten
byte inByte;
byte smlMessage[456]; //minimum 453 + 3 byte für CRC
byte state = 0;
int smlIndex;
int startIndex;
int stopIndex;
byte crcCounter;
char mqttBuffer[50];            //Buffer für Umwandlung von variablen in char
char DatumZeit[30];
byte Stunde=0, Minute=0, Sekunde=0, Tag=0, Monat=0;
int  Jahr=0;

// ---- Timer ----
unsigned long         vorMillisReconnect = 100000; // nur alle 100s einen reconnect versuchen
const long            intervalReconnect  = 100000;


const byte startSequence[] = { 0x1B, 0x1B, 0x1B, 0x1B, 0x01, 0x01, 0x01, 0x01 }; //Start Sequenz SML Telegram
const byte stopSequence[]  = { 0x1B, 0x1B, 0x1B, 0x1B, 0x1A };                   //End Sequenz SML Telegram

byte mac[] = {0xB6, 0xE2, 0x32, 0x11, 0x5F, 0x9C};
const IPAddress ip(192, 168, 0, 123);
const IPAddress server(192, 168, 0, 5);             // MQTT Server IP Adresse lokal

EthernetClient ethClient;
PubSubClient client(ethClient);
FastCRC16 CRC16;

void setup()
{
  // Debug Konsole
  Serial.begin(115200);
  // Tastkopf Stromzähler
  Serial1.begin(9600);
  Serial.println("SML Tester");

  client.setServer(server, 1883); // Adresse des MQTT-Brokers
  client.setCallback(callback);   // Handler für eingehende Nachrichten
  client.setBufferSize(512);      // increase MQTT buffer for Home Asssistant autodiscover
  
  // Ethernet-Verbindung aufbauen
  Ethernet.begin(mac, ip);
  
  // Watchdog aktivieren, nicht unter 250ms, folgende timeout verwenden:
  // WDTO_1S, WDTO_2S, WDTO_4S, WDTO_8S
  wdt_enable(WDTO_8S);
}

void loop()
{
  // Watchdog reset
  wdt_reset();
  
  // MQTT Verbindung aufbauen, solange probieren bis es klappt:
  if (!client.connected())  reconnect();
  
  // MQTT loop
  client.loop();


  //Statusmaschine
  switch (state)
  {
    case 0:
      findStartSequence();
      break;
    case 1:
      findStopSequence();
      break;
    case 2:
      addCRC();
      break;
    case 3:
      publishMessage(); // do something with the result
      break;    
  }
  
}

void findStartSequence()
{
  while (Serial1.available())
  {
    inByte = Serial1.read(); //read serial buffer into array
    if (inByte == startSequence[startIndex]) //in case byte in array matches the start sequence at position 0,1,2...
    {
      smlMessage[startIndex] = inByte; //set smlMessage element at position 0,1,2 to inByte value
      startIndex++; 
      if (startIndex == sizeof(startSequence)) //all start sequence values have been identified
      {
        state = 1; //go to next case
        smlIndex = startIndex; //set start index to last position to avoid rerunning the first numbers in end sequence search
        startIndex = 0;
      }
    }
    else
    {
      startIndex = 0;
    }
  }
}



void findStopSequence()
{  
  while (Serial1.available())
    {
      inByte = Serial1.read();
      smlMessage[smlIndex] = inByte;
      smlIndex++;
      if (smlIndex > 456)
      {
        smlIndex = 0;
        state = 0;
      }
  
      if (inByte == stopSequence[stopIndex])
      {
        stopIndex++;
        if (stopIndex == sizeof(stopSequence))
        {
          state = 2; // add CRC
          stopIndex = 0;
        }
      }
      else
      {
        stopIndex = 0;
      }
    }
}

void addCRC()
{
  while (Serial1.available())
    {
      inByte = Serial1.read();
      smlMessage[smlIndex] = inByte;
      smlIndex++;
      crcCounter++;
      if (crcCounter == 3)
      {
        state = 3; // send SML
        crcCounter = 0;
      }
    }
}

void publishMessage()
{
  //debug Ausgabe vom ganzen SML Telegram
  for (int i = 0; i < sizeof(smlMessage); i++)
  {
    if (smlMessage[i] < 0x10) Serial.print("0"); // print fehlende 0
    Serial.print(smlMessage[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  Serial.print("CRC berechnet: ");
  Serial.println(CRC16.x25(smlMessage, sizeof(smlMessage) - 2));
  Serial.print("CRC aus Nachricht: ");
  Serial.println((uint32_t)smlMessage[455] << 8 | (uint32_t)smlMessage[454]);
  
  //CRC check
  if (CRC16.x25(smlMessage, sizeof(smlMessage) - 2) == ((uint32_t)smlMessage[455] << 8 | (uint32_t)smlMessage[454]))
  {
    Serial.println("CRC OK");
    //Positionen im SML Telegramm:
    //150 Gesamt Verbrauch
    //294 Gesamt Leistung
    //314 L1
    //334 L2
    //354 L3
    
    // Gesamtverbrauch:
    int start = 150;
    unsigned long Gesamtverbrauch = (uint64_t)smlMessage[start] << 56;
    Gesamtverbrauch |= (uint64_t)smlMessage[start + 1] << 48;
    Gesamtverbrauch |= (uint64_t)smlMessage[start + 2] << 40;
    Gesamtverbrauch |= (uint64_t)smlMessage[start + 3] << 32;
    Gesamtverbrauch |= (uint64_t)smlMessage[start + 4] << 24;
    Gesamtverbrauch |= (uint32_t)smlMessage[start + 5] << 16;
    Gesamtverbrauch |= (uint32_t)smlMessage[start + 6] <<  8;
    Gesamtverbrauch |=           smlMessage[start + 7];

    float fGesamtverbrauch = (float)Gesamtverbrauch / 10000;
    Serial.print("Gesamtverbrauch: ");
    Serial.print(fGesamtverbrauch);
    Serial.println(" kWh");
    client.publish("/SmartMeter/Gesamtverbrauch", dtostrf(fGesamtverbrauch, 1, 3, mqttBuffer), true);
    
    start = 294;
    
    int GesamtWirkleistung = (uint64_t)smlMessage[start] << 24;
    GesamtWirkleistung |= (uint32_t)smlMessage[start + 1] << 16;
    GesamtWirkleistung |= smlMessage[start + 2] << 8;
    GesamtWirkleistung |= smlMessage[start + 3];
  
    Serial.print("Gesamt Wirkleistung: ");
    Serial.print(GesamtWirkleistung);
    Serial.println(" W");
    client.publish("/SmartMeter/GesamtWirkleistung", dtostrf(GesamtWirkleistung, 1, 0, mqttBuffer), true);
  
    start = 314;
    
    int L1Wirkleistung = (uint64_t)smlMessage[start] << 24;
    L1Wirkleistung |= (uint32_t)smlMessage[start + 1] << 16;
    L1Wirkleistung |= smlMessage[start + 2] << 8;
    L1Wirkleistung |= smlMessage[start + 3];
  
    Serial.print("L1 Wirkleistung: ");
    Serial.print(L1Wirkleistung);
    Serial.println(" W");
    client.publish("/SmartMeter/L1", dtostrf(L1Wirkleistung, 1, 0, mqttBuffer), true);
  
    start = 334;
    
    int L2Wirkleistung = (uint64_t)smlMessage[start] << 24;
    L2Wirkleistung |= (uint32_t)smlMessage[start + 1] << 16;
    L2Wirkleistung |= smlMessage[start + 2] << 8;
    L2Wirkleistung |= smlMessage[start + 3];
  
    Serial.print("L2 Wirkleistung: ");
    Serial.print(L2Wirkleistung);
    Serial.println(" W");
    client.publish("/SmartMeter/L2", dtostrf(L2Wirkleistung, 1, 0, mqttBuffer), true);
  
    start = 354;
    
    int L3Wirkleistung = (uint64_t)smlMessage[start] << 24;
    L3Wirkleistung |= (uint32_t)smlMessage[start + 1] << 16;
    L3Wirkleistung |= smlMessage[start + 2] << 8;
    L3Wirkleistung |= smlMessage[start + 3];
  
    Serial.print("L3 Wirkleistung: ");
    Serial.print(L3Wirkleistung);
    Serial.println(" W");
    client.publish("/SmartMeter/L3", dtostrf(L3Wirkleistung, 1, 0, mqttBuffer), true);
  }
  // Neustart
  smlIndex = 0;
  state = 0;
  memset(smlMessage, 0, sizeof(smlMessage));
}

void callback(char* topic, byte* payload, unsigned int length)
{
  // Zähler
  int i = 0;
  // Hilfsvariablen für die Convertierung der Nachricht in ein String
  char message_buff[100];
 
  // Kopieren der Nachricht und erstellen eines Bytes mit abschließender \0
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
 

  // wenn topic /System/Zeit empfangen dann String zerlegen und Variablen füllen 
  if (String(topic) == "/System/Zeit"){
    Stunde=String(message_buff).substring(0,2).toInt();
    Minute=String(message_buff).substring(2,4).toInt();
    Sekunde=String(message_buff).substring(4,6).toInt();
    setTime(Stunde, Minute, Sekunde, Tag, Monat, Jahr);
  }
  // wenn topic /System/Datum empfangen dann String zerlegen und Variablen füllen 
  if (String(topic) == "/System/Datum")
  {
    Tag=String(message_buff).substring(0,2).toInt();
    Monat=String(message_buff).substring(2,4).toInt();
    Jahr=String(message_buff).substring(4,8).toInt();
  }
}

void reconnect()
{
  if(millis()-vorMillisReconnect > intervalReconnect)
  {
    vorMillisReconnect = millis();

    //Verbindungsversuch:
    if (client.connect("SmartMeter","Stan","rotweiss"))
    {
      // Abonierte Topics:
      client.subscribe("/System/Zeit");
      client.subscribe("/System/Datum");
      //HomeAssistant autodiscover configs
      client.publish("homeassistant/sensor/SmartMeter/Gesamtverbrauch/config", P("{\"name\":\"Gesamtverbrauch\",\"obj_idd\":\"Gesamtverbrauch\",\"uniq_id\":\"Gesamtverbrauch\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/SmartMeter/Gesamtverbrauch\",\"stat_cla\":\"measurement\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/SmartMeter/GesamtWirkleistung/config", P("{\"name\":\"Gesamt Wirkleistung\",\"obj_idd\":\"GesamtWirkleistung\",\"uniq_id\":\"gesamtwirkleistung\",\"unit_of_meas\":\"W\",\"stat_t\":\"/SmartMeter/GesamtWirkleistung\"}"), true);
      client.publish("homeassistant/sensor/SmartMeter/L1/config", P("{\"name\":\"Wirkleistung L1\",\"obj_idd\":\"WirkleistungL1\",\"uniq_id\":\"wirkleistung_l1\",\"unit_of_meas\":\"W\",\"stat_t\":\"/SmartMeter/L1\"}"), true);
      client.publish("homeassistant/sensor/SmartMeter/L2/config", P("{\"name\":\"Wirkleistung L2\",\"obj_idd\":\"WirkleistungL2\",\"uniq_id\":\"wirkleistung_l2\",\"unit_of_meas\":\"W\",\"stat_t\":\"/SmartMeter/L2\"}"), true);
      client.publish("homeassistant/sensor/SmartMeter/L3/config", P("{\"name\":\"Wirkleistung L3\",\"obj_idd\":\"WirkleistungL3\",\"uniq_id\":\"wirkleistung_l3\",\"unit_of_meas\":\"W\",\"stat_t\":\"/SmartMeter/L3\"}"), true);
    }
  }
}
