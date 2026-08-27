// Ejemplo: controlar un LED RGB como ACTUADOR (SALIDA) -- a diferencia de
// los ejemplos anteriores (que PUBLICAN lecturas de sensores), este
// REACCIONA a comandos que llegan desde el panel/app. Es el patrón que
// seguirías para cualquier salida: relé, motor, válvula, etc.
//
// No necesita ninguna librería externa además de TecnovaIoT -- se maneja
// con analogWrite(), que en el core de Arduino para ESP32 ya implementa
// PWM por vos (no hace falta configurar canales LEDC a mano).
//
// CONEXIÓN: LED RGB de 4 patas, un pin PWM del ESP32 por cada color (más
// la pata común a GND o VCC según sea de cátodo o ánodo común). Este
// ejemplo asume CÁTODO común (la pata larga va a GND) -- si el tuyo es de
// ÁNODO común (la pata larga va a VCC), invertí la lógica cambiando cada
// analogWrite(PIN, valor) por analogWrite(PIN, 255 - valor).
//
// EN EL PANEL: la variable "color_rgb" de este dispositivo tiene que
// existir y estar configurada como tipo "output". El valor que manda la
// app/panel se espera como texto hexadecimal de color, por ejemplo
// {"value":"#FF8800"} (el mismo formato que usa un selector de color de
// cualquier interfaz web).

#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define PIN_R 25
#define PIN_G 26
#define PIN_B 27

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);

// Convierte "#RRGGBB" (o "RRGGBB", sin el "#") a los 3 valores 0-255.
void hexAColor(const String &hex, uint8_t &r, uint8_t &g, uint8_t &b)
{
	String h = hex;
	if (h.startsWith("#"))
	{
		h.remove(0, 1);
	}
	if (h.length() < 6)
	{
		r = g = b = 0;
		return;
	}
	r = (uint8_t)strtol(h.substring(0, 2).c_str(), nullptr, 16);
	g = (uint8_t)strtol(h.substring(2, 4).c_str(), nullptr, 16);
	b = (uint8_t)strtol(h.substring(4, 6).c_str(), nullptr, 16);
}

void setup()
{
	Serial.begin(921600);
	pinMode(PIN_R, OUTPUT);
	pinMode(PIN_G, OUTPUT);
	pinMode(PIN_B, OUTPUT);

	// Se registra ANTES de begin(). "color_rgb" debe ser el nombre EXACTO
	// de la variable de tipo actuador configurada en el panel.
	tecnova.onCommand("color_rgb", [](JsonVariant value) {
		uint8_t r, g, b;
		hexAColor(value["value"].as<String>(), r, g, b);
		analogWrite(PIN_R, r);
		analogWrite(PIN_G, g);
		analogWrite(PIN_B, b);
		Serial.printf("Color aplicado: R=%d G=%d B=%d\n", r, g, b);
	});

	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);

	// Este dispositivo RECIBE comandos -- no puede dormir profundo (un
	// comando que llegue mientras duerme se perdería, ver README, sección
	// "Consumo de energía"). enablePowerSave() ahorra bastante menos que
	// un deep sleep, pero mantiene al dispositivo alcanzable en todo
	// momento -- es la opción correcta para actuadores y dispositivos
	// mixtos (que publican Y reciben).
	tecnova.enablePowerSave();
}

void loop()
{
	// Este ejemplo no publica nada -- solo reacciona a comandos, así que
	// alcanza con darle su ciclo a la librería para que reciba y reconecte.
	tecnova.loop();
	delay(50);
}
