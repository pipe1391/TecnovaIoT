// Ejemplo: leer temperatura, humedad y presión atmosférica con un sensor
// BME280 (I2C) -- un sensor combinado bastante más preciso que el DHT11,
// muy usado en estaciones meteorológicas caseras.
//
// REQUIERE agregar a tu platformio.ini (o instalar desde el Gestor de
// Librerías del IDE de Arduino, buscando "Adafruit BME280"):
//
//   lib_deps =
//       adafruit/Adafruit BME280 Library@^2.2.4
//       adafruit/Adafruit Unified Sensor@^1.1.14
//
// CONEXIÓN (I2C): la mayoría de los módulos BME280 se conectan igual --
//   VCC -> 3.3V
//   GND -> GND
//   SCL -> GPIO22 (I2C por defecto en la mayoría de las placas ESP32 devkit)
//   SDA -> GPIO21
//
// La dirección I2C del módulo suele ser 0x76 o 0x77 según el fabricante
// -- si bme.begin(0x76) falla, probá con 0x77.

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define BME280_I2C_ADDRESS 0x76

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);
Adafruit_BME280 bme;

unsigned long lastRead = 0;

void setup()
{
	Serial.begin(921600);

	if (!bme.begin(BME280_I2C_ADDRESS))
	{
		Serial.println("No se encontro el BME280 -- revisa el cableado I2C (SDA/SCL) y la direccion (0x76/0x77).");
	}

	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop()
{
	tecnova.loop();

	if (millis() - lastRead > 10000)
	{
		lastRead = millis();

		// Los nombres "temperatura"/"humedad"/"presion" tienen que coincidir
		// EXACTO con los que configuraste para este dispositivo en el panel.
		tecnova.setValue("temperatura", bme.readTemperature());        // en °C
		tecnova.setValue("humedad", bme.readHumidity());               // en % HR
		tecnova.setValue("presion", bme.readPressure() / 100.0F);      // Pa -> hPa
	}

	delay(50);
}
