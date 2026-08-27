// Ejemplo: leer temperatura y humedad con un sensor DHT11 -- el "clásico"
// de los kits de iniciación, más simple y barato que el BME280 pero
// también más lento y menos preciso (un solo decimal, y no se puede leer
// más de una vez cada ~2 segundos).
//
// REQUIERE agregar a tu platformio.ini (o instalar desde el Gestor de
// Librerías del IDE de Arduino, buscando "DHT sensor library" de Adafruit):
//
//   lib_deps =
//       adafruit/DHT sensor library@^1.4.6
//       adafruit/Adafruit Unified Sensor@^1.1.14
//
// CONEXIÓN: VCC -> 3.3V o 5V (según el módulo que tengas), GND -> GND,
// DATA -> un pin digital cualquiera (acá GPIO4). Si tu módulo es de 3
// patas sueltas (no la placa con resistencia incorporada), hace falta una
// resistencia pull-up de 10kΩ entre DATA y VCC.

#include <DHT.h>
#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define DHT_PIN 4
#define DHT_TYPE DHT11

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);
DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastRead = 0;

void setup()
{
	Serial.begin(921600);
	dht.begin();
	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop()
{
	tecnova.loop();

	if (millis() - lastRead > 3000)
	{
		lastRead = millis();

		float humedad = dht.readHumidity();
		float temperatura = dht.readTemperature();

		// El DHT11 devuelve NaN ("Not a Number") cuando falla la lectura --
		// pasa seguido, no es necesariamente un cable mal puesto. Por eso
		// nunca hay que publicar sin chequear esto primero.
		if (isnan(humedad) || isnan(temperatura))
		{
			Serial.println("Error leyendo el DHT11 (lectura fallida) -- se reintenta en el proximo ciclo.");
		}
		else
		{
			tecnova.setValue("temperatura", temperatura);
			tecnova.setValue("humedad", humedad);
		}
	}

	delay(50);
}
