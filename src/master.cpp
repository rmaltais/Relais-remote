

/* Fonctionnement de 8 relais à l'aide de montage et du 74HC595
 Le status est conservé lors de la coupure de courant et peut faire
 actionner des relais.
 REJEAN MALTAIS 2022
 Ajout des valeurs enregistré dans l'eeprom.
 Mise à jour le 2022-02-20
 Ajouter de la led qui indique que au moins un des relais est actif.
 */
#include <Arduino.h>
// Radio enable
#define MY_RADIO_RF24
#define MY_DEBUG
#define DEBUG_ON
#define MY_NODE_ID 2

#include <LiquidCrystal_I2C.h> //LiquidCrystal by F Malpartida
#include <Wire.h>
#include <TimeLib.h>
#include <MySensors.h>

#define SN "RELAY REMOTE"
#define SV "1.0"
#define RELAY_ON 1
#define RELAY_OFF 0
#define NB_OUTPUTS 8
#define CHILD_ID_RELAY 0
MyMessage tmpOpen(CHILD_ID_RELAY, V_VAR1);

// Déclaration des fonctions pour convention du language.

void fastClear();
void slowToggleLED();
void displayMenu(void);
void changeOutputState(int i, boolean newState);
void updateStatus();

// Utilise le composant 74HC595 pour minimiser le nombre sorties.
#define OUTPUT_SER 7   // Data
#define OUTPUT_RCLK 8  // Latch
#define OUTPUT_SRCLK 4 // Clock
#define OUTPUT_ENA 6   // Enable (Must be low with my board, othewise not need)
#define HEARTBEAT_DELAY 3600000
#ifdef DEBUG_ON
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define SERIAL_START(x)
#endif

// Caracteres custom pour afficher le status.
byte gate[8] = {
	0x1F,
	0x1F,
	0x15,
	0x15,
	0x04,
	0x04,
	0x04,
	0x04};

byte outA[] = {
	0x00,
	0x00,
	0x00,
	0x0A,
	0x1B,
	0x1B,
	0x1F,
	0x1F};

bool initialValueSent = false;
const int ledPin = 5;
byte menuState = 0;
bool state;
byte outputStates;
unsigned long lastHeartBeat = 0;
int lastValve;
String nomRelais[8] = {"Relais 1", "Relais 2", "Relais 3", "Relais 4", "Relais 5", "Relais 6", "Relais 7", "Relais 8"};

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

void setup()

{

	pinMode(OUTPUT_SER, OUTPUT);
	pinMode(OUTPUT_RCLK, OUTPUT);
	pinMode(OUTPUT_SRCLK, OUTPUT);
	pinMode(OUTPUT_ENA, OUTPUT);
	pinMode(ledPin, OUTPUT);
	Serial.begin(115200);

	lcd.begin(16, 2); //(16 characters and 2 line display)
	lcd.clear();
	lcd.backlight();
	lcd.createChar(0, gate);
	lcd.createChar(1, outA);
	// Activé le 74HC595 en raison de la configuration de la plaquette.
	digitalWrite(OUTPUT_ENA, LOW);

	// Ici avec Home Assistant, on doit initialisé les relais pour qu'il soit reconnus et aussi mise à jour matériel (voir fonction ci-haut)
	updateStatus();

	lcd.setCursor(0, 0);
	lcd.print(F("Relais Controle"));
	lcd.setCursor(0, 2);
	lcd.print(F("  Version. 1.0"));
	wait(3000);
	fastClear();
	lcd.setCursor(0, 0);
	lcd.print(F("Relais Controle"));
	lcd.setCursor(0, 2);
	lcd.print(F(" HomeAssistant "));
	wait(3000);
	displayMenu();
}

void presentation()
{
	// Send the Sketch Version Information to the Gateway
	sendSketchInfo(SN, SV);

	// Présentation de tous les relais :
	for (int i = 0; i < NB_OUTPUTS; i++)
	{
		present(i, S_BINARY);
	}
}

void loop()
{

	if (millis() > lastHeartBeat + HEARTBEAT_DELAY)
	{
		lastHeartBeat = millis();
		sendHeartbeat();
	}
	displayMenu();

	for (int sensor = 0; sensor < NB_OUTPUTS; sensor++)
	{
		MyMessage initMsg(sensor, V_STATUS);
		int lstate = loadState(sensor);
		if (lstate == 1)
		{
			slowToggleLED();
		}
	}
}

void receive(const MyMessage &message)
{

	boolean currentStatus;
	switch (message.type)
	{
	case V_CUSTOM:
		// Revert output status :
		currentStatus = bitRead(outputStates, message.sensor);
		changeOutputState(message.sensor, !currentStatus);
		break;

	case V_STATUS:
		changeOutputState(message.sensor, message.getBool());
		break;
	}

	// Send new state :
	currentStatus = bitRead(outputStates, message.sensor);
	MyMessage msg(message.sensor, V_STATUS);
	msg.set(currentStatus);
	send(msg);
	saveState(message.getSensor(), message.getBool());
	menuState++; // Ici je change toujours la valeur après une action pour que la fonction d'affichage se met à jour
}

void changeOutputState(int i, boolean newState)
{
	bitWrite(outputStates, i, newState);

	// Lock latch
	digitalWrite(OUTPUT_RCLK, LOW);
	// Write bits
	shiftOut(OUTPUT_SER, OUTPUT_SRCLK, MSBFIRST, outputStates);
	// Unlock latch
	digitalWrite(OUTPUT_RCLK, HIGH);
}

void updateStatus()
{

	for (int sensor = 0; sensor < NB_OUTPUTS; sensor++)
	{
		MyMessage initMsg(sensor, V_STATUS);
		int lstate = loadState(sensor);

		send(initMsg.setSensor(sensor).set(lstate ? RELAY_ON : RELAY_OFF));
		changeOutputState(sensor, (lstate ? RELAY_ON : RELAY_OFF)); // Mise à jour initiale sur le controlleur
	}
}

void fastClear()
{
	lcd.setCursor(0, 0);
	lcd.print(F("                "));
	lcd.setCursor(0, 1);
	lcd.print(F("                "));
}

void slowToggleLED()
{
	static unsigned long slowLedTimer;
	if (millis() - slowLedTimer >= 125UL)
	{
		digitalWrite(ledPin, !digitalRead(ledPin));
		slowLedTimer = millis();
	}
}

void displayMenu(void)
{
	static byte lastMenuState = -1;
	if (menuState != lastMenuState) // Conditon classique de non retour jusqu'à un changement de la valeur pour évité le scintillement
	{
		fastClear();
		lcd.setCursor(0, 0);
		lcd.print(F(" STATUS RELAIS "));
		for (int sensor = 0; sensor < NB_OUTPUTS; sensor++)
		{
			MyMessage initMsg(sensor, V_STATUS);
			int lstate = loadState(sensor);
			int relay = sensor + 1;
			lcd.setCursor((relay + 3), 1);
			if (lstate == 1)
			{
				lcd.write(byte(0));
			}
			else
				lcd.write(byte(1));
		}

		lastMenuState = menuState;
		DEBUG_PRINT(F("Status menu: "));
		DEBUG_PRINT(menuState);
	}
}