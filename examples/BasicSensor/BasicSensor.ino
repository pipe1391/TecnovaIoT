// Ejemplo minimo de TecnovaIoT: publica un sensor simulado y reacciona a un
// comando de actuador. Reemplaza los TODO con los datos de tu dispositivo
// (los ves en la seccion "Dispositivos" del panel) y con tu WiFi.
//
// Los nombres "temperatura" y "led" de abajo son solo ejemplo -- tienen que
// coincidir EXACTO con los nombres de variable que configuraste para este
// dispositivo en el panel.
//
// EN EL PANEL, al crear cada variable hay que contestar la pregunta "Que
// hace esta variable?":
//   - "temperatura" -> "El equipo la mide"   (se publica con setValue)
//   - "led"         -> "El panel la acciona" (se recibe con onCommand)
// Si "led" queda como "El equipo la mide" el comando igual va a llegar --
// el panel te deja ponerle el interruptor lo mismo -- pero esa variable no
// va a poder usarse en Automatizaciones, y la lista de Variables te va a
// mostrar una "frecuencia de envio" que en un actuador no significa nada.
// Marcala bien.

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
	// callback con el JSON recibido, que siempre tiene la forma
	// {"value": ...}.
	//
	// El panel manda el valor TIPADO, nunca como texto: para un LED se le
	// pone un INTERRUPTOR, que publica el booleano {"value":true} o
	// {"value":false}. Por eso se lee con .as<bool>().
	//
	// NO compares contra el texto "true" (value["value"] == "true"): para
	// ArduinoJson un booleano y ese texto son tipos distintos y nunca son
	// "iguales", asi que la comparacion da false siempre y el LED no
	// prende -- sin ningun error a la vista.
	//
	// Si en vez de un interruptor le pones un BOTON DE PULSO, vas a recibir
	// siempre {"value":true} y nunca vas a poder apagar: un boton no tiene
	// estado. Para encender y apagar, interruptor.
	tecnova.onCommand("led", [](JsonVariant value) {
		digitalWrite(LED_PIN, value["value"].as<bool>() ? HIGH : LOW);
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
