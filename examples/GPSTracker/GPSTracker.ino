// Ejemplo: publicar la posición GPS (latitud/longitud) leyendo un módulo
// GPS por UART -- los módulos NEO-6M / NEO-M8N (muy comunes y baratos) son
// el caso típico.
//
// REQUIERE agregar a tu platformio.ini (o instalar desde el Gestor de
// Librerías del IDE de Arduino, buscando "TinyGPSPlus"):
//
//   lib_deps =
//       mikalhart/TinyGPSPlus@^1.0.3
//
// CONEXIÓN: el módulo GPS habla por UART (serie), así que se usa el
// SEGUNDO puerto serie del ESP32 (Serial2) -- si usáramos el mismo puerto
// que el USB/Monitor Serie, se pisarían los datos.
//   GPS TX -> ESP32 GPIO16 (RX2)
//   GPS RX -> ESP32 GPIO17 (TX2)
//   VCC/GND según el módulo (la mayoría acepta 3.3V-5V)
//
// OJO: un módulo GPS puede tardar varios minutos en conseguir señal la
// primera vez que se enciende ("cold start"), sobre todo en interiores o
// cerca de una ventana chica -- es normal que no haya posición válida
// todavía un rato largo después de prender el dispositivo.

#include <TinyGPSPlus.h>
#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define GPS_RX_PIN 16 // ESP32 RX2 <- GPS TX
#define GPS_TX_PIN 17 // ESP32 TX2 -> GPS RX
#define GPS_BAUD 9600 // velocidad de fabrica tipica de los modulos NEO-6M/NEO-M8N

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // Serial2 del ESP32

unsigned long lastPublish = 0;

void setup()
{
	Serial.begin(921600);
	gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop()
{
	tecnova.loop();

	// Hay que alimentar el parser NMEA byte a byte todo el tiempo, no solo
	// cuando toca publicar -- si no, se pierden sentencias y nunca
	// engancha una posición válida.
	while (gpsSerial.available())
	{
		gps.encode(gpsSerial.read());
	}

	if (millis() - lastPublish > 5000)
	{
		lastPublish = millis();

		if (gps.location.isValid())
		{
			// TinyGPSPlus devuelve double -- se castea a float explicito
			// porque si no, es ambiguo con cual overload de setValue() usar
			// (podria convertirse tanto a float como a int).
			tecnova.setValue("latitud", (float)gps.location.lat());
			tecnova.setValue("longitud", (float)gps.location.lng());
		}
		else
		{
			Serial.println("GPS todavia sin señal valida...");
		}
	}

	delay(50);
}
