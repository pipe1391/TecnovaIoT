// Ejemplo genérico: leer los 4 canales analógicos de un ADS1115 (ADC
// externo de 16 bits por I2C) y publicarlos como 4 variables.
//
// Este ejemplo NO es específico de ningún sensor en particular -- sirve
// como plantilla para CUALQUIER sensor que dé una salida analógica (0-Vcc)
// y donde el ADC interno del ESP32 no alcance. Ver también
// examples/PhSensor, que usa exactamente este mismo ADC para un caso
// concreto (una sonda de pH) y explica en detalle POR QUÉ conviene un ADC
// externo en vez de analogRead() del ESP32 (resumen: conflicto con WiFi
// en la mitad de los pines analógicos, y resolución/ruido insuficiente
// para señales chicas).
//
// REQUIERE agregar a tu platformio.ini (o instalar desde el Gestor de
// Librerías del IDE de Arduino, buscando "Adafruit ADS1X15"):
//
//   lib_deps =
//       adafruit/Adafruit ADS1X15@^2.4.0
//
// CONEXIÓN:
//   ADS1115  VDD  -> 3.3V
//   ADS1115  GND  -> GND
//   ADS1115  SCL  -> GPIO22
//   ADS1115  SDA  -> GPIO21
//   ADS1115  ADDR -> GND (direccion I2C 0x48, la de fabrica -- si conectas
//            ADDR a VDD/SDA/SCL en vez de GND, la direccion cambia; ver la
//            hoja de datos del modulo si necesitas mas de un ADS1115 en el
//            mismo bus I2C)
//   Hasta 4 sensores analógicos -> canales A0, A1, A2, A3
//
// Los nombres de variable "canal_a0".."canal_a3" son solo de ejemplo --
// en un proyecto real, renombralos según qué sensor tengas en cada canal
// (y agregá/sacá canales según cuántos uses).

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);
Adafruit_ADS1115 ads;

unsigned long lastRead = 0;

void setup()
{
	Serial.begin(921600);

	if (!ads.begin())
	{
		Serial.println("No se encontro el ADS1115 -- revisa el cableado I2C (SDA/SCL) y la direccion (ADDR->GND = 0x48).");
	}
	// +-4.096V de rango -- ajustar segun el voltaje de salida real de tus
	// sensores (ver los valores GAIN_* en Adafruit_ADS1X15.h de la libreria).
	ads.setGain(GAIN_ONE);

	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop()
{
	tecnova.loop();

	if (millis() - lastRead > 5000)
	{
		lastRead = millis();

		// readADC_SingleEnded(n) mide el voltaje del canal An respecto a
		// GND. computeVolts() lo convierte de cuentas crudas del ADC a
		// voltios reales, ya usando la ganancia configurada arriba.
		float voltajeA0 = ads.computeVolts(ads.readADC_SingleEnded(0));
		float voltajeA1 = ads.computeVolts(ads.readADC_SingleEnded(1));
		float voltajeA2 = ads.computeVolts(ads.readADC_SingleEnded(2));
		float voltajeA3 = ads.computeVolts(ads.readADC_SingleEnded(3));

		tecnova.setValue("canal_a0", voltajeA0);
		tecnova.setValue("canal_a1", voltajeA1);
		tecnova.setValue("canal_a2", voltajeA2);
		tecnova.setValue("canal_a3", voltajeA3);
	}

	delay(50);
}
