// Ejemplo: cargar las credenciales de WiFi y del dispositivo Tecnova desde
// un portal cautivo, en vez de escribirlas a mano en el codigo.
//
// A DIFERENCIA del ejemplo BasicSensor (que necesita que edites el codigo
// para poner tu SSID/password), este NO tiene ningun dato hardcodeado --
// toda la configuracion se hace la primera vez que se enciende el
// dispositivo, desde el celular, sin volver a programarlo.
//
// REQUISITO: agregar tzapu/WiFiManager a tus lib_deps (ver
// lib/TecnovaIoT/README.md, seccion "Portal cautivo (TecnovaProvisioning)").
//
// COMO PROBARLO:
//   1. Programa el ESP32 con este sketch y abri el Monitor Serie (921600 baud).
//   2. La primera vez, no va a encontrar credenciales guardadas y va a
//      crear su propia red WiFi "TecnovaIoT-Setup".
//   3. Desde tu celular, conectate a esa red (sin contraseña). Deberia
//      abrirse solo un formulario (si no, abri un navegador a 192.168.4.1).
//   4. Completa: la red WiFi de destino + su contraseña, y el "ID del
//      dispositivo (dId)" + password que ves en la seccion "Dispositivos"
//      del panel Tecnova.
//   5. Al guardar, el ESP32 se conecta solo y arranca a publicar datos.
//   6. Para volver a cambiar la configuracion mas adelante: con el
//      dispositivo ya prendido y funcionando, mantene apretado el boton
//      BOOT 3 segundos -- se reinicia solo y vuelve a mostrar el portal.

#include <TecnovaIoT.h>
#include <TecnovaProvisioning.h>

#define LED_PIN 12

TecnovaIoT *tecnova = nullptr;

int prevTemp = 0;
unsigned long lastRead = 0;

void setup()
{
	Serial.begin(921600);
	pinMode(LED_PIN, OUTPUT);

	// Se registra ANTES de crear TecnovaIoT: "led" debe ser el nombre
	// EXACTO de una variable de tipo actuador configurada para este
	// dispositivo en el panel.
	String wifiSsid, wifiPassword, deviceId, devicePassword;
	TecnovaProvisioning::begin(wifiSsid, wifiPassword, deviceId, devicePassword);

	// Recien ACA sabemos el dId/password del dispositivo -- por eso
	// TecnovaIoT se crea despues de la llamada anterior, no antes.
	tecnova = new TecnovaIoT(deviceId, devicePassword);

	tecnova->onCommand("led", [](JsonVariant value) {
		digitalWrite(LED_PIN, value["value"] == "true" ? HIGH : LOW);
	});

	// El WiFi ya quedo conectado por TecnovaProvisioning::begin() -- este
	// begin() lo detecta solo y no vuelve a intentar conectarse.
	tecnova->begin(wifiSsid.c_str(), wifiPassword.c_str());
}

void loop()
{
	// Mantener BOOT 3s con el dispositivo ya funcionando reabre el portal.
	TecnovaProvisioning::checkReconfigureButton();

	tecnova->loop();

	if (millis() - lastRead > 2000)
	{
		lastRead = millis();
		float temperaturaC = 20.0 + random(0, 100) / 10.0;
		tecnova->setValue("temperatura", temperaturaC);
	}

	delay(50);
}
