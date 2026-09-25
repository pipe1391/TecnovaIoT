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
// EN EL PANEL: hacen falta TRES variables, "rojo", "verde" y "azul". A
// cada una, en el formulario de Variables, hay que contestarle "El panel
// la acciona" a la pregunta "¿Qué hace esta variable?", y después ponerle
// en el panel un widget DESLIZADOR.
//
// OJO CON EL RANGO: el panel te ofrece el deslizador de 0 a 100. Hay que
// cambiarle el Máximo a 255, que es hasta donde llega analogWrite(). Si
// lo dejás en 100 el LED igual prende, pero al tope de la escala cada
// canal llega apenas a 100 de 255 -- como 39% de brillo -- así que los
// colores salen apagados y nunca vas a poder hacer un blanco. Eso NO lo
// puede arreglar el firmware: se arregla en el panel.
//
// POR QUÉ TRES DESLIZADORES Y NO UN SELECTOR DE COLOR: el panel tiene
// exactamente tres controles -- interruptor (manda un booleano), botón de
// pulso (manda siempre true) y deslizador (manda un número). No hay
// selector de color ni ningún control que mande texto, así que un
// {"value":"#FF8800"} no puede salir del panel. Hasta la versión 1.3.0
// este ejemplo esperaba justamente eso: compilaba, pero nadie podía
// accionarlo nunca. Con un deslizador por canal se enseña lo mismo (PWM y
// mezcla de color) y además funciona de verdad.

#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define PIN_R 25
#define PIN_G 26
#define PIN_B 27

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);

// Lee el número que manda un deslizador y lo deja en 0-255.
//
// El rango del widget se configura en el panel, así que podría venir mal
// puesto. Si quedó de 0 a 100 (el valor por defecto), vas a perder brillo
// -- eso se arregla en el panel, no acá. Si alguien lo puso por arriba de
// 255, constrain() evita que el valor dé la vuelta al pasarlo a uint8_t y
// termine encendiendo un color que nadie pidió.
//
// El valor se guarda en una variable intermedia porque constrain() es una
// macro y evaluaría su argumento más de una vez.
uint8_t canalDesdeComando(JsonVariant value)
{
	int v = value["value"].as<int>();
	return (uint8_t)constrain(v, 0, 255);
}

void setup()
{
	Serial.begin(921600);
	pinMode(PIN_R, OUTPUT);
	pinMode(PIN_G, OUTPUT);
	pinMode(PIN_B, OUTPUT);

	// Se registran ANTES de begin(). "rojo", "verde" y "azul" deben ser los
	// nombres EXACTOS de las tres variables configuradas en el panel.
	//
	// Llega un comando por canal, no los tres juntos: mover un deslizador
	// publica solo su variable y los otros dos canales se quedan como
	// estaban. Eso es lo que se quiere -- el color final es la mezcla.
	tecnova.onCommand("rojo", [](JsonVariant value) {
		uint8_t r = canalDesdeComando(value);
		analogWrite(PIN_R, r);
		Serial.printf("Canal rojo: %d\n", r);
	});

	tecnova.onCommand("verde", [](JsonVariant value) {
		uint8_t g = canalDesdeComando(value);
		analogWrite(PIN_G, g);
		Serial.printf("Canal verde: %d\n", g);
	});

	tecnova.onCommand("azul", [](JsonVariant value) {
		uint8_t b = canalDesdeComando(value);
		analogWrite(PIN_B, b);
		Serial.printf("Canal azul: %d\n", b);
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
	//
	// OJO: no llames a setValue("rojo", ...) para "informar" en qué quedó
	// el canal. Una variable de salida no se publica nunca; la librería te
	// lo avisa por el monitor serie la primera vez que lo intentás. Si
	// querés que el equipo reporte su estado, creá variables de entrada
	// aparte.
	tecnova.loop();
	delay(50);
}
