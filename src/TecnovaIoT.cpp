#include "TecnovaIoT.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_crt_bundle.h"
#include "esp_idf_version.h"
#include "esp_sleep.h"
#include "TecnovaRootCaBundle.h"

// En arduino-esp32 2.x (ESP-IDF 4.4) el bundle se maneja con la copia que
// trae Arduino (arduino_esp_crt_bundle_*); en 3.x (ESP-IDF 5) esa copia no
// existe y se usa la de ESP-IDF (esp_crt_bundle_*). Mismo criterio que ya
// elige el struct del MQTT en _startMqtt(): ESP_IDF_VERSION_MAJOR.
// (Ojo: siempre #if, nunca #ifdef sobre una comparación.)
#if ESP_IDF_VERSION_MAJOR >= 5
#define TECNOVA_CRT_BUNDLE_ATTACH esp_crt_bundle_attach
#else
#define TECNOVA_CRT_BUNDLE_ATTACH arduino_esp_crt_bundle_attach
#endif

// ============================================================================
// REGLAS ENTRE TAREAS (invariantes) -- leelas antes de tocar este archivo
// ============================================================================
// En esta librería corren al menos dos tareas de FreeRTOS a la vez: la que
// llama a begin()/loop() (tu loop(), o una tarea propia en el modo sin
// reinicio) y la tarea interna de esp-mqtt (mqtt_task), que entrega los
// eventos y los comandos. Además, setValue(), sendNow() y getState() se
// pueden llamar desde cualquier otra. Todo el diseño se sostiene en estas
// cinco reglas; si un cambio rompe alguna, aparecen cuelgues o memoria
// corrupta que no se reproducen a voluntad:
//
//   I1. Solo la tarea de begin()/loop() escribe _state, _mqttClient, los
//       relojes (_stateSinceMs, _wifiLostAtMs, ...) y la espera
//       anti-tormenta del webhook. mqtt_task solo escribe _mqttConnected,
//       _mqttDisconnectedSinceMs y _mqttAuthRefused, que son de 1 o 4 bytes
//       y alineados: se leen enteros desde cualquier tarea, sin candado.
//   I2. _fetchCredentials() solo corre con _mqttClient == NULL, en los dos
//       modos: en begin() todavía no hay cliente, y en loop() antes se
//       llama a _stopMqtt(). Es por dos cosas: el pedido HTTPS vuelve a
//       cargar el bundle de CA, que es GLOBAL (si la tarea del MQTT
//       estuviera validando un certificado con él, leería memoria ya
//       liberada), y así nunca conviven dos sesiones TLS (unos 50 KB cada
//       una, mucho para un ESP32 sin PSRAM).
//   I3. En el modo sin reinicio ningún pedido al webhook sale antes de que
//       venza la espera anti-tormenta (_fetchGateStartMs/_fetchGateMs): ver
//       _fetchIfAllowed(), que es el único camino.
//   I4. Nunca se llama a esp-mqtt con _mutex tomado (ver
//       _publishDueVariables(): hacerlo provocaba un deadlock).
//   I5. _stopMqtt() y _fetchCredentials() nunca se llaman desde un
//       callback de onCommand(): ahí corre mqtt_task, y esp-mqtt no se
//       puede detener a sí mismo.

namespace
{
	const uint8_t _OBF_KEY = 0x5a;

	String _deobfuscate(const uint8_t *data, size_t len)
	{
		String out;
		out.reserve(len);
		for (size_t i = 0; i < len; i++)
		{
			out += (char)(data[i] ^ _OBF_KEY);
		}
		return out;
	}

	// Endpoint del webhook de credenciales (XOR con _OBF_KEY).
	const uint8_t _WEBHOOK_ENDPOINT_OBF[] PROGMEM = {
		0x32, 0x2e, 0x2e, 0x2a, 0x29, 0x60, 0x75, 0x75, 0x2a, 0x3b, 0x34, 0x3f, 0x36, 0x74, 0x39, 0x3f,
		0x3f, 0x2e, 0x3f, 0x39, 0x34, 0x35, 0x2c, 0x3b, 0x74, 0x39, 0x35, 0x37, 0x75, 0x3b, 0x2a, 0x33,
		0x75, 0x3d, 0x3f, 0x2e, 0x3e, 0x3f, 0x2c, 0x33, 0x39, 0x3f, 0x39, 0x28, 0x3f, 0x3e, 0x3f, 0x34,
		0x2e, 0x33, 0x3b, 0x36, 0x29};

	// URI del broker MQTT (XOR con _OBF_KEY).
	const uint8_t _MQTT_URI_OBF[] PROGMEM = {
		0x2d, 0x29, 0x29, 0x60, 0x75, 0x75, 0x2a, 0x3b, 0x34, 0x3f, 0x36, 0x74, 0x39, 0x3f, 0x3f, 0x2e,
		0x3f, 0x39, 0x34, 0x35, 0x2c, 0x3b, 0x74, 0x39, 0x35, 0x37, 0x75, 0x37, 0x2b, 0x2e, 0x2e};

	String _webhookEndpoint()
	{
		return _deobfuscate(_WEBHOOK_ENDPOINT_OBF, sizeof(_WEBHOOK_ENDPOINT_OBF));
	}

	String _mqttUri()
	{
		return _deobfuscate(_MQTT_URI_OBF, sizeof(_MQTT_URI_OBF));
	}

	// El body del pedido de credenciales va como
	// application/x-www-form-urlencoded, y en ese formato hay caracteres que
	// significan otra cosa: el servidor lee "+" como un espacio, "&" como el
	// comienzo de otro campo y "%41" como una "A". Hasta la 1.4.0 el dId y el
	// password se mandaban tal cual, así que una clave como "clave+2026" o
	// "a&b" llegaba cambiada y el panel la rechazaba aunque fuera la
	// correcta. Acá todo lo que no sea letra, número o - _ . ~ viaja como
	// %XX, que el servidor vuelve a convertir en el carácter original. Para
	// una clave de solo letras y números los bytes que salen son exactamente
	// los mismos que antes, y cualquier clave que ya funcionaba le sigue
	// llegando igual al servidor.
	String _formEncode(const String &text)
	{
		static const char HEX_DIGITS[] = "0123456789ABCDEF";
		String out;
		out.reserve(text.length() * 3);
		for (size_t i = 0; i < text.length(); i++)
		{
			uint8_t c = (uint8_t)text[i];
			bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
						 c == '-' || c == '_' || c == '.' || c == '~';
			if (plain)
			{
				out += (char)c;
			}
			else
			{
				out += '%';
				out += HEX_DIGITS[c >> 4];
				out += HEX_DIGITS[c & 0x0F];
			}
		}
		return out;
	}

	const unsigned long WIFI_RETRY_DELAY_MS = 500;
	const int WIFI_MAX_RETRIES = 10;
	const unsigned long CREDENTIALS_RETRY_DELAY_MS = 10000;
	const unsigned long MQTT_RECONNECT_TIMEOUT_MS = 30000;
	// Si el panel no trae variableSendFreq (o viene en 0), se usa este piso
	// para no floodear el broker publicando en cada vuelta de loop().
	const unsigned long MIN_SEND_FREQ_MS = 1000;

	const unsigned long SEND_NOW_SPACING_MS = 250;         // sendNow(), en los dos modos
	// Solo con setAutoRestart(false):
	const unsigned long WIFI_KICK_FIRST_MS = 15000;        // primer empujón al WiFi (antes reintenta el núcleo solo)
	const unsigned long WIFI_KICK_MAX_MS = 60000;          // 15 -> 30 -> 60 -> 60 s entre empujones
	const unsigned long WIFI_DROP_MQTT_MS = 10000;         // corte de WiFi tras el cual la sesión MQTT se da por muerta
	const unsigned long FETCH_RETRY_MIN_MS = 5000;         // espera del webhook: 5, 10, 20, 40, 80, 120 s...
	const unsigned long FETCH_RETRY_MAX_MS = 120000;
	const unsigned long FETCH_REJECTED_MS = 300000;        // tras un 401/403/404
	const unsigned long WEBHOOK_CONNECT_TIMEOUT_MS = 5000; // HTTPClient::setConnectTimeout (ms en 2.x y 3.x)
	const unsigned long WEBHOOK_READ_TIMEOUT_MS = 5000;    // HTTPClient::setTimeout (ms en 2.x y 3.x)
	const unsigned long WEBHOOK_HANDSHAKE_TIMEOUT_S = 10;  // WiFiClientSecure::setHandshakeTimeout (segundos en 2.x y 3.x)
	const int MQTT_KEEPALIVE_NO_RESTART_S = 30;            // keepalive MQTT (esp-mqtt usa 120 s si no se le dice nada)
	// Acá no se usan min()/max() de Arduino (su tipo cambia entre núcleos:
	// van ternarios) ni WiFiClientSecure::setTimeout(), que está en segundos
	// en 2.x y en milisegundos en 3.x.

#if ESP_IDF_VERSION_MAJOR >= 5 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
	// Formato viejo (2.x e IDF <= 5.3): [cantidad u16 BE] y por cada
	// certificado [largo nombre u16 BE][largo clave u16 BE][nombre][clave].
	// Formato nuevo (IDF >= 5.4): [offset u32 LE] x cantidad y después los
	// mismos certificados, contiguos, con los largos en u16 LE. Se conserva el
	// orden (por nombre): lo necesita la búsqueda binaria de ESP-IDF.
	// Con el formato viejo, esp_crt_bundle_set() lo rechaza SIN AVISAR y el
	// ESP32 valida contra otro bundle: por eso se traduce.
	size_t _convertBundleToIdf54(const uint8_t *in, size_t inLen, uint8_t *out, size_t outCap)
	{
		if (inLen < 2)
		{
			return 0;
		}
		uint16_t n = (uint16_t)((in[0] << 8) | in[1]);
		size_t outLen = inLen - 2 + 4 * (size_t)n;
		if (n == 0 || outLen > outCap)
		{
			return 0;
		}
		size_t pin = 2, pout = 4 * (size_t)n;
		for (uint16_t i = 0; i < n; i++)
		{
			if (pin + 4 > inLen)
			{
				return 0;
			}
			uint16_t nameLen = (uint16_t)((in[pin] << 8) | in[pin + 1]);
			uint16_t keyLen = (uint16_t)((in[pin + 2] << 8) | in[pin + 3]);
			size_t certLen = 4 + (size_t)nameLen + keyLen;
			if (pin + certLen > inLen)
			{
				return 0;
			}
			for (int b = 0; b < 4; b++)
			{
				out[4 * i + b] = (uint8_t)(pout >> (8 * b)); // offset LE
			}
			out[pout] = (uint8_t)(nameLen & 0xff);
			out[pout + 1] = (uint8_t)(nameLen >> 8);
			out[pout + 2] = (uint8_t)(keyLen & 0xff);
			out[pout + 3] = (uint8_t)(keyLen >> 8);
			memcpy(out + pout + 4, in + pin + 4, (size_t)nameLen + keyLen);
			pin += certLen;
			pout += certLen;
		}
		return (pin == inLen) ? pout : 0;
	}
#endif

#if ESP_IDF_VERSION_MAJOR >= 5
	// El bundle en el formato que espera este ESP-IDF (len = bytes; NULL si
	// falló la traducción). En 2.x no hace falta: ahí se usa el arreglo tal
	// cual (y sin este #if el compilador avisaría que la función no se usa).
	const uint8_t *_rootCaBundle(size_t &len)
	{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
		// Desde IDF 5.4 esp_crt_bundle_set() guarda el PUNTERO (no copia): el
		// arreglo tiene que vivir siempre (static) y alineado a 4 (tabla u32).
		static uint8_t converted[sizeof(TECNOVA_ROOT_CA_BUNDLE) + 64] __attribute__((aligned(4)));
		static size_t convertedLen = 0;
		if (convertedLen == 0)
		{
			convertedLen = _convertBundleToIdf54(TECNOVA_ROOT_CA_BUNDLE, sizeof(TECNOVA_ROOT_CA_BUNDLE), converted, sizeof(converted));
		}
		len = convertedLen;
		return convertedLen > 0 ? converted : NULL;
#else
		len = sizeof(TECNOVA_ROOT_CA_BUNDLE);
		return TECNOVA_ROOT_CA_BUNDLE;
#endif
	}
#endif

	// Deja cargado el bundle de la librería para todo el ESP32 (es estado
	// GLOBAL). Solo con el MQTT detenido: cambiarlo mientras la tarea del MQTT
	// valida un certificado leería memoria ya liberada.
	void _installRootCaBundle()
	{
#if ESP_IDF_VERSION_MAJOR >= 5
		size_t len = 0;
		const uint8_t *bundle = _rootCaBundle(len);
		if (bundle == NULL || esp_crt_bundle_set(bundle, len) != ESP_OK)
		{
			Serial.println("[TecnovaIoT] ERROR: no se pudo cargar el paquete de CA raiz de la libreria.");
		}
#else
		arduino_esp_crt_bundle_set(TECNOVA_ROOT_CA_BUNDLE); // exactamente la llamada de la 1.4.0
#endif
	}

	// Hace que "client" valide el certificado del servidor contra ese bundle.
	void _useRootCaBundle(WiFiClientSecure &client)
	{
#if ESP_IDF_VERSION_MAJOR >= 5
		size_t len = 0;
		const uint8_t *bundle = _rootCaBundle(len);
		if (bundle == NULL)
		{
			return; // sin CA el cliente no conecta: falla cerrado, nunca inseguro
		}
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 4)
		client.setCACertBundle(bundle, len);
#else
		// 3.0.0 a 3.0.3: setCACertBundle() no recibía el tamaño y pasaba
		// sizeof(puntero). ESP-IDF lo rechaza antes de tocar nada y queda el
		// bundle que cargó _installRootCaBundle(); lo que importa es que marca
		// al cliente para validar con el bundle.
		client.setCACertBundle(bundle);
#endif
#else
		client.setCACertBundle(TECNOVA_ROOT_CA_BUNDLE); // 2.x: recarga el mismo bundle global
#endif
	}
}

TecnovaIoT::TecnovaIoT(const String &deviceId, const String &devicePassword, size_t credentialsJsonCapacity)
	: _deviceId(deviceId),
	  _devicePassword(devicePassword),
	  _credentialsJsonCapacity(credentialsJsonCapacity),
	  _mqttClient(NULL),
	  _mqttConnected(false),
	  _mqttDisconnectedSinceMs(0),
	  _lastStatsMs(0),
	  _wifiSsid(NULL),
	  _wifiPassword(NULL),
	  _autoRestart(true),
	  _state(TECNOVA_IDLE),
	  _stateSinceMs(0),
	  _wifiLostAtMs(0),
	  _wifiKickAtMs(0),
	  _wifiKickEveryMs(WIFI_KICK_FIRST_MS),
	  _mqttDropped(false),
	  _fetchGateStartMs(0),
	  _fetchGateMs(0),
	  _fetchBackoffMs(0),
	  _waitState(TECNOVA_FETCHING_CREDENTIALS),
	  _mqttAuthRefused(false)
{
	_mutex = xSemaphoreCreateMutex();
}

void TecnovaIoT::onCommand(const String &variableName, TecnovaCommandCallback callback)
{
	_pendingCallbacks.push_back(std::make_pair(variableName, callback));
}

bool TecnovaIoT::begin(const char *wifiSsid, const char *wifiPassword)
{
	if (!_autoRestart)
	{
		return _beginNoRestart(wifiSsid, wifiPassword);
	}

	// ---- De acá para abajo: el comportamiento de siempre (1.4.0) ----
	_wifiSsid = wifiSsid;
	_wifiPassword = wifiPassword;

	// Sin esto, arduino_esp_crt_bundle_attach() en _startMqtt() falla con
	// "Failed to attach bundle" -- nunca hay certificados cargados por
	// default, hay que setearlos a mano una vez. (En 2.x es exactamente la
	// misma llamada de siempre; ver _installRootCaBundle().)
	_installRootCaBundle();

	_setState(TECNOVA_WIFI_CONNECTING);
	if (!_connectWifi())
	{
		return false; // no debería llegar acá: _connectWifi() reinicia solo si falla
	}

	_setState(TECNOVA_FETCHING_CREDENTIALS);
	FetchResult result = _fetchCredentials();
	if (result != FETCH_OK)
	{
		_setState(result == FETCH_REJECTED ? TECNOVA_CREDENTIALS_REJECTED : TECNOVA_SERVER_UNAVAILABLE);
		Serial.println("[TecnovaIoT] No se pudieron obtener las credenciales del panel. Reiniciando en 10s...");
		delay(CREDENTIALS_RETRY_DELAY_MS);
		ESP.restart();
		return false;
	}

	_startMqtt();
	_setState(TECNOVA_MQTT_CONNECTING);
	return true;
}

bool TecnovaIoT::_beginNoRestart(const char *wifiSsid, const char *wifiPassword)
{
	if (_state != TECNOVA_IDLE)
	{
		Serial.println("[TecnovaIoT] Aviso: begin() ya se habia llamado -- se ignora (no se crea otro cliente MQTT).");
		return false;
	}

	// Copias propias: quien llama suele pasar el c_str() de un String local
	// (por ejemplo, el que llenó TecnovaProvisioning::begin()), y acá los
	// datos se siguen usando mucho después, cada vez que hay que volver a
	// empujar al WiFi. Un puntero a un String ya destruido apunta a basura.
	_wifiSsidCopy = wifiSsid ? wifiSsid : "";
	_wifiPasswordCopy = wifiPassword ? wifiPassword : "";

	_installRootCaBundle();
	_waitState = TECNOVA_FETCHING_CREDENTIALS;

	if (WiFi.status() == WL_CONNECTED)
	{
		_setState(TECNOVA_FETCHING_CREDENTIALS); // loop() pide las credenciales en su primera vuelta
		return true;
	}

	_beginWifi(); // arranca a conectar y vuelve: no espera
	unsigned long now = millis();
	_wifiLostAtMs = now;
	_wifiKickAtMs = now;
	_wifiKickEveryMs = WIFI_KICK_FIRST_MS;
	_mqttDropped = false;
	_setState(TECNOVA_WIFI_CONNECTING);
	return true;
}

void TecnovaIoT::_beginWifi()
{
	Serial.println("[TecnovaIoT] Conectando WiFi (sin esperar)...");
	if (_wifiSsidCopy.length() > 0)
	{
		WiFi.begin(_wifiSsidCopy.c_str(), _wifiPasswordCopy.c_str());
	}
	else
	{
		WiFi.begin(); // sin SSID: la red que ya tiene guardada el ESP32
	}
}

void TecnovaIoT::setAutoRestart(bool enabled)
{
	if (_state != TECNOVA_IDLE)
	{
		Serial.println("[TecnovaIoT] Aviso: setAutoRestart() va ANTES de begin(); se ignora.");
		return;
	}
	_autoRestart = enabled;
}

void TecnovaIoT::loop()
{
	if (!_autoRestart)
	{
		_loopNoRestart();
		return;
	}

	// ---- De acá para abajo: el comportamiento de siempre (1.4.0) ----
	if (WiFi.status() != WL_CONNECTED)
	{
		// En este modo _setState() no imprime nada: solo evita que
		// getState(), leído desde otra tarea, diga "conectado" durante los
		// 15 s que faltan para el reinicio.
		_setState(TECNOVA_WIFI_CONNECTING);
		Serial.println("[TecnovaIoT] Se perdio la conexion WiFi. Reiniciando...");
		delay(15000);
		ESP.restart();
	}

	if (_mqttConnected)
	{
		_publishDueVariables();
		return;
	}

	// Si lleva más de 30s sin poder reconectar solo, puede ser que las
	// credenciales quedaron viejas (rotaron del lado del servidor) -- se
	// piden de nuevo y se reinicia el cliente MQTT con las nuevas. El
	// reintento de bajo nivel (red cortada un instante, etc.) ya lo maneja
	// esp_mqtt_client internamente, no hace falta hacerlo acá.
	if (_mqttDisconnectedSinceMs != 0 && millis() - _mqttDisconnectedSinceMs > MQTT_RECONNECT_TIMEOUT_MS)
	{
		Serial.println("[TecnovaIoT] 30s sin conexion MQTT -- pidiendo credenciales de nuevo...");
		_stopMqtt();
		_setState(TECNOVA_FETCHING_CREDENTIALS);
		FetchResult result = _fetchCredentials();
		if (result == FETCH_OK)
		{
			_startMqtt();
			// Rearmar el reloj: el cliente nuevo necesita SUS propios 30 s
			// para conectarse.
			//
			// Sin esta línea la marca se quedaba en la hora de la primera
			// desconexión (solo se pone en cero en MQTT_EVENT_CONNECTED, y
			// MQTT_EVENT_DISCONNECTED solo la escribe si vale 0), así que
			// la condición de arriba seguía siendo verdadera en CADA vuelta
			// de loop(). Con el delay(50) de los ejemplos, eso destruía con
			// esp_mqtt_client_destroy() un cliente que llevaba 50 ms de
			// negociación TLS+WSS -- que no termina en 50 ms -- y lanzaba
			// un POST HTTPS al webhook de credenciales por vuelta. Un corte
			// de red de más de 30 s en el liceo se convertía en una
			// tormenta de pedidos contra la API, y el equipo solo se
			// recuperaba de casualidad.
			_mqttDisconnectedSinceMs = millis();
			_setState(TECNOVA_MQTT_CONNECTING);
		}
		else
		{
			_setState(result == FETCH_REJECTED ? TECNOVA_CREDENTIALS_REJECTED : TECNOVA_SERVER_UNAVAILABLE);
			Serial.println("[TecnovaIoT] Error obteniendo credenciales. Reiniciando en 10s...");
			delay(CREDENTIALS_RETRY_DELAY_MS);
			ESP.restart();
		}
	}
}

bool TecnovaIoT::isConnected() const
{
	return _mqttConnected;
}

bool TecnovaIoT::setValue(const String &variableName, JsonVariant value, bool save)
{
	StaticJsonDocument<192> doc;
	doc["value"] = value;
	doc["save"] = save ? 1 : 0;
	String payload;
	serializeJson(doc, payload);
	return _setValueByName(variableName, payload);
}

bool TecnovaIoT::setValue(const String &variableName, float value, bool save)
{
	StaticJsonDocument<96> doc;
	doc["value"] = value;
	doc["save"] = save ? 1 : 0;
	String payload;
	serializeJson(doc, payload);
	return _setValueByName(variableName, payload);
}

bool TecnovaIoT::setValue(const String &variableName, int value, bool save)
{
	StaticJsonDocument<96> doc;
	doc["value"] = value;
	doc["save"] = save ? 1 : 0;
	String payload;
	serializeJson(doc, payload);
	return _setValueByName(variableName, payload);
}

bool TecnovaIoT::setValue(const String &variableName, bool value, bool save)
{
	StaticJsonDocument<96> doc;
	doc["value"] = value;
	doc["save"] = save ? 1 : 0;
	String payload;
	serializeJson(doc, payload);
	return _setValueByName(variableName, payload);
}

bool TecnovaIoT::setValue(const String &variableName, const String &value, bool save)
{
	StaticJsonDocument<192> doc;
	doc["value"] = value;
	doc["save"] = save ? 1 : 0;
	String payload;
	serializeJson(doc, payload);
	return _setValueByName(variableName, payload);
}

void TecnovaIoT::printStats(Stream &out)
{
	unsigned long now = millis();
	if (now - _lastStatsMs < 2000)
	{
		return;
	}
	_lastStatsMs = now;

	out.println();
	out.println("----------------------------");
	out.println("     TECNOVA IOT STATS");
	out.println("----------------------------");
	out.printf("%-3s %-16s %-14s %-7s %-7s %s\n", "#", "Name", "Var", "Type", "Count", "Last V");

	// _lastReceivedMsg lo escribe la tarea del MQTT cada vez que llega un
	// comando: se copia con el candado tomado (junto con la tabla) y se
	// imprime después. Leerlo sin candado mientras se reasigna podía leer
	// memoria que el String acababa de liberar.
	String lastReceivedMsg;

	xSemaphoreTake(_mutex, portMAX_DELAY);
	for (size_t i = 0; i < _variables.size(); i++)
	{
		out.printf("%-3d %-16s %-14s %-7s %-7lu %s\n",
				   (int)i,
				   _variables[i].fullName.c_str(),
				   _variables[i].id.c_str(),
				   _variables[i].type.c_str(),
				   _variables[i].counter,
				   _variables[i].lastPayloadJson.c_str());
	}
	lastReceivedMsg = _lastReceivedMsg;
	xSemaphoreGive(_mutex);

	out.printf("\nFree RAM -> %u bytes\n", ESP.getFreeHeap());
	out.printf("Last incoming msg -> %s\n", lastReceivedMsg.c_str());
}

void TecnovaIoT::enablePowerSave()
{
	// WIFI_PS_MIN_MODEM: el radio WiFi se apaga entre "beacons" (los
	// paquetes periódicos que manda el router) y se prende justo para
	// escucharlos, en vez de estar recibiendo todo el tiempo. El access
	// point bufferea lo que llegue mientras tanto y lo entrega en el
	// próximo beacon -- por eso hay algo más de latencia, pero la sesión
	// MQTT sigue viva y el dispositivo sigue recibiendo comandos.
	WiFi.setSleep(WIFI_PS_MIN_MODEM);
	Serial.println("[TecnovaIoT] Power save activado (WiFi modem-sleep) -- el dispositivo sigue alcanzable.");
}

void TecnovaIoT::deepSleepSeconds(uint64_t seconds)
{
	// esp_mqtt_client_publish() con QoS 0 es "fire and forget": la
	// llamada vuelve apenas el dato se encola para enviar, no espera a
	// que salga de verdad por el aire. Si apagáramos el WiFi al toque,
	// una publicación reciente podría quedar a mitad de camino. Este
	// delay es una espera prudencial, no una garantía perfecta -- para
	// una confirmación real haría falta QoS 1 (no soportado todavía).
	delay(500);

	Serial.printf("[TecnovaIoT] Entrando en deep sleep por %llu s...\n", (unsigned long long)seconds);
	Serial.flush();

	esp_sleep_enable_timer_wakeup(seconds * 1000000ULL); // la API espera microsegundos
	esp_deep_sleep_start();
	// No hay código después de esta línea: esp_deep_sleep_start() nunca
	// retorna. Al despertar, el ESP32 arranca de cero -- es indistinguible
	// de un reset, vuelve a correr setup() desde el principio.
}

TecnovaState TecnovaIoT::getState() const
{
	// _state lo escribe solo la tarea de begin()/loop(); _mqttConnected, la
	// tarea del MQTT. Combinarlos acá hace que el estado cambie apenas llega
	// el evento del MQTT, sin esperar a loop(), y sirve en los dos modos.
	TecnovaState s = (TecnovaState)_state;
	if (s == TECNOVA_MQTT_CONNECTING && _mqttConnected)
	{
		return TECNOVA_CONNECTED;
	}
	if (s == TECNOVA_CONNECTED && !_mqttConnected)
	{
		return TECNOVA_MQTT_CONNECTING;
	}
	return s;
}

const char *TecnovaIoT::stateName(TecnovaState state)
{
	// Sin tildes a propósito: estos textos suelen terminar en una pantalla,
	// y las fuentes chicas de los displays no las traen.
	switch (state)
	{
	case TECNOVA_IDLE:
		return "sin iniciar";
	case TECNOVA_WIFI_CONNECTING:
		return "conectando WiFi";
	case TECNOVA_FETCHING_CREDENTIALS:
		return "pidiendo credenciales";
	case TECNOVA_CREDENTIALS_REJECTED:
		return "credenciales rechazadas";
	case TECNOVA_SERVER_UNAVAILABLE:
		return "servidor no disponible";
	case TECNOVA_MQTT_CONNECTING:
		return "conectando MQTT";
	case TECNOVA_CONNECTED:
		return "conectado";
	default:
		return "desconocido";
	}
}

bool TecnovaIoT::sendNow(const String &variableName)
{
	// Mismo criterio que _setValueByName(): se decide con el mutex tomado y
	// se imprime afuera. Acá tampoco se publica: solo se levanta la marca
	// sendRequested, y la cumple _publishDueVariables() desde loop() -- así
	// quien llama (la tarea de la pantalla, un onCommand()) nunca espera a
	// la red, y el espaciado de 250 ms lo controla un solo lugar.
	bool avisarSalida = false;
	bool requested = false;

	xSemaphoreTake(_mutex, portMAX_DELAY);
	int idx = _findVariableIndexByName(variableName);
	if (idx >= 0)
	{
		if (_variables[idx].type == "output")
		{
			// Una variable de salida no se publica nunca (ver
			// _publishDueVariables()): mismo aviso que setValue(), y
			// comparten la marca para no repetirlo.
			if (!_variables[idx].warnedOutputSetValue)
			{
				_variables[idx].warnedOutputSetValue = true;
				avisarSalida = true;
			}
		}
		else
		{
			_variables[idx].sendRequested = true;
			requested = true;
		}
	}
	xSemaphoreGive(_mutex);

	if (avisarSalida)
	{
		Serial.printf(
			"[TecnovaIoT] Aviso: sendNow(\"%s\") no publica nada. En el panel esa variable esta marcada como \"El panel la acciona\" (salida): se recibe con onCommand(), no se envia. Si queres que el equipo informe su estado, crea una variable de entrada aparte.\n",
			variableName.c_str());
	}

	return requested;
}

// ---- privado ----

void TecnovaIoT::_setState(TecnovaState state)
{
	if (_state == state)
	{
		return;
	}
	_state = state;
	if (!_autoRestart) // en el modo por defecto la salida serie queda igual que en la 1.4.0
	{
		Serial.printf("[TecnovaIoT] Estado: %s\n", stateName(state));
	}
}

void TecnovaIoT::_loopNoRestart()
{
	if (_state == TECNOVA_IDLE)
	{
		return; // sin begin() no hay nada que hacer
	}
	// Todos los relojes se comparan con resta sin signo (now - marca): sin
	// reinicios, el equipo puede pasar los 49,7 días en que millis() da la
	// vuelta, y la resta sigue dando bien.
	unsigned long now = millis();

	// 1) Sin WiFi. El núcleo del ESP32 reintenta solo (WiFi.setAutoReconnect);
	//    esto es un respaldo por si se rinde (por ejemplo, AUTH_FAIL).
	if (WiFi.status() != WL_CONNECTED)
	{
		if (_state != TECNOVA_WIFI_CONNECTING)
		{
			_wifiLostAtMs = now;
			_wifiKickAtMs = now;
			_wifiKickEveryMs = WIFI_KICK_FIRST_MS;
			_mqttDropped = false;
			_setState(TECNOVA_WIFI_CONNECTING);
			return;
		}
		// Si el corte dura, la sesión MQTT vieja se da por muerta: sin esto,
		// esp-mqtt recién lo nota por su keepalive (ver _startMqtt(): hasta
		// casi un minuto) y la pantalla diría "conectado" publicando al
		// vacío. No espera a la red:
		// solo le deja un aviso a la tarea del MQTT, que después reintenta
		// sola cuando vuelva el WiFi.
		if (_mqttClient != NULL && !_mqttDropped && now - _wifiLostAtMs >= WIFI_DROP_MQTT_MS)
		{
			esp_mqtt_client_disconnect(_mqttClient);
			_mqttDropped = true;
		}
		if (now - _wifiKickAtMs >= _wifiKickEveryMs)
		{
			Serial.println("[TecnovaIoT] Sin WiFi -- reintentando la conexion (sin reiniciar)...");
			if (!WiFi.reconnect())
			{
				_beginWifi(); // reconnect() no reescribe la configuración guardada; si falla, de cero
			}
			_wifiKickAtMs = now;
			_wifiKickEveryMs = (_wifiKickEveryMs * 2 > WIFI_KICK_MAX_MS) ? WIFI_KICK_MAX_MS : _wifiKickEveryMs * 2;
		}
		return;
	}

	// 2) Volvió (o llegó) el WiFi
	if (_state == TECNOVA_WIFI_CONNECTING)
	{
		Serial.print("[TecnovaIoT] WiFi conectado, IP: ");
		Serial.println(WiFi.localIP());
		if (_mqttClient != NULL)
		{
			_stateSinceMs = now; // esp-mqtt reconecta solo con sus credenciales: se le dan 30 s
			_setState(TECNOVA_MQTT_CONNECTING);
		}
		else
		{
			_setState((TecnovaState)_waitState); // lo que estaba esperando antes del corte
		}
		return;
	}

	// 3) Con WiFi: cada estado sabe qué le toca
	switch ((TecnovaState)_state)
	{
	case TECNOVA_FETCHING_CREDENTIALS:
	case TECNOVA_CREDENTIALS_REJECTED:
	case TECNOVA_SERVER_UNAVAILABLE:
		_fetchIfAllowed();
		break;

	case TECNOVA_MQTT_CONNECTING:
		if (_mqttConnected)
		{
			_fetchBackoffMs = 0; // sesión lograda: la próxima falla empieza de nuevo en 5 s
			_setState(TECNOVA_CONNECTED);
			break;
		}
		// El broker rechazó usuario/clave (credenciales rotadas): se piden ya.
		// Si no, se le dan 30 s a esp-mqtt, que reintenta cada 10 s.
		if (_mqttAuthRefused || now - _stateSinceMs >= MQTT_RECONNECT_TIMEOUT_MS)
		{
			_fetchIfAllowed(); // si la espera anti-tormenta no venció, sigue esperando
		}
		break;

	case TECNOVA_CONNECTED:
		if (!_mqttConnected)
		{
			_stateSinceMs = now;
			_setState(TECNOVA_MQTT_CONNECTING);
			break;
		}
		_publishDueVariables(); // incluye los pedidos de sendNow()
		break;

	default:
		break;
	}
}

void TecnovaIoT::_fetchIfAllowed()
{
	if (millis() - _fetchGateStartMs < _fetchGateMs)
	{
		return; // la espera anti-tormenta no venció: todavía no toca (I3)
	}

	if (_mqttClient != NULL)
	{
		// Invariante I2: el webhook solo con el MQTT detenido (bundle global
		// y un solo TLS a la vez). _stopMqtt() es el de la 1.4.0: stop +
		// destroy; puede tardar unos segundos si esp-mqtt estaba a mitad de
		// un intento.
		Serial.println("[TecnovaIoT] Cerrando el cliente MQTT para pedir credenciales nuevas...");
		_stopMqtt();
	}

	_setState(TECNOVA_FETCHING_CREDENTIALS);
	_mqttAuthRefused = false;
	FetchResult result = _fetchCredentials();

	// La espera se duplica en cada pedido que no terminó en una sesión MQTT
	// (vuelve a 0 al conectar): ningún camino puede pedir más seguido. El
	// azar (hasta +20 %) evita que los equipos de un aula que se recuperan
	// del mismo corte le pregunten al panel todos en el mismo segundo.
	_fetchBackoffMs = (_fetchBackoffMs == 0) ? FETCH_RETRY_MIN_MS
					  : ((_fetchBackoffMs * 2 > FETCH_RETRY_MAX_MS) ? FETCH_RETRY_MAX_MS : _fetchBackoffMs * 2);
	unsigned long wait = (result == FETCH_REJECTED) ? FETCH_REJECTED_MS : _fetchBackoffMs;
	_fetchGateStartMs = millis();
	_fetchGateMs = wait + (unsigned long)random(0, (long)(wait / 5 + 1));

	if (result == FETCH_OK)
	{
		_startMqtt();
		_stateSinceMs = millis();
		_waitState = TECNOVA_FETCHING_CREDENTIALS;
		_setState(TECNOVA_MQTT_CONNECTING);
		return;
	}

	_waitState = (result == FETCH_REJECTED) ? TECNOVA_CREDENTIALS_REJECTED : TECNOVA_SERVER_UNAVAILABLE;
	_setState((TecnovaState)_waitState);
	Serial.printf("[TecnovaIoT] Nuevo intento en %lu s (sin reiniciar)\n", _fetchGateMs / 1000);
}

bool TecnovaIoT::_connectWifi()
{
	if (WiFi.status() == WL_CONNECTED)
	{
		return true;
	}

	Serial.println("[TecnovaIoT] Conectando WiFi...");
	WiFi.begin(_wifiSsid, _wifiPassword);

	int attempts = 0;
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(WIFI_RETRY_DELAY_MS);
		Serial.print(".");
		attempts++;
		if (attempts > WIFI_MAX_RETRIES)
		{
			Serial.println("\n[TecnovaIoT] No se pudo conectar el WiFi. Reiniciando...");
			delay(2000);
			ESP.restart();
			return false;
		}
	}

	Serial.print("\n[TecnovaIoT] WiFi conectado, IP: ");
	Serial.println(WiFi.localIP());
	return true;
}

TecnovaIoT::FetchResult TecnovaIoT::_fetchCredentials()
{
	Serial.println("[TecnovaIoT] Pidiendo credenciales al panel...");

	String body = "dId=" + _formEncode(_deviceId) + "&password=" + _formEncode(_devicePassword); // ver _formEncode()

	// NOTA DE SEGURIDAD: este pedido valida el certificado del servidor con
	// el mismo paquete de CA raíz que la conexión MQTT (ver
	// _useRootCaBundle() y _startMqtt()). Es el mismo host y el mismo
	// certificado que el del broker, así que si el MQTT valida, esto
	// también. Hasta la 1.4.0 se usaba setInsecure(), y eso tenía un costo
	// real: el body lleva el dId y el password del dispositivo, y la
	// respuesta, el usuario y la clave MQTT. Cualquiera en la misma red (el
	// WiFi de un aula, por ejemplo) podía hacerse pasar por el servidor y
	// quedarse con todo. Validando, esa suplantación falla en el saludo TLS
	// y no sale nada.
	// Ojo: cargar el bundle toca estado GLOBAL, por eso esta función solo
	// corre con el MQTT detenido (invariante I2, al principio del archivo).
	WiFiClientSecure httpsClient;
	_useRootCaBundle(httpsClient);
	HTTPClient http;
	http.begin(httpsClient, _webhookEndpoint());
	if (!_autoRestart)
	{
		// Sin reinicios, loop() no puede quedar colgado de un servidor que
		// no contesta: se acota cada etapa del pedido. En el modo por
		// defecto quedan los valores de siempre del núcleo.
		http.setConnectTimeout(WEBHOOK_CONNECT_TIMEOUT_MS);
		http.setTimeout(WEBHOOK_READ_TIMEOUT_MS);
		httpsClient.setHandshakeTimeout(WEBHOOK_HANDSHAKE_TIMEOUT_S);
	}
	http.addHeader("Content-Type", "application/x-www-form-urlencoded");
	int responseCode = http.POST(body);

	if (responseCode != 200)
	{
		Serial.printf("[TecnovaIoT] Error del webhook: HTTP %d\n", responseCode);
		if (responseCode < 0)
		{
			// Un código negativo es que ni siquiera hubo respuesta HTTP. Si la
			// causa fue el TLS (por ejemplo, un certificado que no valida),
			// mbedTLS la deja anotada en el cliente: sin esto, el único
			// síntoma sería un "HTTP -1" que no dice nada. Qué devuelve
			// lastError():
			//   - 0: ni se llegó a conectar (por ejemplo, falló el DNS);
			//   - -1: lo usa el propio cliente para las fallas de RED (el TCP
			//     no conecta, o se agotó el tiempo de la conexión o del
			//     saludo TLS). mbedTLS lo traduciría como "Generic error", que
			//     suena a problema de certificado y no lo es: no se muestra;
			//   - otro negativo: un error real de mbedTLS, que sí se muestra;
			//   - positivo: la conexión TLS había salido bien (es el número de
			//     socket) y no hay nada que mostrar.
			char detail[100];
			int tlsError = httpsClient.lastError(detail, sizeof(detail));
			if (tlsError < 0 && tlsError != -1)
			{
				Serial.printf("[TecnovaIoT] Detalle TLS: %s\n", detail); // causa de mbedTLS; no contiene secretos
			}
		}
		http.end();
		// 401/403/404: el panel contestó, y lo que dice es que este dId o
		// este password no valen. Cualquier otra cosa (red, certificado,
		// error del servidor) puede arreglarse sola con el tiempo.
		return (responseCode == 401 || responseCode == 403 || responseCode == 404) ? FETCH_REJECTED : FETCH_FAILED;
	}

	String responseBody = http.getString();
	http.end();

	DynamicJsonDocument doc(_credentialsJsonCapacity);
	DeserializationError err = deserializeJson(doc, responseBody);
	if (err)
	{
		Serial.printf("[TecnovaIoT] Respuesta del webhook invalida: %s\n", err.c_str());
		return FETCH_FAILED;
	}

	xSemaphoreTake(_mutex, portMAX_DELAY);

	_mqttUsername = doc["username"].as<String>();
	_mqttPassword = doc["password"].as<String>();
	_topicPrefix = doc["topic"].as<String>();
	_subscribeTopic = _topicPrefix + "+/acdata";

	// En el modo sin reinicio este pedido se repite sin que el programa
	// vuelva a empezar: lo que la pantalla (u otra tarea) ya cargó con
	// setValue() o sendNow() no se puede perder por el camino. Se guardan
	// las variables viejas para copiar esos datos más abajo. En el modo por
	// defecto se descartan, como siempre.
	std::vector<Variable> previous;
	if (!_autoRestart)
	{
		previous.swap(_variables);
	}
	else
	{
		_variables.clear();
	}
	JsonArray variablesArray = doc["variables"].as<JsonArray>();
	for (JsonVariant v : variablesArray)
	{
		Variable variable;
		variable.id = v["variable"].as<String>();
		variable.fullName = v["variableFullName"].as<String>();
		variable.type = v["variableType"].as<String>();
		variable.sendFreqMs = (unsigned long)v["variableSendFreq"].as<String>().toInt() * 1000UL;
		if (variable.sendFreqMs == 0)
		{
			variable.sendFreqMs = MIN_SEND_FREQ_MS;
		}
		variable.lastSendMs = 0;
		variable.counter = 0;
		variable.warnedOutputSetValue = false;
		variable.sendRequested = false;
		_variables.push_back(variable);
	}

	if (!_autoRestart)
	{
		// Se busca por id (el que no cambia aunque renombren la variable en
		// el panel). lastSendMs queda en 0 a propósito: el último valor sale
		// apenas conecte el MQTT nuevo.
		for (auto &variable : _variables)
		{
			for (auto &old : previous)
			{
				if (old.id == variable.id)
				{
					variable.lastPayloadJson = old.lastPayloadJson;
					variable.sendRequested = old.sendRequested;
					break;
				}
			}
		}
	}

	// Asocia los callbacks que se registraron con onCommand() antes de
	// begin() (todavía no existían las variables reales en ese momento) a
	// las variables que el panel acaba de devolver.
	for (auto &pending : _pendingCallbacks)
	{
		int idx = _findVariableIndexByName(pending.first);
		if (idx >= 0)
		{
			_variables[idx].callback = pending.second;
		}
		else
		{
			Serial.printf("[TecnovaIoT] Aviso: onCommand(\"%s\") no coincide con ninguna variable del panel para este dispositivo\n", pending.first.c_str());
		}
	}

	xSemaphoreGive(_mutex);

	Serial.printf("[TecnovaIoT] Credenciales obtenidas: %u variable(s)\n", (unsigned)_variables.size());
	return FETCH_OK;
}

void TecnovaIoT::_startMqtt()
{
	String clientId = "device_" + _deviceId + "_" + String(random(1, 9999));
	String mqttUri = _mqttUri();

	Serial.println("[TecnovaIoT] Conectando MQTT (WSS)...");

	esp_mqtt_client_config_t cfg = {};
	// El struct de configuración de esp_mqtt_client cambió de forma entre
	// ESP-IDF 4.x (plano: cfg.uri, cfg.username, ...) y 5.x (anidado:
	// cfg.broker.address.uri, cfg.credentials.username, ...). Se detecta
	// automáticamente con la versión de IDF que trae el core instalado, en
	// vez de asumir una sola variante -- así la librería compila igual con
	// arduino-esp32 2.x (IDF4) o 3.x (IDF5). Lo mismo con la función que
	// engancha el bundle de CA: ver TECNOVA_CRT_BUNDLE_ATTACH, arriba.
#if ESP_IDF_VERSION_MAJOR >= 5
	cfg.broker.address.uri = mqttUri.c_str();
	cfg.broker.verification.crt_bundle_attach = TECNOVA_CRT_BUNDLE_ATTACH;
	cfg.credentials.username = _mqttUsername.c_str();
	cfg.credentials.authentication.password = _mqttPassword.c_str();
	cfg.credentials.client_id = clientId.c_str();
#else
	cfg.uri = mqttUri.c_str();
	cfg.crt_bundle_attach = TECNOVA_CRT_BUNDLE_ATTACH;
	cfg.username = _mqttUsername.c_str();
	cfg.password = _mqttPassword.c_str();
	cfg.client_id = clientId.c_str();
#endif

	if (!_autoRestart)
	{
		// Si se corta Internet pero el WiFi sigue andando, nada avisa: la
		// sesión MQTT sigue "conectada" hasta que esp-mqtt no recibe la
		// respuesta a su ping (keepalive). Con los 120 s de siempre eso
		// podía tardar dos minutos o más, con la pantalla diciendo
		// "conectado" y publicando al vacío. Con 30 s se nota en menos de un
		// minuto, a cambio de un ping cada 15 s (unos pocos bytes). En el
		// modo por defecto queda el valor de siempre.
#if ESP_IDF_VERSION_MAJOR >= 5
		cfg.session.keepalive = MQTT_KEEPALIVE_NO_RESTART_S;
#else
		cfg.keepalive = MQTT_KEEPALIVE_NO_RESTART_S;
#endif
	}

	_mqttClient = esp_mqtt_client_init(&cfg);
	esp_mqtt_client_register_event(_mqttClient, MQTT_EVENT_ANY, TecnovaIoT::_staticMqttEventHandler, this);
	esp_mqtt_client_start(_mqttClient);
}

void TecnovaIoT::_stopMqtt()
{
	if (_mqttClient != NULL)
	{
		esp_mqtt_client_stop(_mqttClient);
		esp_mqtt_client_destroy(_mqttClient);
		_mqttClient = NULL;
	}
	_mqttConnected = false;
}

void TecnovaIoT::_publishDueVariables()
{
	unsigned long now = millis();

	// Qué publicar se decide con _mutex tomado, pero se publica DESPUÉS de
	// soltarlo. esp_mqtt_client_publish() toma el candado interno del cliente
	// MQTT, y la tarea del MQTT tiene ese candado tomado cuando entrega un
	// comando; para entregarlo pide _mutex (_handleIncomingMessage). Si
	// publicáramos con _mutex tomado y justo llegara un comando, cada tarea
	// esperaría a la otra para siempre (deadlock): el equipo deja de publicar
	// y de recibir sin ningún error ni watchdog que lo rescate.
	struct Pending
	{
		String id;      // para volver a encontrar la variable si el envío falla
		String topic;
		String payload;
		bool requested; // salía por un pedido de sendNow()
	};
	std::vector<Pending> pending;

	xSemaphoreTake(_mutex, portMAX_DELAY);
	for (auto &variable : _variables)
	{
		if (variable.type == "output")
		{
			// Las "output" (las que en el panel están marcadas como "El
			// panel la acciona") se RECIBEN con onCommand(); no se
			// publican nunca. El panel tampoco las lee de acá: el widget
			// del interruptor refleja el comando que él mismo mandó, no
			// un reporte del equipo.
			//
			// OJO: si llamás a setValue() sobre una variable de salida, te
			// va a devolver true (el nombre existe) pero no se publica ni
			// se guarda nada -- _setValueByName() lo descarta a propósito
			// y avisa una vez por el monitor serie. Si querés que el
			// equipo informe en qué estado quedó de verdad un actuador,
			// creá en el panel una segunda variable de entrada (por
			// ejemplo "led_confirmado") y publicá esa.
			continue;
		}
		if (variable.lastPayloadJson.length() == 0)
		{
			continue; // todavía no se le seteó ningún valor con setValue()
		}
		// Un pedido de sendNow() acorta la espera a 250 ms. Si nadie llama a
		// sendNow(), sendRequested vale siempre false y esto es exactamente
		// la cuenta de la 1.4.0.
		unsigned long wait = variable.sendRequested ? SEND_NOW_SPACING_MS : variable.sendFreqMs;
		if (now - variable.lastSendMs < wait)
		{
			continue;
		}

		pending.push_back({variable.id, _topicPrefix + variable.id + "/sdata", variable.lastPayloadJson, variable.sendRequested});
		variable.lastSendMs = now;
		variable.sendRequested = false; // el envío también reinicia el intervalo normal
		variable.counter++;
	}
	xSemaphoreGive(_mutex);

	for (auto &item : pending)
	{
		// len=0 -> esp_mqtt_client usa strlen(payload) solo.
		int msgId = esp_mqtt_client_publish(_mqttClient, item.topic.c_str(), item.payload.c_str(), 0, 0, 0);

		// Con QoS 0, si la sesión se cayó justo entre la decisión y el envío
		// (o la red se trabó y venció la espera), esp-mqtt descarta el
		// mensaje y devuelve -1. Un pedido de sendNow() no se puede perder
		// así: sendNow() promete que el último valor sale, así que el pedido
		// queda de nuevo en pie y sale apenas vuelva la conexión. (Se vuelve
		// a tomar el candado recién ahora, ya sin llamar a esp-mqtt adentro:
		// ver la regla I4.) Si nadie llamó a sendNow() no se toca nada: la
		// variable sale en su próximo ciclo, como en la 1.4.0.
		if (msgId < 0 && item.requested)
		{
			xSemaphoreTake(_mutex, portMAX_DELAY);
			int idx = _findVariableIndexById(item.id);
			if (idx >= 0)
			{
				_variables[idx].sendRequested = true;
				_variables[idx].counter--; // no salió: no cuenta como mensaje procesado
			}
			xSemaphoreGive(_mutex);
		}
	}
}

bool TecnovaIoT::_setValueByName(const String &variableName, const String &payloadJson)
{
	// Se decide adentro del mutex pero se imprime afuera: escribir por
	// serie es lento y no hay por qué tener bloqueado _variables mientras
	// tanto (el callback de MQTT corre en otra tarea y también lo pide).
	bool avisarSalida = false;

	xSemaphoreTake(_mutex, portMAX_DELAY);
	int idx = _findVariableIndexByName(variableName);
	if (idx >= 0)
	{
		if (_variables[idx].type == "output")
		{
			// Una variable de salida NO se publica nunca (ver
			// _publishDueVariables). Sin este aviso, setValue() devolvía
			// true y no pasaba nada: el clásico fallo que no da ningún
			// error y cuesta una tarde encontrar.
			//
			// Y sobre todo: NO se pisa lastPayloadJson. Ahí vive el último
			// comando que mandó el panel (lo escribe
			// _handleIncomingMessage) y es lo que printStats() muestra en
			// la columna "Last V". Si guardáramos acá el valor que el
			// usuario intentó publicar, borraríamos la única evidencia de
			// qué llegó -- justo cuando está depurando por qué su actuador
			// no hace lo que espera.
			if (!_variables[idx].warnedOutputSetValue)
			{
				_variables[idx].warnedOutputSetValue = true;
				avisarSalida = true;
			}
		}
		else
		{
			_variables[idx].lastPayloadJson = payloadJson;
		}
	}
	xSemaphoreGive(_mutex);

	if (avisarSalida)
	{
		Serial.printf(
			"[TecnovaIoT] Aviso: setValue(\"%s\") no publica nada. En el panel esa variable esta marcada como \"El panel la acciona\" (salida): se recibe con onCommand(), no se envia. Si queres que el equipo informe su estado, crea una variable de entrada aparte.\n",
			variableName.c_str());
	}

	return idx >= 0;
}

int TecnovaIoT::_findVariableIndexByName(const String &variableName) const
{
	// Comparación insensible a mayúsculas/minúsculas a propósito: el
	// nombre configurado en el panel ("variableFullName") lo escribe una
	// persona a mano, y "Temperatura" vs "temperatura" es un error muy
	// fácil de cometer de cualquiera de los dos lados (panel o código) --
	// no tiene sentido que setValue()/onCommand() fallen en silencio por
	// una diferencia de capitalización que no cambia el significado.
	for (size_t i = 0; i < _variables.size(); i++)
	{
		if (_variables[i].fullName.equalsIgnoreCase(variableName))
		{
			return (int)i;
		}
	}
	return -1;
}

int TecnovaIoT::_findVariableIndexById(const String &variableId) const
{
	for (size_t i = 0; i < _variables.size(); i++)
	{
		if (_variables[i].id == variableId)
		{
			return (int)i;
		}
	}
	return -1;
}

void TecnovaIoT::_handleIncomingMessage(const String &topic, const String &payload)
{
	// Con el candado: printStats() los lee desde otra tarea, y un String a
	// medio reasignar puede apuntar a memoria que se acaba de liberar.
	xSemaphoreTake(_mutex, portMAX_DELAY);
	_lastReceivedTopic = topic;
	_lastReceivedMsg = payload;
	xSemaphoreGive(_mutex);

	if (!topic.startsWith(_topicPrefix))
	{
		return;
	}
	// El topic tiene forma "<topicPrefix><variableId>/acdata" -- se le saca
	// el prefijo conocido y se toma el segmento que queda antes de "/acdata".
	String rest = topic.substring(_topicPrefix.length());
	int slashPos = rest.indexOf('/');
	String variableId = (slashPos >= 0) ? rest.substring(0, slashPos) : rest;

	DynamicJsonDocument doc(256);
	if (deserializeJson(doc, payload) != DeserializationError::Ok)
	{
		return;
	}

	// Se captura el callback mientras se tiene el mutex, pero se invoca ya
	// afuera -- nunca hay que correr código del usuario con el lock tomado
	// (si ese callback llamara a setValue(), se colgaría esperando el mismo
	// mutex).
	TecnovaCommandCallback callbackToInvoke = nullptr;

	xSemaphoreTake(_mutex, portMAX_DELAY);
	int idx = _findVariableIndexById(variableId);
	if (idx >= 0)
	{
		_variables[idx].lastPayloadJson = payload;
		_variables[idx].counter++;
		callbackToInvoke = _variables[idx].callback;
	}
	xSemaphoreGive(_mutex);

	if (callbackToInvoke)
	{
		callbackToInvoke(doc.as<JsonVariant>());
	}
}

void TecnovaIoT::_staticMqttEventHandler(void *handlerArgs, esp_event_base_t base, int32_t eventId, void *eventData)
{
	TecnovaIoT *self = static_cast<TecnovaIoT *>(handlerArgs);
	self->_handleMqttEvent((esp_mqtt_event_handle_t)eventData);
}

void TecnovaIoT::_handleMqttEvent(esp_mqtt_event_handle_t event)
{
	switch ((esp_mqtt_event_id_t)event->event_id)
	{
	case MQTT_EVENT_CONNECTED:
		Serial.println("[TecnovaIoT] MQTT conectado");
		_mqttConnected = true;
		_mqttDisconnectedSinceMs = 0;
		_mqttAuthRefused = false;
		// Recién acá, ya conectado, se hace la suscripción -- así el broker
		// sabe que este cliente quiere recibir los comandos entrantes
		// ("acdata") de todas sus variables.
		esp_mqtt_client_subscribe(_mqttClient, _subscribeTopic.c_str(), 0);
		break;

	case MQTT_EVENT_DISCONNECTED:
		Serial.println("[TecnovaIoT] MQTT desconectado");
		_mqttConnected = false;
		if (_mqttDisconnectedSinceMs == 0)
		{
			_mqttDisconnectedSinceMs = millis();
		}
		break;

	case MQTT_EVENT_DATA:
	{
		// event->topic/event->data NO vienen terminados en '\0' (pueden ser
		// binarios) -- por eso se arman con String(ptr, len), nunca con
		// String(char*) directo.
		String topic((const char *)event->topic, event->topic_len);
		String payload((const char *)event->data, event->data_len);
		payload.trim();
		_handleIncomingMessage(topic, payload);
		break;
	}

	case MQTT_EVENT_ERROR:
		Serial.println("[TecnovaIoT] Error MQTT");
		// CONNACK 4 o 5: el broker contestó, pero no acepta este usuario o
		// esta clave (por ejemplo, porque rotaron del lado del servidor).
		// Reintentar con las mismas no sirve: en el modo sin reinicio,
		// loop() lo ve y pide credenciales nuevas sin esperar los 30 s.
		if (event->error_handle != NULL && event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED &&
			(event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
			 event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED))
		{
			_mqttAuthRefused = true; // en el modo por defecto nadie lo lee
		}
		break;

	default:
		break;
	}
}
