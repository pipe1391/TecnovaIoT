// Ejemplo minimo de TecnovaIoT: publica un sensor simulado y reacciona a un
// comando de actuador. Reemplaza los TODO con los datos de tu dispositivo
// (los ves en la seccion "Dispositivos" del panel) y con tu WiFi.
//
// Los nombres "temperatura" y "led" de abajo son solo ejemplo -- tienen que
// coincidir EXACTO con los nombres de variable que configuraste para este
// dispositivo en el panel.

#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

#define LED_PIN 12

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);

void setup()
{
	Serial.begin(921600);
	pinMode(LED_PIN, OUTPUT);

	// Se registra ANTES de begin(): cuando llegue un comando para la
	// variable "led" (el nombre configurado en el panel), se llama a este
	// callback con el JSON recibido -- puede llegar como booleano nativo
	// ({"value":true}) o como texto ({"value":"true"}) segun como este
	// configurada la variable en el panel. Comparar solo contra el string
	// "true" falla en silencio si llega un booleano real (son tipos
	// distintos para ArduinoJson, nunca son "iguales" aunque representen
	// lo mismo) -- por eso se contemplan los dos casos.
	tecnova.onCommand("led", [](JsonVariant value) {
		JsonVariant v = value["value"];
		bool encender = v.is<bool>() ? v.as<bool>() : (v.as<String>() == "true");
		digitalWrite(LED_PIN, encender ? HIGH : LOW);
	});

	// Conecta WiFi, pide credenciales al panel, y abre la conexión MQTT.
	// Bloqueante -- reinicia el ESP32 solo si algo falla.
	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
}

unsigned long lastRead = 0;

void loop()
{
	// Hay que llamarlo siempre: publica lo que corresponda y reconecta
	// automáticamente si se corta el WiFi o el MQTT.
	tecnova.loop();

	// Simula una lectura de sensor cada 2s. setValue() no publica al
	// instante -- deja el valor listo para que loop() lo mande respetando
	// la frecuencia de envío configurada en el panel para "temperatura".
	if (millis() - lastRead > 2000)
	{
		lastRead = millis();
		float temperaturaC = 20.0 + random(0, 100) / 10.0;
		tecnova.setValue("temperatura", temperaturaC);
	}

	if (tecnova.isConnected())
	{
		tecnova.printStats(); // opcional, solo debug
	}

	delay(50);
}
