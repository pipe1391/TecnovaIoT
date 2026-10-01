// TecnovaProvisioning -- modulo OPCIONAL de TecnovaIoT para cargar
// credenciales de WiFi + del dispositivo Tecnova desde un portal cautivo,
// en vez de hardcodearlas en el firmware.
//
// Requiere agregar tzapu/WiFiManager a tus lib_deps -- NO es una
// dependencia obligatoria de TecnovaIoT en si, solo de este modulo. Si tu
// proyecto no usa TecnovaProvisioning, no hace falta esa libreria.
//
// Uso tipico:
//
//   #include <TecnovaIoT.h>
//   #include <TecnovaProvisioning.h>
//
//   TecnovaIoT *tecnova = nullptr;
//
//   void setup() {
//     Serial.begin(921600);
//     String wifiSsid, wifiPassword, deviceId, devicePassword;
//     TecnovaProvisioning::begin(wifiSsid, wifiPassword, deviceId, devicePassword);
//     tecnova = new TecnovaIoT(deviceId, devicePassword);
//     tecnova->begin(wifiSsid.c_str(), wifiPassword.c_str()); // WiFi ya conectado, no reconecta
//     TecnovaProvisioning::confirmSuccess(); // confirma que las credenciales sirvieron de verdad
//   }
//
//   void loop() {
//     TecnovaProvisioning::checkReconfigureButton(); // mantener BOOT 3s para reconfigurar
//     tecnova->loop();
//     ...
//   }
//
// Ver examples/CaptivePortal para un ejemplo completo. En un equipo con
// pantalla (sin reinicios), ver setAutoRestart() mas abajo y la seccion
// "Dispositivos con pantalla" del README.

#ifndef TecnovaProvisioning_h
#define TecnovaProvisioning_h

#include <Arduino.h>
#include <functional>

// Se llama cada vez que se abre el portal, con el nombre de la red WiFi que
// crea el equipo (el apName de begin()). Sirve, por ejemplo, para mostrar un
// QR "WIFI:T:nopass;S:<apName>;;" en una pantalla. Corre en la tarea que
// llamo a begin(), recien levantada la red y antes del servidor web: no uses
// delay() ni toques la pantalla desde aca (en un equipo con LVGL, manda el
// nombre por una cola).
typedef std::function<void(const char *apName)> TecnovaPortalCallback;

namespace TecnovaProvisioning
{
	// Junta SSID/password de WiFi + dId/password del dispositivo. Si ya
	// estan guardados de una vez anterior (NVS), conecta directo sin
	// mostrar nada; si falta algo, levanta el portal cautivo hasta
	// completarlo. Con la configuracion por defecto es bloqueante -- no
	// vuelve hasta tener las 4 credenciales y WiFi conectado (o reinicia el
	// ESP32 si algo falla feo). Para que no reinicie nunca ni espere al
	// WiFi (equipos con pantalla), ver setAutoRestart().
	//
	// RECUPERACION AUTOMATICA: si alguien carga credenciales de
	// dispositivo invalidas (que el panel rechaza) o una red WiFi que
	// nunca conecta, TecnovaIoT/WiFiManager reinician el ESP32 solos
	// desde adentro de begin() -- el codigo nunca llega a loop(), asi que
	// mantener apretado el boton de configuracion en ese momento NO hace
	// nada (checkReconfigureButton() nunca se ejecuta). Para no quedar
	// atascado en ese loop de reinicios, begin() cuenta los arranques
	// seguidos sin confirmSuccess() (persistido en NVS) y, al tercero (o
	// sea, despues de dos fallidos), reabre el portal por su cuenta -- sin
	// que haga falta tocar ningun boton.
	// Ese contador se resetea llamando a confirmSuccess() (ver mas abajo).
	void begin(String &wifiSsid, String &wifiPassword, String &deviceId, String &devicePassword,
			   const char *apName = "TecnovaIoT-Setup", uint8_t configButtonPin = 0);

	// Confirma que este arranque llego hasta el final con exito (llamar
	// justo despues de que tecnova->begin() retorne -- si retorno en vez
	// de reiniciar el chip, es porque el panel acepto las credenciales).
	// Resetea a cero el contador de arranques fallidos consecutivos que
	// usa begin() para la recuperacion automatica descripta arriba. Sin
	// esta llamada, un dispositivo que arranca bien pero despues se queda
	// sin WiFi un rato largo podria terminar reabriendo el portal sin
	// necesidad la proxima vez que se corte la luz.
	void confirmSuccess();

	// Llamar en cada vuelta de loop(). Si "configButtonPin" se mantiene
	// apretado "holdMs" ms seguidos con el dispositivo ya funcionando,
	// guarda un pedido de reconfiguracion y reinicia hacia el portal.
	// A proposito NO revisa el boton en el instante del reset -- en el
	// ESP32, GPIO0 es el pin de "strapping" que decide si entra al
	// bootloader de flasheo por USB, y mantenerlo apretado justo ahi puede
	// hacer que el firmware ni siquiera llegue a correr.
	void checkReconfigureButton(uint8_t configButtonPin = 0, unsigned long holdMs = 3000);

	// Borra las credenciales del dispositivo guardadas en NVS (no toca las
	// de WiFi, esas las maneja el propio ESP-IDF). Util para forzar una
	// reconfiguracion completa en la proxima llamada a begin().
	void forget();

	// ---- Opciones: llamar ANTES de begin() ----

	// true (por defecto): el comportamiento de siempre (ver RECUPERACION
	// AUTOMATICA). false: pensado para equipos con pantalla, donde un
	// reinicio se ve como un apagon. begin() no cuenta arranques fallidos, no
	// reinicia nunca y no conecta el WiFi: si ya hay datos guardados, los
	// devuelve enseguida y TecnovaIoT (tambien con setAutoRestart(false))
	// conecta en segundo plano. Un WiFi caido al arrancar NO abre el portal:
	// se abre solo si faltan datos o si se pidio reconfigurar (boton o
	// requestReconfigure()). Si el portal se cierra sin completarse, sigue con
	// lo guardado; si no habia datos completos, el portal no vence.
	// Mientras el portal esta abierto, la tarea que llamo a begin() baja a la
	// prioridad minima (la de IDLE) para que el vigilante (watchdog) del
	// nucleo 0 no reinicie el ESP32, y al cerrarse vuelve a la suya.
	// El formulario NO trae precargado el password del dispositivo (la red
	// del portal es abierta): vacio = se conserva el guardado.
	// En este modo confirmSuccess() no hace falta.
	void setAutoRestart(bool enabled);

	// Segundos que el portal espera sin que nadie lo use (cada pagina que se
	// abre reinicia la cuenta). Por defecto 300; 0 = sin limite.
	void setPortalTimeout(unsigned long seconds);

	// Registra la funcion que se llama cada vez que se abre el portal.
	void onPortalOpen(TecnovaPortalCallback callback);

	// Pide abrir el portal desde el codigo (por ejemplo, un boton "Configurar
	// conexion" en una pantalla tactil): guarda el pedido y reinicia ya; no
	// vuelve. Es lo mismo que mantener el boton de checkReconfigureButton().
	// Llamala desde la misma tarea que begin() y checkReconfigureButton():
	// comparten el acceso a la memoria NVS.
	void requestReconfigure();
}

#endif
