#include "TecnovaIoT.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_crt_bundle.h"
#include "esp_idf_version.h"
#include "TecnovaRootCaBundle.h"

namespace
{
	// Los dos endpoints de la plataforma no van como texto plano en el
	// fuente, para que no aparezcan con un grep/Ctrl+F directo en el repo
	// (que es publico). OJO: esto es una molestia para el curioso casual,
	// NO seguridad real -- terminan en texto plano igual en el firmware
	// compilado y en el trafico de red (el SNI de TLS manda el hostname
	// sin cifrar). La seguridad real esta del lado del servidor
	// (autenticacion por dId+password, politicas de acceso), no en que el
	// nombre del dominio sea dificil de encontrar. Ver README.
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

	const unsigned long WIFI_RETRY_DELAY_MS = 500;
	const int WIFI_MAX_RETRIES = 10;
	const unsigned long CREDENTIALS_RETRY_DELAY_MS = 10000;
	const unsigned long MQTT_RECONNECT_TIMEOUT_MS = 30000;
	// Si el panel no trae variableSendFreq (o viene en 0), se usa este piso
	// para no floodear el broker publicando en cada vuelta de loop().
	const unsigned long MIN_SEND_FREQ_MS = 1000;
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
	  _wifiPassword(NULL)
{
	_mutex = xSemaphoreCreateMutex();
}

void TecnovaIoT::onCommand(const String &variableName, TecnovaCommandCallback callback)
{
	_pendingCallbacks.push_back(std::make_pair(variableName, callback));
}

bool TecnovaIoT::begin(const char *wifiSsid, const char *wifiPassword)
{
	_wifiSsid = wifiSsid;
	_wifiPassword = wifiPassword;

	// Sin esto, arduino_esp_crt_bundle_attach() en _startMqtt() falla con
	// "Failed to attach bundle" -- nunca hay certificados cargados por
	// default, hay que setearlos a mano una vez.
	arduino_esp_crt_bundle_set(TECNOVA_ROOT_CA_BUNDLE);

	if (!_connectWifi())
	{
		return false; // no debería llegar acá: _connectWifi() reinicia solo si falla
	}

	if (!_fetchCredentials())
	{
		Serial.println("[TecnovaIoT] No se pudieron obtener las credenciales del panel. Reiniciando en 10s...");
		delay(CREDENTIALS_RETRY_DELAY_MS);
		ESP.restart();
		return false;
	}

	_startMqtt();
	return true;
}

void TecnovaIoT::loop()
{
	if (WiFi.status() != WL_CONNECTED)
	{
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
		if (_fetchCredentials())
		{
			_startMqtt();
		}
		else
		{
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
	xSemaphoreGive(_mutex);

	out.printf("\nFree RAM -> %u bytes\n", ESP.getFreeHeap());
	out.printf("Last incoming msg -> %s\n", _lastReceivedMsg.c_str());
}

// ---- privado ----

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

bool TecnovaIoT::_fetchCredentials()
{
	Serial.println("[TecnovaIoT] Pidiendo credenciales al panel...");

	String body = "dId=" + _deviceId + "&password=" + _devicePassword;

	// NOTA DE SEGURIDAD: setInsecure() no valida el certificado del
	// servidor en esta llamada puntual (a diferencia de la conexión MQTT,
	// que sí valida vía el bundle de CA -- ver _startMqtt()). Es una
	// simplificación deliberada: esta llamada solo intercambia credenciales
	// de un dispositivo ya identificado por dId+password, no expone datos
	// de terceros.
	WiFiClientSecure httpsClient;
	httpsClient.setInsecure();
	HTTPClient http;
	http.begin(httpsClient, _webhookEndpoint());
	http.addHeader("Content-Type", "application/x-www-form-urlencoded");
	int responseCode = http.POST(body);

	if (responseCode != 200)
	{
		Serial.printf("[TecnovaIoT] Error del webhook: HTTP %d\n", responseCode);
		http.end();
		return false;
	}

	String responseBody = http.getString();
	http.end();

	DynamicJsonDocument doc(_credentialsJsonCapacity);
	DeserializationError err = deserializeJson(doc, responseBody);
	if (err)
	{
		Serial.printf("[TecnovaIoT] Respuesta del webhook invalida: %s\n", err.c_str());
		return false;
	}

	xSemaphoreTake(_mutex, portMAX_DELAY);

	_mqttUsername = doc["username"].as<String>();
	_mqttPassword = doc["password"].as<String>();
	_topicPrefix = doc["topic"].as<String>();
	_subscribeTopic = _topicPrefix + "+/acdata";

	_variables.clear();
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
		_variables.push_back(variable);
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
	return true;
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
	// arduino-esp32 2.x (IDF4) o 3.x (IDF5).
#if ESP_IDF_VERSION_MAJOR >= 5
	cfg.broker.address.uri = mqttUri.c_str();
	cfg.broker.verification.crt_bundle_attach = arduino_esp_crt_bundle_attach;
	cfg.credentials.username = _mqttUsername.c_str();
	cfg.credentials.authentication.password = _mqttPassword.c_str();
	cfg.credentials.client_id = clientId.c_str();
#else
	cfg.uri = mqttUri.c_str();
	cfg.crt_bundle_attach = arduino_esp_crt_bundle_attach;
	cfg.username = _mqttUsername.c_str();
	cfg.password = _mqttPassword.c_str();
	cfg.client_id = clientId.c_str();
#endif

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

	xSemaphoreTake(_mutex, portMAX_DELAY);
	for (auto &variable : _variables)
	{
		if (variable.type == "output")
		{
			continue; // las "output" (actuadores) se reciben, no se publican
		}
		if (variable.lastPayloadJson.length() == 0)
		{
			continue; // todavía no se le seteó ningún valor con setValue()
		}
		if (now - variable.lastSendMs < variable.sendFreqMs)
		{
			continue;
		}

		variable.lastSendMs = now;
		String topic = _topicPrefix + variable.id + "/sdata";
		// len=0 -> esp_mqtt_client usa strlen(payload) solo.
		esp_mqtt_client_publish(_mqttClient, topic.c_str(), variable.lastPayloadJson.c_str(), 0, 0, 0);
		variable.counter++;
	}
	xSemaphoreGive(_mutex);
}

bool TecnovaIoT::_setValueByName(const String &variableName, const String &payloadJson)
{
	xSemaphoreTake(_mutex, portMAX_DELAY);
	int idx = _findVariableIndexByName(variableName);
	if (idx >= 0)
	{
		_variables[idx].lastPayloadJson = payloadJson;
	}
	xSemaphoreGive(_mutex);
	return idx >= 0;
}

int TecnovaIoT::_findVariableIndexByName(const String &variableName) const
{
	for (size_t i = 0; i < _variables.size(); i++)
	{
		if (_variables[i].fullName == variableName)
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
	_lastReceivedTopic = topic;
	_lastReceivedMsg = payload;

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
		break;

	default:
		break;
	}
}
