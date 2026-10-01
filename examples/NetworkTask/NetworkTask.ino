// Ejemplo: TecnovaIoT en su propia tarea de FreeRTOS, sin reinicios.
// PARA QUE SIRVE: en un equipo con pantalla loop() tiene que correr todo el
// tiempo (dibujar, leer el tactil). Si ahi adentro la libreria espera al WiFi
// o reinicia el ESP32, la pantalla se congela o se apaga. La receta:
//   1. tecnova.setAutoRestart(false): nunca reinicia; lo que falle se
//      reintenta solo, con esperas que crecen.
//   2. begin() y loop() de la libreria van en una tarea propia (nucleo 0).
//   3. Tu loop() usa solo funciones seguras entre tareas: setValue(),
//      sendNow(), getState(), isConnected().
// EN EL PANEL: dos variables "El equipo la mide": "temperatura" y "nivel".
// CONEXION: un potenciometro en el GPIO 34 (hace de deslizador de pantalla).
// Con portal cautivo: TecnovaProvisioning::setAutoRestart(false) y su begin()
// tambien van adentro de la tarea (ver README, "Dispositivos con pantalla").
#include <TecnovaIoT.h>

const char *WIFI_SSID = "TODO_nombre_de_tu_red";
const char *WIFI_PASSWORD = "TODO_password_de_tu_red";
const char *DEVICE_ID = "TODO_dId_de_un_dispositivo_real";
const char *DEVICE_PASSWORD = "TODO_password_de_ese_dispositivo";
#define POT_PIN 34

TecnovaIoT tecnova(DEVICE_ID, DEVICE_PASSWORD);

// Todo lo lento (WiFi, HTTPS, MQTT) pasa aca, nunca en loop()
void tareaRed(void *parametro)
{
	tecnova.begin(WIFI_SSID, WIFI_PASSWORD); // sin reinicio: vuelve enseguida
	for (;;)
	{
		tecnova.loop();                // a veces tarda unos segundos (HTTPS): por eso va aca
		vTaskDelay(pdMS_TO_TICKS(20)); // cede el nucleo: asi corren el WiFi y el vigilante (watchdog)
	}
}

void setup()
{
	Serial.begin(921600);
	tecnova.setAutoRestart(false); // ANTES de begin()
	// 12 KB de pila (HTTPS, y el portal si lo usas). Nucleo 0, el del WiFi:
	// el 1 queda para tu loop() y la pantalla.
	xTaskCreatePinnedToCore(tareaRed, "red", 12288, NULL, 1, NULL, 0);
}

TecnovaState estadoAnterior = TECNOVA_IDLE;
int nivelEnviado = -1;
unsigned long ultimaTemperatura = 0;

void loop()
{
	TecnovaState estado = tecnova.getState(); // se puede leer desde cualquier tarea
	if (estado != estadoAnterior)
	{
		Serial.printf("[ejemplo] Estado: %s\n", TecnovaIoT::stateName(estado));
		estadoAnterior = estado;
	}

	int nivel = (int)(analogRead(POT_PIN) * 100L / 4095);
	// setValue() da false hasta tener credenciales: se reintenta solo en la proxima vuelta
	if (abs(nivel - nivelEnviado) >= 2 && tecnova.setValue("nivel", nivel))
	{
		tecnova.sendNow("nivel"); // sale ya (como mucho uno cada 250 ms); el ultimo siempre sale
		nivelEnviado = nivel;
	}

	if (millis() - ultimaTemperatura > 2000) // un sensor comun: sale a la frecuencia del panel
	{
		ultimaTemperatura = millis();
		tecnova.setValue("temperatura", 20.0f + random(0, 100) / 10.0f);
	}

	delay(20); // en un equipo con pantalla, aca iria lv_timer_handler()
}
