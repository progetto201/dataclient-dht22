/**
 * DATACLIENT-DHT22: invia i dati recuperati dal dht22 al raspberry via MQTT
 * 
 * Lo sketch verifica nel setup() se il valore delle variabili e' accettabile: 
 * - <timeToWait> deve essere almeno 2000 [ms]
 * 
 * poi memorizza in <macAddr> l'indirizzo mac del dispositivo.
 * > Per farlo si usa la funzione macAddrToString()
 * 
 * > Formato: @codeAB:AB:AB:AB:AB:AB@endcode
 * 
 * Nella funzione loop() viene gestita la connessione all'access point di raspberry
 * attraverso la funzione WiFiconn() e la connessione al broker MQTT con la funzione MQTTconn().
 * > WiFiconn() memorizza l'indirizzo IP nella stringa <ipAddr>
 * 
 * > MQTTconn() annuncia la presenza del sensore al sistema usando la funzione present()
 * 
 * Dopo essersi connessi all'AP e al broker MQTT vengono rilevate temperatura e umidita'
 * dal DHT22 e viene misurato l'RSSI.
 * Temperatura, umidita' e RSSI vengono mandati al broker MQTT dalla funzione reportData()
 * 
 * @author Stefano Zenaro
 * @version 01_01
 * @date $Date: 2020/02/08 20:42:00 $
 * @file
*/

// dim(x) restituisce la dimensione di un array
template <typename T, int N> char(&dim_helper(T(&)[N]))[N];
#define dim(x) (sizeof(dim_helper(x)))

// ********************* INCLUDE *********************

#include <ArduinoJson.h>   // libreria JSON
#include "DHT.h"           // libreria DHT
#include <ESP8266WiFi.h>   // libreria WiFi
#include <PubSubClient.h>  // libreria MQTT

// ********************* DEFINIZIONI/VARIABILI *********************

  // VARIABILI DEBUG
#define dhtConnected false //!< true = DHT22 utilizzabile/connesso
#define serialDebug true //!< true = visualizza messaggi di debug attraverso comunicazione seriale

#define DHTPIN 4       //!< pin dato del DHT22
#define DHTTYPE DHT22  //!< tipo di sensore DHT (DHT22)

const int nodeType = 0; //!< tipo di nodo

  // VARIABILI MQTT
const char mqttServer[15] = "raspberry";  //!< IP/dominio mqtt server
const int mqttPort = 1883;                //!< porta mqtt
const char* mqttUser = "test";            //!< utente mqtt
const char* mqttPassword = "test";        //!< password mqtt

char prMainTopic[14] = "presentation/"; //!< topic principale presentazione
char optMainTopic[9] = "options/";      //!< topic principale opzioni
char dataMainTopic[6] = "data/";        //!< topic principale dati

  // Se il sensore non e' connesso (dhtConnected=false), uso dati finti per testare
float humidity = 12.34;     //!< umidita' (default 12.34 come test)
float temperature = 12.34;  //!< temperatura Celsius (default 12.34 come test)

int timeSinceLastRead = 0;  //!< variabile tempo dall'ultima lettura DHT
int timeToWait = 2000;      //!< variabile tempo da aspettare per interrogare DHT (minimo 2000 [ms])

  // VARIABILI DI RETE
const char* WIFI_SSID = "nameofnetwork"; //!< SSID access point
const char* WIFI_PASS = "longerthan8charspassword"; //!< Passphrase access point

char ipAddr[16];   //!< contiene IP address
char macAddr[18];  //!< contiene MAC address

// ********************* OGGETTI *********************

DHT dht(DHTPIN, DHTTYPE);  //!< creazione oggetto dht

  // MQTT client 
WiFiClient espClient;           //!< crea un client WiFI
PubSubClient client(espClient); //!< usa client WiFi per MQTT

// ********************* FUNZIONI *********************


void WiFiconn() {
  /**
   * Questa funzione si occupa di connettersi al WiFi.
   *
   * Disattiva e riattiva il WiFi per evitare un tentativo di connessione mentre e' gia' connesso,
   * e utilizza ssid e password per tentare una connessione alla rete.
   * Dopo la connessione viene memorizzato l'indirizzo IP in <ipAddr>.
   * 
   * Se in 15 secondi non ci riesce smette di tentare di connettersi all'access point.
  */

  if(serialDebug){
    Serial.println(F("Inizio connessione al WIFI"));
    Serial.println(F("-------------------------------------"));
    Serial.print(F("\n\nMi sto connettendo alla rete: "));
    Serial.println(WIFI_SSID);
  }

  WiFi.persistent(false); // WiFi fix: https://github.com/esp8266/Arduino/issues/2186
  WiFi.mode(WIFI_OFF); // Disabilito il wifi
  WiFi.mode(WIFI_STA); // Lo riabilito in modalita' "station" (si puo' connettere all'AP)
  WiFi.begin(WIFI_SSID, WIFI_PASS); // mi connetto al wifi

  unsigned long wifiConnectStart = millis(); // salvo momento del tentativo di connessione

  while (WiFi.status() != WL_CONNECTED) {
    // finche' non e' connesso al WiFi

    if (WiFi.status() == WL_CONNECT_FAILED and serialDebug) {
      // se la connessione e' fallita
      Serial.println(F("Connessione fallita. Per favore controllare le credenziali:\n"));
      Serial.print(F("SSID: "));
      Serial.println(WIFI_SSID);
      Serial.print(F("Password: "));
      Serial.println(WIFI_PASS);
      Serial.println();
    }

    if (serialDebug){
      Serial.println(F("..."));
    }

    delay(500);

    if(millis() - wifiConnectStart > 15000) {
      // se la connessione impiega piu' di 15 secondi
      if (serialDebug){
        Serial.println(F("Connessione fallita"));
        Serial.println(F("Per favore modificare i parametri di connessione"));
      }
      return;
    }
  }

  // Connessione al WIFI riuscita
  if (serialDebug){
    Serial.println(F("\nConnessione riuscita"));
    Serial.println(F("Indirizzo IP: "));
    Serial.println(WiFi.localIP());
    Serial.println();
  }

  // salva l'IP in ipBytes, convertilo in stringa e salvala in <ipAddr>
  IPAddress ipBytes;
  ipBytes = WiFi.localIP();
  snprintf(ipAddr, sizeof ipAddr, "%d.%d.%d.%d", ipBytes[0], ipBytes[1], ipBytes[2], ipBytes[3]);
}


void macAddrToString(byte *mac, char *str) {
  /**
   * Converte indirizzo MAC in una stringa. 
   * 
   * Scorre l'array di byte:
   * per ogni byte prende i due valori esadecimali e ottiene
   * la loro "versione stringa".
   * Ogni byte e' concatenato da ":"
   * > L'ultimo ":" viene sostituito da "\0"
   * 
   * @param mac Puntatore a un array di byte con indirizzo MAC
   * @param str Puntatore a una stringa
  */
  for(int i = 0; i<6; i++) {
    byte digit;
    digit = (*mac >> 8) & 0xF;
    *str++ = (digit < 10 ? '0' : 'A'-10) + digit;
    digit = (*mac) & 0xF;
    *str++ = (digit < 10 ? '0' : 'A'-10) + digit;
    *str++ = ':';
    mac ++;
  }
  // sostituisci ultimi due punti con nul
  str[-1] = '\0';
}


void MQTTconn(){
  /**
   * Connette il nodemcu al broker MQTT su raspberry.
   * 
   * Imposta nome/IP del server e la relativa porta,
   * imposta la funzione da richiamare quando si ricevono messaggi
   * sui topic sottoscritti e si tenta la connessione con username e password.
   * > L'id di connessione e' l'indirizzo MAC del dispositivo.
   * 
   * Se la connessione ha successo viene inviato un messaggio di presentazione
   * attraverso la funzione present()
  */
  client.setServer(mqttServer, mqttPort); // imposta dominio/IP broker MQTT e porta
  client.setCallback(callback); // imposta funzione callback

  unsigned long mqttConnectStart = millis(); // salvo momento del tentativo di connessione
  
  // cerca di connettersi al broker MQTT
  while (!client.connected()) {
    if (serialDebug){
      Serial.print(F("Mi sto connettendo al broker MQTT '"));
      Serial.print(mqttServer);
      Serial.print(F(":"));
      Serial.print(mqttPort);
      Serial.println(F("'..."));
    }

    if (client.connect(macAddr, mqttUser, mqttPassword)) { 
      // client connesso al broker
      if (serialDebug){
        Serial.println(F("Connesso"));
      }
    } 
    else {
      // connessione fallita
      if (serialDebug){
        Serial.print(F("Connessione fallita con stato: "));
        Serial.println(client.state());
      }
      delay(2000);
    }

    if(millis() - mqttConnectStart > 15000) {
      // se la connessione impiega piu' di 15 secondi
      if (serialDebug){
        Serial.println(F("Connessione fallita"));
      }
      return;
    }
  }

  present(); // presento il dispositivo al sistema
}


void present(){
  /**
   * Presenta il dispositivo al sistema via messaggio MQTT.
   * 
   * Manda sul topic presentation/<mac> il proprio indirizzo MAC, IP, tipo di nodo e timeToWait.
   * Se il dispositivo e' riuscito a inviare il dato il client si iscrive al topic per ottenere
   * configurazioni dal raspberry, altrimenti si disconnette dal broker 
   * per cercare di ri-presentarsi alla prossima connessione
  */

  // options topic: Ex. options/AB:AB:AB:AB:AB:AB
  char opt_topic[dim(optMainTopic) + dim(macAddr) + 1];
  strncpy(opt_topic, optMainTopic, dim(optMainTopic));
  strncat(opt_topic, macAddr, dim(macAddr));
  
  // presentation topic: Ex. presentation/AB:AB:AB:AB:AB:AB
  char pr_topic[dim(prMainTopic) + dim(macAddr) + 1];
  strncpy(pr_topic, prMainTopic, dim(prMainTopic));
  strncat(pr_topic, macAddr, dim(macAddr));
  
  // oggetto JSON con i dati
  StaticJsonDocument<300> jsonData;

  // inserisci dati nel JSON
  jsonData["ip"] = ipAddr;
  jsonData["mac"] = macAddr;
  jsonData["nodeType"] = nodeType;
  jsonData["sketchTimeToWait"] = timeToWait;

  // converti JSON in stringa
  char jsonBuffer[512];
  size_t n = serializeJson(jsonData, jsonBuffer); // restituisce dimensione jsonBuffer
  
  // invia il JSON al broker MQTT
  if (client.publish(pr_topic, jsonBuffer, n)){
    // successo
    if (serialDebug){
      Serial.println(F("Successo: messaggio presentazione inviato"));
    }
    // iscriviti al topic in cui ricevere le impostazioni
    client.subscribe(opt_topic);
  } 
  else {
    // errore durante invio messaggio: disconnessione dal broker
    if (serialDebug){
      Serial.println(F("Errore durante presentazione"));
    }
    client.disconnect();
  }
}


void callback(char* topic, byte* payload, unsigned int length) {
  /**
   * Funzione di callback sui topic sottoscritti. 
   * 
   * Compone il topic opzioni.
   * Se il topic del messaggio ricevuto corrisponde al topic delle opzioni,
   * controlla se il valore ricevuto e' valido.
   * Se e' valido (numero intero) usalo.
   * 
   * @param topic Stringa con topic
   * @param payload Messaggio ricevuto
   * @param length Lunghezza messaggio
  */
  
  // options topic: Ex. options/AB:AB:AB:AB:AB:AB
  char opt_topic[dim(optMainTopic) + dim(macAddr) + 1];
  strncpy(opt_topic, optMainTopic, dim(optMainTopic));
  strncat(opt_topic, macAddr, dim(macAddr));
  
  if (serialDebug) {
    Serial.print(F("Nuovo messaggio arrivato al topic: "));
    Serial.println(topic);
    Serial.println(opt_topic);

    Serial.print(F("Messaggio:"));
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println(F("\n-----------------------"));
  }

  // se il topic del messaggio e' options/<macAddr>...
  if (strcmp(opt_topic, topic) == 0){
    // crea oggetto JSON e interpreta la stringa JSON
    StaticJsonDocument<256> doc;
    deserializeJson(doc, payload, length);

    // memorizza timeToWait per controllare il tipo di variabile
    JsonVariant newTimeToWait = doc["timeToWait"];
    if (newTimeToWait.is<int>()) {
      // e' un numero intero, sostituisci timeToWait attuale
      timeToWait = doc["timeToWait"];
      
      // deve essere almeno 2000
      if (timeToWait < 2000){
        timeToWait = 2000;
      }

      if (serialDebug){
        Serial.print(F("timeToWait e' cambiato in: "));
        Serial.println(timeToWait);
      }
    }
    else{
      if (serialDebug){
        Serial.println("timeToWait NON e' numero intero");
      }
    }
  }
}


void reportData(float t_humidity, float t_temp, int t_rssi){
  /**
   * Invia i dati al broker MQTT.
   * 
   * Compone il topic per i dati, crea un oggetto JSON con i dati passati alla funzione,
   * lo converte in stringa e lo invia al broker MQTT su raspberry pi.
   * 
   * @param t_humidity Float umidita'
   * @param t_temp Float temperatura
   * @param t_rssi Intero RSSI
  */

  // data_topic Ex. data/AB:AB:AB:AB:AB:AB
  char data_topic[dim(dataMainTopic) + dim(macAddr) + 1];
  strncpy(data_topic, dataMainTopic, dim(dataMainTopic));
  strncat(data_topic, macAddr, dim(macAddr));

  // oggetto JSON con i dati
  StaticJsonDocument<300> jsonData;

  // inserisci dati nel JSON
  jsonData["humidity"] = t_humidity;
  jsonData["temperature"] = t_temp;
  jsonData["rssi"] = t_rssi;

  // converti JSON in stringa
  char jsonBuffer[512];
  size_t n = serializeJson(jsonData, jsonBuffer);

  // invia stringa JSON al broker mqtt
  if (client.publish(data_topic, (uint8_t*)jsonBuffer, n, false)){
    if(serialDebug){
      Serial.print(F("Dati inviati con successo al topic: "));
      Serial.println(data_topic);
    }
  }
  else {
    if(serialDebug){
      Serial.print(F("Non e' stato possibile mandare i dati al topic: "));
      Serial.println(data_topic);     
    }
  }
}


void setup() {
  /**
   * Salva indirizzo MAC e si connette all' access point e al broker MQTT.
   * 
   * La funzione converte l'indirizzo MAC da byte array in stringa e lo salva nella variabile <macAddr>,
   * si assicura che timeToWait vale almeno 2000 [ms]
   * > Per rilevare i dati dal DHT22 servono almeno 2 secondi tra le letture
  */

  // salva indirizzo MAC in <mac>
  byte mac[6];
  WiFi.macAddress(mac);
  
  // converti MAC in stringa e memorizzala in <macAddr>
  macAddrToString(mac, macAddr);
  
  if(timeToWait < 2000){
    // < 2 secondi sono pochi tra le rilevazioni...
    timeToWait = 2000; // impongo 2 secondi minimi
  }

  if (serialDebug){
    Serial.begin(9600);      // inizializza comunicazione seriale
    Serial.setTimeout(2000); // imposta tempo massimo [ms] da aspettare per dati seriali

    while(!Serial) { } // Attende che la comunicazione seriale sia stabilita

    Serial.println(F("Il dispositivo e' pronto"));
    Serial.println(F("-------------------------------------"));
  }
}


void loop() {
  /**
   * Mantiene connesso il dispositivo a WiFi e broker MQTT, rileva dati e li invia al broker.
   * 
   * La funzione controlla se il dispositivo e' connesso al WiFi:
   * se non e' piu' connesso si ricollega richiamando la funzione WiFiconn()
   * 
   * Poi controlla se e' connesso al broker MQTT:
   * se non e' piu' connesso si ricollega richiamando la funzione MQTTconn()
   * 
   * Ogni <timeToWait> viene salvato il valore di RSSI, vengono eseguite le rilevazioni
   * dal sensore DHT22 e poi invia i dati al broker MQTT con la funzione reportData()
  */

  if (WiFi.status() != WL_CONNECTED) {
    // se non e' connesso al wifi, richiama WiFiconn()
    if (serialDebug){
      Serial.println(F("Il node non e' piu' connesso al WiFi"));
    }
    WiFiconn();
    return;
  }

  if (!client.connected()){
    // se client non e' connesso al server MQTT cerca di riconnetterti
    MQTTconn();
    return;
  }

  // mantieni attivo client MQTT
  client.loop();
  
  // Ogni circa <timeToWait> millisecondi leggi DHT
  // Leggere la temperatura o l'umidita' dal sensore puo' richiedere 2 secondi
  if(timeSinceLastRead > timeToWait) {
    // memorizza RSSI
    int rssi = WiFi.RSSI();
    
    if(dhtConnected){
      // se DHT connesso
      humidity = dht.readHumidity();       // leggi umidita' dal sensore
      temperature = dht.readTemperature(); // leggi temperatura Celsius

      if (isnan(humidity) || isnan(temperature)) {
        // assicurati che la lettura ha avuto successo
        if (serialDebug){
          Serial.println(F("Lettura dati dal sensore DHT fallita!"));
        }
        
        timeSinceLastRead = 0; // tempo dall'ultima lettura si resetta
        return; // ripeto loop()
      }
    }
    
    if (serialDebug){
      Serial.print(F("Umidita': "));
      Serial.print(humidity);
      Serial.print(F(" %\t Temperatura: "));
      Serial.print(temperature);
      Serial.print(F(" *C\t RSSI: "));
      Serial.print(rssi);
      Serial.println(F(" dBm"));
    }
    reportData(humidity, temperature, rssi); // richiama funzione che invia i dati
    timeSinceLastRead = 0; // resetta tempo dall'ultima lettura 
  }
  delay(100);               // aspetta 100 ms
  timeSinceLastRead += 100; // incrementa di 100 il numero di ms aspettati dall'ultima lettura
}
