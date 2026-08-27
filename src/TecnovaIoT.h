// TecnovaIoT -- cliente para conectar un ESP32 a la plataforma IoT Tecnova.
//
// Se encarga de todo lo necesario para que un dispositivo hable con el
// panel: conectar WiFi, autenticarse por HTTPS contra el webhook del
// dispositivo, validar el certificado TLS del broker, conectar y mantener
// la sesión MQTT sobre WebSocket seguro (WSS), reconectar solo ante
// cortes, y mapear los topics del protocolo de Tecnova a nombres de
// variable legibles -- para que el código del proyecto solo tenga que
// preocuparse por leer sus sensores y reaccionar a comandos, no por MQTT
// ni TLS.
//
// Uso típico:
//
//   #include <TecnovaIoT.h>
//
//   TecnovaIoT tecnova("<dId>", "<password>");
//
//   void setup() {
//     Serial.begin(921600);
//     tecnova.onCommand("estado_led", [](JsonVariant valor) {
//       digitalWrite(LED_BUILTIN, valor["value"] == "true" ? HIGH : LOW);
//     });
//     tecnova.begin("<ssid_wifi>", "<password_wifi>");
//   }
//
//   void loop() {
//     tecnova.loop();
//     tecnova.setValue("temperatura", leerTemperatura());
//   }
//
// Ver examples/BasicSensor para un ejemplo completo, y
// examples/CaptivePortal si preferís cargar las credenciales desde un
// portal cautivo en vez de escribirlas en el código (ver
// TecnovaProvisioning.h y el README).
//
// NOTA PARA QUIEN LEA/MODIFIQUE ESTE CÓDIGO: adentro (ver la sección
// "private" más abajo) hay dos detalles de implementación que vale la
// pena entender, no solo copiar:
//   - _mutex: `mqtt_event_handler` (los callbacks de MQTT_EVENT_DATA,
//     MQTT_EVENT_CONNECTED, etc.) corren en una tarea de FreeRTOS
//     DISTINTA a la que ejecuta tu setup()/loop() -- el propio driver
//     esp_mqtt_client se maneja así internamente. Eso significa que dos
//     "hilos" distintos pueden intentar leer/escribir _variables al mismo
//     tiempo (uno cuando llega un mensaje, otro cuando loop() llama a
//     setValue() o al publicador automático). Sin protegerlo con un mutex,
//     eso puede corromper la memoria de forma intermitente y muy difícil
//     de reproducir. Por eso CUALQUIER acceso a _variables está envuelto
//     en xSemaphoreTake/xSemaphoreGive.
//   - _pendingCallbacks: cuando llamás a onCommand(), todavía no sabemos
//     qué variables tiene el dispositivo (eso se entera recién adentro de
//     begin(), al consultar el panel) -- por eso los callbacks se guardan
//     "pendientes" y se conectan con la Variable real más adelante.
//
// Repositorio: https://github.com/pipe1391/TecnovaIoT

#ifndef TecnovaIoT_h
#define TecnovaIoT_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Se invoca cuando llega desde el panel/app un comando dirigido a una
// variable con onCommand() registrado. "value" es el JSON completo
// recibido (típicamente algo con forma {"value": ...}).
typedef std::function<void(JsonVariant value)> TecnovaCommandCallback;

class TecnovaIoT
{
public:
	// deviceId / devicePassword: los mismos datos que se ven en
	// "Dispositivos" dentro del panel de Tecnova.
	// credentialsJsonCapacity: bytes reservados temporalmente para parsear
	// la respuesta del webhook (bytes de ArduinoJson, no de RAM total) --
	// solo hace falta subirlo si tu dispositivo tiene muchísimas variables
	// configuradas en el panel.
	TecnovaIoT(const String &deviceId, const String &devicePassword, size_t credentialsJsonCapacity = 4096);

	// Registra qué hacer cuando llega un comando para la variable llamada
	// "variableName" (el nombre configurado en el panel para esa variable,
	// no su id interno). Hay que llamarlo ANTES de begin().
	void onCommand(const String &variableName, TecnovaCommandCallback callback);

	// Conecta WiFi (si todavía no está conectado), pide las credenciales
	// MQTT al webhook del dispositivo, y abre la conexión MQTT. Es
	// bloqueante: si no logra conectar el WiFi o el webhook falla, reinicia
	// el ESP32 y reintenta desde cero (mismo comportamiento validado en
	// producción). Devuelve true si terminó de arrancar la conexión MQTT
	// (no espera a que esté conectada -- eso es asíncrono, usar
	// isConnected() para saberlo).
	bool begin(const char *wifiSsid, const char *wifiPassword);

	// Hay que llamarlo en cada vuelta de loop(). Publica automáticamente
	// las variables que ya cumplieron su intervalo de envío
	// (variableSendFreq configurado en el panel), y maneja la reconexión
	// si se cae el WiFi o el MQTT.
	void loop();

	// Actualiza el valor de una variable -- se publica solo, en su próximo
	// ciclo, respetando el intervalo configurado en el panel para esa
	// variable. "save" indica si el backend debe guardar este valor en el
	// historial. Devuelve false si "variableName" no existe entre las
	// variables que el panel devolvió para este dispositivo (revisar que el
	// nombre coincida exactamente con el configurado ahí).
	bool setValue(const String &variableName, JsonVariant value, bool save = false);
	bool setValue(const String &variableName, float value, bool save = false);
	bool setValue(const String &variableName, int value, bool save = false);
	bool setValue(const String &variableName, bool value, bool save = false);
	bool setValue(const String &variableName, const String &value, bool save = false);

	// true si la sesión MQTT está activa ahora mismo.
	bool isConnected() const;

	// Debug: imprime una tabla con el estado de cada variable (nombre, id,
	// tipo, mensajes procesados, último valor) y la RAM libre. No afecta la
	// lógica del dispositivo. Se puede llamar en cada loop() sin problema
	// -- tiene un throttle interno, no imprime más seguido que cada 2s.
	void printStats(Stream &out = Serial);

	// ---- Consumo de energía ----
	//
	// MQTT funciona por "empuje" (push): el broker manda el mensaje apenas
	// alguien publica, no hay forma de "pedirlo" después. Eso divide a los
	// dispositivos en dos familias, con estrategias de ahorro distintas:
	//
	//   - Los que SOLO publican (sensores) pueden dormir profundo entre
	//     lecturas -- no importa que estén "sordos" un rato, nadie les va
	//     a mandar nada. Usar deepSleepSeconds().
	//   - Los que RECIBEN comandos (actuadores) o son MIXTOS no pueden
	//     dormir profundo sin más: si les llega un comando mientras están
	//     dormidos, se pierde (esta librería usa QoS 0, sin cola en el
	//     broker). Usar enablePowerSave() en su lugar -- ahorra bastante
	//     menos, pero el dispositivo sigue alcanzable en todo momento.

	// Reduce el consumo del radio WiFi manteniendo la sesión MQTT viva --
	// para dispositivos que reciben comandos (onCommand) o son mixtos, y
	// por eso no pueden usar deepSleepSeconds(). El ahorro es más modesto
	// que un deep sleep, pero el dispositivo sigue recibiendo mensajes en
	// todo momento (con algo más de latencia). Llamar después de begin().
	void enablePowerSave();

	// Le da tiempo a lo que esté pendiente de publicarse para salir
	// realmente por la red, y apaga el ESP32 en modo deep sleep durante
	// "seconds" segundos. SOLO para dispositivos que nunca necesitan
	// recibir un comando (si este dispositivo tiene variables "output"
	// registradas con onCommand(), no uses esto -- ver la nota de arriba).
	// Al despertar, el ESP32 arranca de cero (vuelve a correr setup()); no
	// hay "vuelve del sleep", es indistinguible de un reset.
	void deepSleepSeconds(uint64_t seconds);

private:
	struct Variable
	{
		String id;             // id interno (el que se usa en los topics MQTT)
		String fullName;       // nombre legible (la clave que usan setValue()/onCommand())
		String type;           // "input" o "output"
		unsigned long sendFreqMs;
		unsigned long lastSendMs;
		String lastPayloadJson; // último valor, ya serializado (ej {"value":1,"save":0})
		unsigned long counter;  // mensajes procesados (recibidos o enviados) para esta variable
		TecnovaCommandCallback callback;
	};

	String _deviceId;
	String _devicePassword;
	size_t _credentialsJsonCapacity;

	String _mqttUsername;
	String _mqttPassword;
	String _topicPrefix; // "<userId>/<dId>/", tal cual lo devuelve el webhook
	String _subscribeTopic;

	std::vector<Variable> _variables;
	// callbacks de onCommand() registrados antes de begin(), pendientes de
	// asociar a una Variable real una vez que _fetchCredentials() trae la
	// lista de variables del panel (ahí recién se sabe qué variables existen).
	std::vector<std::pair<String, TecnovaCommandCallback>> _pendingCallbacks;
	SemaphoreHandle_t _mutex;

	esp_mqtt_client_handle_t _mqttClient;
	volatile bool _mqttConnected;
	unsigned long _mqttDisconnectedSinceMs; // 0 = nunca se conectó todavía

	String _lastReceivedTopic;
	String _lastReceivedMsg;
	unsigned long _lastStatsMs; // throttle interno de printStats(), independiente de cuánto la llame el usuario

	const char *_wifiSsid;
	const char *_wifiPassword;

	bool _connectWifi();
	bool _fetchCredentials();
	void _startMqtt();
	void _stopMqtt();
	void _publishDueVariables();
	void _handleIncomingMessage(const String &topic, const String &payload);
	int _findVariableIndexByName(const String &variableName) const; // requiere el mutex ya tomado
	int _findVariableIndexById(const String &variableId) const;     // requiere el mutex ya tomado
	bool _setValueByName(const String &variableName, const String &payloadJson);

	static void _staticMqttEventHandler(void *handlerArgs, esp_event_base_t base, int32_t eventId, void *eventData);
	void _handleMqttEvent(esp_mqtt_event_handle_t event);
};

#endif
