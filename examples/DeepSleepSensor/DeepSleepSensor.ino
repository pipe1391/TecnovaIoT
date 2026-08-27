// Ejemplo: dispositivo a batería que SOLO publica (nunca recibe
// comandos) -- se conecta, mide, publica, y se apaga en deep sleep hasta
// el próximo ciclo. Es el patrón correcto para sensores remotos a
// batería (por ejemplo, un sensor de humedad de suelo en el campo).
//
// ¿POR QUÉ ESTO NO SIRVE PARA UN ACTUADOR? MQTT funciona por "empuje": el
// broker manda el mensaje apenas alguien publica, no hay forma de
// "pedirlo" después. Si este dispositivo tuviera una variable "output"
// (algo que reciba comandos con onCommand()), cualquier comando mandado
// mientras está dormido se PIERDE -- no queda esperando a que despierte.
// Por eso este patrón es solo para dispositivos que jamás necesitan
// recibir nada. Si tu dispositivo es un actuador o mixto, mirá el
// ejemplo RGBLed en cambio (usa tecnova.enablePowerSave() -- ahorra
// menos, pero se mantiene alcanzable).
//
// IMPORTANTE: para el ESP32, despertar de un deep sleep es indistinguible
// de un reinicio -- vuelve a correr setup() desde cero, reconectando WiFi
// y MQTT cada vez. Eso tiene un costo real de tiempo y batería por ciclo
// (unos segundos), así que este patrón conviene para intervalos de
// MINUTOS, no de segundos -- para publicar muy seguido, usá el patrón
// normal (ver examples/BasicSensor) sin dormir.

#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";

const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";       // "Dispositivos" en el panel
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo"; // idem

const uint64_t SLEEP_SECONDS = 5 * 60; // dormir 5 minutos entre lecturas

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);

void setup()
{
	Serial.begin(921600);

	// begin() es bloqueante: conecta WiFi, pide credenciales y conecta
	// MQTT antes de devolver el control -- por eso alcanza con hacer todo
	// el trabajo acá mismo en setup(), no hace falta loop().
	tecnova.begin(WIFI_SSID, WIFI_PASSWORD);

	float temperaturaC = 20.0 + random(0, 100) / 10.0; // acá iría tu lectura real
	tecnova.setValue("temperatura", temperaturaC);

	// setValue() solo deja el dato listo -- hace falta darle vueltas de
	// loop() a la librería para que realmente lo publique.
	unsigned long inicio = millis();
	while (millis() - inicio < 8000)
	{
		tecnova.loop();
		delay(50);
	}

	// A partir de acá el ESP32 se apaga -- esta función no vuelve.
	tecnova.deepSleepSeconds(SLEEP_SECONDS);
}

void loop()
{
	// No se usa: todo el trabajo de este dispositivo pasa una sola vez en
	// setup() por cada ciclo de despertar -- este loop() nunca llega a
	// ejecutarse (setup() termina en deepSleepSeconds(), que apaga el chip).
}
