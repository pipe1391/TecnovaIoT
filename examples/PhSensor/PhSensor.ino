// Ejemplo: medir pH con un sensor analógico de pH (por ejemplo el DFRobot
// Gravity pH Meter Kit / SEN0161, uno de los más comunes en el mercado --
// cualquier sensor de pH analógico con salida 0-3.3V funciona igual, solo
// cambia la calibración).
//
// No necesita ninguna librería externa además de TecnovaIoT -- se lee
// directo con analogRead(), sin dependencias extra.
//
// CONEXIÓN: el módulo de interfaz del sensor (la "placa azul" que viene
// con la sonda) se alimenta a 3.3V-5V, y su salida analógica va a un pin
// ADC del ESP32. Usá un pin "solo entrada" (34, 35, 36 o 39) si tu placa
// los tiene libres -- son los más limpios para leer sensores analógicos.
//
// CALIBRACIÓN (importante -- cada sonda viene con una curva propia y NO
// vas a tener valores correctos sin hacer esto):
//   1. Sumergí la sonda en una solución buffer de pH 7.0 conocida, esperá
//      que se estabilice, y anotá qué valor da analogRead(PH_SENSOR_PIN).
//   2. Repetí con una solución buffer de pH 4.0.
//   3. Convertí esas dos lecturas a voltaje (lectura * 3.3 / 4095.0) y
//      reemplazá PH7_VOLTAGE / PH4_VOLTAGE más abajo con tus valores
//      reales -- los que están puestos ahora son solo un ejemplo.

#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define PH_SENSOR_PIN 34

// Voltaje que midió TU sonda en cada solución buffer -- ver "CALIBRACIÓN"
// más arriba. Estos son valores de ejemplo, hay que reemplazarlos.
const float PH7_VOLTAGE = 1.65;
const float PH4_VOLTAGE = 2.03;

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);

float prevPh = 0;
unsigned long lastRead = 0;

float leerPh()
{
	int lectura = analogRead(PH_SENSOR_PIN);
	float voltaje = lectura * (3.3 / 4095.0);

	// Recta que pasa por los dos puntos de calibración (pH 7 y pH 4) --
	// "pendiente" es cuánto cambia el pH por cada voltio.
	float pendiente = (7.0 - 4.0) / (PH7_VOLTAGE - PH4_VOLTAGE);
	float ph = pendiente * (voltaje - PH7_VOLTAGE) + 7.0;
	return ph;
}

void setup()
{
	Serial.begin(921600);
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
		// Fluctuaciones chicas de pH son normales (ruido del sensor); solo
		// pedimos guardar en el historial si el cambio es real.
		bool guardar = fabs(ph - prevPh) >= 0.3;
		tecnova.setValue("ph", ph, guardar);
		prevPh = ph;
	}

	delay(50);
}
