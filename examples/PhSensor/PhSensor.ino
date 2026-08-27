// Ejemplo: medir pH con un sensor analógico de pH (por ejemplo el DFRobot
// Gravity pH Meter Kit / SEN0161, uno de los más comunes en el mercado --
// cualquier sensor de pH analógico funciona igual, solo cambia la
// calibración) leído a través de un ADS1115 (ADC externo por I2C), en vez
// de leerlo directo con analogRead() del ESP32.
//
// ¿POR QUÉ UN ADC EXTERNO Y NO analogRead() DIRECTO? Dos motivos reales,
// no es capricho:
//
//   1. CONFLICTO CON WIFI: la mitad de los pines analógicos del ESP32
//      (los del "ADC2": GPIO 0, 2, 4, 12-15, 25-27) comparten circuitería
//      con el radio WiFi. Mientras el WiFi está activo -- que en un
//      dispositivo TecnovaIoT es siempre -- esos pines dan lecturas
//      erráticas o directamente fallan. Los del "ADC1" (GPIO 32-39) sí
//      son seguros con WiFi, pero eso nos lleva al segundo problema:
//
//   2. RESOLUCIÓN Y RUIDO: una sonda de pH cambia su voltaje muy poco por
//      unidad de pH (~59mV, según la ecuación de Nernst). El ADC interno
//      del ESP32 es de 12 bits y tiene no-linealidad conocida -- en la
//      práctica, el ruido puede mover la lectura ±0.2/0.3 de pH sin que
//      el agua haya cambiado en absoluto. Para un dato que se va a usar
//      de verdad (por ejemplo, monitoreo de cultivos), eso no alcanza.
//
// El ADS1115 es un ADC de 16 bits que vive AFUERA del ESP32 y se lee por
// I2C: no compite con el WiFi por hardware, y tiene muchísima más
// resolución real.
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
//   ADS1115  SCL  -> GPIO22 (I2C por defecto en la mayoría de las placas ESP32 devkit)
//   ADS1115  SDA  -> GPIO21
//   ADS1115  ADDR -> GND (asi queda en la direccion I2C 0x48, la de fabrica)
//   Salida analógica del sensor de pH -> ADS1115 canal A0
//
// CALIBRACIÓN (igual de importante que antes -- cada sonda viene con una
// curva propia y NO vas a tener valores correctos sin hacer esto):
//   1. Sumergí la sonda en una solución buffer de pH 7.0 conocida, esperá
//      que se estabilice, y anotá qué voltaje da ads.computeVolts(...)
//      para el canal A0 (podés imprimirlo por Serial para verlo).
//   2. Repetí con una solución buffer de pH 4.0.
//   3. Reemplazá PH7_VOLTAGE / PH4_VOLTAGE más abajo con tus valores
//      reales -- los que están puestos ahora son solo un ejemplo.

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define PH_ADS1115_CHANNEL 0

// Voltaje que midió TU sonda en cada solución buffer -- ver "CALIBRACIÓN"
// más arriba. Estos son valores de ejemplo, hay que reemplazarlos.
const float PH7_VOLTAGE = 1.65;
const float PH4_VOLTAGE = 2.03;

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);
Adafruit_ADS1115 ads;

float prevPh = 0;
unsigned long lastRead = 0;

float leerPh()
{
	int16_t lectura = ads.readADC_SingleEnded(PH_ADS1115_CHANNEL);
	float voltaje = ads.computeVolts(lectura);

	// Recta que pasa por los dos puntos de calibración (pH 7 y pH 4) --
	// "pendiente" es cuánto cambia el pH por cada voltio.
	float pendiente = (7.0 - 4.0) / (PH7_VOLTAGE - PH4_VOLTAGE);
	float ph = pendiente * (voltaje - PH7_VOLTAGE) + 7.0;
	return ph;
}

void setup()
{
	Serial.begin(921600);

	if (!ads.begin())
	{
		Serial.println("No se encontro el ADS1115 -- revisa el cableado I2C (SDA/SCL) y la direccion (ADDR->GND = 0x48).");
	}
	// +-4.096V de rango: suficiente margen para los 0-3V tipicos de una
	// sonda de pH, con mejor resolucion que el rango por defecto (+-6.144V).
	ads.setGain(GAIN_ONE);

	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop()
{
	tecnova.loop();

	// El pH cambia lento -- no hace falta leerlo más seguido que cada 5s.
	if (millis() - lastRead > 5000)
	{
		lastRead = millis();
		float ph = leerPh();
		// Fluctuaciones chicas de pH son normales (ruido residual); solo
		// pedimos guardar en el historial si el cambio es real.
		bool guardar = fabs(ph - prevPh) >= 0.3;
		tecnova.setValue("ph", ph, guardar);
		prevPh = ph;
	}

	delay(50);
}
