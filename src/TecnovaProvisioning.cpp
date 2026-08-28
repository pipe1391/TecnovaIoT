#include "TecnovaProvisioning.h"

// PlatformIO/Arduino compilan TODOS los .cpp de una librería como una sola
// unidad, sin importar si el sketch usa este archivo en particular -- por
// eso, sin este chequeo, cualquier proyecto que use TecnovaIoT (aunque
// jamás llame a TecnovaProvisioning) dejaría de compilar si no tiene
// WiFiManager instalada. __has_include() nos deja detectar en tiempo de
// compilación si esa librería está disponible, y si no lo está, compilar
// una versión "stub" (que avisa el error por Serial en vez de fallar la
// build entera) -- así se cumple de verdad lo que dice el README: este
// módulo es opcional.
#if __has_include(<WiFiManager.h>)
#define TECNOVA_HAS_WIFIMANAGER 1
#else
#define TECNOVA_HAS_WIFIMANAGER 0
#endif

#if TECNOVA_HAS_WIFIMANAGER

#include <Preferences.h>
#include <WiFiManager.h>

// ============================================================================
// QUE ES UN "PORTAL CAUTIVO" Y POR QUE LO USAMOS
// ============================================================================
// Cuando un dispositivo nuevo no tiene todavia ni WiFi ni credenciales
// configuradas, no hay forma de que "hable" con nosotros por internet
// (justamente porque no tiene WiFi). La solucion clasica: el propio
// dispositivo se convierte, por unos minutos, en su PROPIO punto de acceso
// WiFi (un "Access Point" o AP) -- exactamente como un router casero. Uno
// se conecta a esa red desde el celular, y como el ESP32 corre un
// servidorcito web adentro, aparece un formulario para cargar los datos
// reales (la red WiFi de destino + las credenciales del dispositivo).
//
// "Cautivo" quiere decir que, apenas te conectas a esa red, el celular
// solo te deja ver esa pagina de configuracion (no navegar a otro lado) --
// es el mismo mecanismo que usa el WiFi de un cafe o un hotel para
// mostrarte "aceptar terminos" antes de dejarte navegar.
//
// Toda esta parte "dificil" (armar el punto de acceso, el servidor web, la
// deteccion automatica del portal en el celular) la resuelve la libreria
// WiFiManager -- nosotros solo le agregamos DOS campos de mas al
// formulario (el ID y password del dispositivo Tecnova) y le ponemos nuestro
// propio estilo visual.
//
// ============================================================================
// DONDE SE GUARDAN LAS CREDENCIALES: NVS (memoria no volatil)
// ============================================================================
// El ESP32 tiene una zona de su memoria flash reservada para guardar datos
// chiquitos que sobreviven a un reinicio o un corte de luz -- se llama NVS
// ("Non-Volatile Storage"). Arduino nos da una forma facil de usarla: la
// clase Preferences, que funciona como un diccionario clave->valor
// (parecido a localStorage en un navegador web, si conoces JavaScript).
//
// Guardamos ahi 4 cosas, bajo el "namespace" (como una carpeta) "tecnova":
//   - wifi (en realidad NO la guardamos nosotros -- el propio WiFiManager /
//     ESP-IDF ya persiste el SSID/password de WiFi en su propia zona de NVS
//     cuando uno hace WiFi.begin() con exito, asi que no hace falta que la
//     dupliquemos)
//   - "device_id"    -> el dId del dispositivo en el panel Tecnova
//   - "device_pass"  -> el password de ese dispositivo
//   - "force_portal" -> una bandera temporal que usamos para "pedir" que se
//     vuelva a abrir el portal la proxima vez que arranque (ver
//     checkReconfigureButton())

namespace
{
	// Cuantos arranques fallidos seguidos (sin llegar a confirmSuccess())
	// tolera begin() antes de reabrir el portal por su cuenta -- ver la
	// nota de "RECUPERACION AUTOMATICA" en TecnovaProvisioning.h.
	const int MAX_BOOT_FAILURES = 3;

	// ------------------------------------------------------------------------
	// Estilo del portal: paleta neutra de un solo color de acento, como usan
	// los dispositivos IoT "serios" (routers, camaras, asistentes de voz) en
	// su pantalla de primera configuracion. La idea de diseño es simple: un
	// formulario de configuracion es una HERRAMIENTA, no un afiche
	// publicitario -- cuantos mas colores compitiendo, mas cuesta leerlo
	// rapido. Un solo acento (acá, un azul) alcanza para que se note qué es
	// clickeable, sin distraer del objetivo real (cargar los datos y listo).
	//
	// Esto se inyecta en el <head> de TODAS las paginas del portal via
	// wm.setCustomHeadElement() -- es simplemente CSS estandar.
	// ------------------------------------------------------------------------
	const char PORTAL_CSS[] PROGMEM = R"CSS(
<style>
:root{--accent:#2563eb;--accent-dark:#1d4ed8;--text:#111827;--muted:#6b7280;--bg:#f3f4f6;--card:#ffffff;--border:#e5e7eb;}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;}
.wrap{background:var(--card);border:1px solid var(--border);border-radius:10px;box-shadow:0 1px 2px rgba(0,0,0,.04),0 4px 12px rgba(0,0,0,.04);padding:28px 24px;margin:32px auto;max-width:380px;}
h1{font-size:1.15rem;font-weight:600;}
h3{color:var(--muted);font-weight:400;font-size:.8rem;}
label{color:var(--text);font-weight:500;font-size:.85rem;}
input{border:1px solid var(--border);border-radius:8px;background:#fafafa;}
input:focus{outline:none;border-color:var(--accent);background:#fff;box-shadow:0 0 0 3px rgba(37,99,235,.15);}
button,input[type='submit']{background:var(--accent) !important;border-radius:8px !important;font-weight:600;letter-spacing:0;}
button:hover{background:var(--accent-dark) !important;}
button.D{background:#dc2626 !important;}
a{color:var(--accent);font-weight:500;}
a:hover{color:var(--accent-dark) !important;}
.msg{border-radius:8px;}
.msg.S{border-left-color:#16a34a !important;} .msg.S h4{color:#16a34a !important;}
.msg.D{border-left-color:#dc2626 !important;} .msg.D h4{color:#dc2626 !important;}
.msg.P{border-left-color:var(--accent) !important;} .msg.P h4{color:var(--accent) !important;}
</style>
)CSS";

	// Pequeño titulo + ayuda que aparece arriba de los dos campos propios de
	// Tecnova, para separarlos visualmente de los campos de WiFi (que ya
	// vienen incluidos con WiFiManager).
	const char SECTION_DIVIDER[] PROGMEM =
		"<hr><label style='display:block;margin-bottom:2px;'>Datos del dispositivo</label>"
		"<small style='color:#6b7280;'>Los ves en la seccion \"Dispositivos\" del panel.</small>";

	Preferences _prefs;

	// WiFiManager necesita que estos objetos (los campos extra del
	// formulario) sigan existiendo mientras el portal esta abierto -- por
	// eso son punteros a memoria reservada con "new" en vez de variables
	// locales de begin(): una variable local se destruiria apenas la
	// funcion terminara esa parte del codigo, pero el portal puede seguir
	// esperando que alguien lo complete durante varios minutos.
	WiFiManagerParameter *_paramDeviceId = nullptr;
	WiFiManagerParameter *_paramDevicePass = nullptr;

	unsigned long _buttonPressStart = 0;

	String _loadPref(const char *key)
	{
		_prefs.begin("tecnova", true); // true = abrir solo para lectura
		String value = _prefs.getString(key, "");
		_prefs.end();
		return value;
	}

	void _savePref(const char *key, const String &value)
	{
		_prefs.begin("tecnova", false); // false = abrir para lectura Y escritura
		_prefs.putString(key, value);
		_prefs.end();
	}

	// WiFiManager llama a esta funcion automaticamente apenas el usuario
	// aprieta "Save" en el portal -- es el momento exacto en el que sabemos
	// que campos escribio, asi que aprovechamos para guardarlos en NVS.
	// WiFiManager NO los guarda solo: el, por diseño, solo sabe mostrar el
	// formulario y decirnos que valores puso la persona -- que hacer con
	// esos valores es decision de cada proyecto.
	void _onPortalSave()
	{
		_savePref("device_id", String(_paramDeviceId->getValue()));
		_savePref("device_pass", String(_paramDevicePass->getValue()));
	}
}

namespace TecnovaProvisioning
{

void begin(String &outWifiSsid, String &outWifiPassword, String &outDeviceId, String &outDevicePassword,
		   const char *apName, uint8_t configButtonPin)
{
	pinMode(configButtonPin, INPUT_PULLUP);

	// Recuperacion automatica (ver la nota completa en el .h): cuenta los
	// arranques seguidos que NO terminaron en un confirmSuccess(). Si se
	// pasa de MAX_BOOT_FAILURES, algo esta mal con lo que hay guardado
	// (WiFi que no conecta, o credenciales de dispositivo que el panel
	// rechaza) y loop() -- donde se revisa el boton -- nunca se llega a
	// ejecutar. En vez de quedar reiniciando en loop para siempre, se
	// fuerza el portal solo, sin que haga falta tocar nada.
	int bootFailures = _loadPref("boot_fails").toInt() + 1;
	bool tooManyFailures = (bootFailures >= MAX_BOOT_FAILURES);
	if (tooManyFailures)
	{
		Serial.printf("[TecnovaProvisioning] %d arranques seguidos sin exito -- reabriendo el portal automaticamente.\n", bootFailures);
		_savePref("boot_fails", "0");
	}
	else
	{
		_savePref("boot_fails", String(bootFailures));
	}

	// Paso 1: ver que tenemos guardado de arranques anteriores.
	outDeviceId = _loadPref("device_id");
	outDevicePassword = _loadPref("device_pass");

	// "force_portal" es una bandera que deja prendida
	// checkReconfigureButton() cuando alguien pide reconfigurar a mano. La
	// leemos UNA vez y la borramos enseguida ("se consume"), para que en el
	// SIGUIENTE reinicio (una vez resuelto el portal) no se vuelva a abrir
	// solo sin que nadie lo pida.
	bool forcePortal = (_loadPref("force_portal") == "1") || tooManyFailures;
	if (_loadPref("force_portal") == "1")
	{
		_prefs.begin("tecnova", false);
		_prefs.remove("force_portal");
		_prefs.end();
	}

	bool missingDeviceCreds = (outDeviceId.length() == 0 || outDevicePassword.length() == 0);

	// Paso 2: configurar como se va a ver y comportar el portal, SI hace
	// falta abrirlo.
	WiFiManager wm;
	wm.setTitle("Configuracion del dispositivo");
	wm.setCustomHeadElement(PORTAL_CSS);
	wm.setConfigPortalTimeout(300); // si nadie completa el formulario en 5 min, sigue reintentando con lo que ya tenia

	WiFiManagerParameter sectionDivider(SECTION_DIVIDER);
	_paramDeviceId = new WiFiManagerParameter("device_id", "ID del dispositivo (dId)", outDeviceId.c_str(), 40);
	_paramDevicePass = new WiFiManagerParameter("device_pass", "Password del dispositivo", outDevicePassword.c_str(), 40);

	wm.addParameter(&sectionDivider);
	wm.addParameter(_paramDeviceId);
	wm.addParameter(_paramDevicePass);
	wm.setSaveParamsCallback(_onPortalSave);

	// Paso 3: la decision central de esta funcion.
	//   - Si falta algun dato, o alguien pidio reconfigurar -> abrimos el
	//     portal SI O SI con startConfigPortal(), aunque ya hubiera WiFi
	//     guardado.
	//   - Si ya esta todo completo -> usamos autoConnect(), que intenta
	//     conectar con el WiFi ya guardado primero, y solo abre el portal
	//     como plan B si esa conexion falla (por ejemplo, si cambiaron la
	//     contraseña del router).
	bool wifiOk;
	if (forcePortal || missingDeviceCreds)
	{
		Serial.println("[TecnovaProvisioning] Abriendo portal de configuracion...");
		wifiOk = wm.startConfigPortal(apName);
	}
	else
	{
		wifiOk = wm.autoConnect(apName);
	}

	if (!wifiOk)
	{
		// wm.setConfigPortalTimeout() hizo que se rindiera despues de 5 min
		// sin que nadie completara el formulario. Reiniciar el ESP32 es la
		// forma mas simple y confiable de "empezar de nuevo" en estos
		// casos -- vuelve a intentar todo el proceso desde cero.
		Serial.println("[TecnovaProvisioning] No se pudo completar la configuracion. Reiniciando...");
		delay(3000);
		ESP.restart();
	}

	// Si llegamos aca, WiFi.status() ya es WL_CONNECTED (eso es justamente
	// lo que garantiza wifiOk == true). WiFi.SSID()/WiFi.psk() nos dan de
	// vuelta la red y password con la que efectivamente quedo conectado
	// -- utiles para pasarselos despues a TecnovaIoT::begin().
	outWifiSsid = WiFi.SSID();
	outWifiPassword = WiFi.psk();

	// Releemos desde NVS (no desde las variables locales) porque, si
	// _onPortalSave() se disparo durante el portal, ahi es donde quedaron
	// los valores mas actualizados.
	outDeviceId = _loadPref("device_id");
	outDevicePassword = _loadPref("device_pass");

	if (outDeviceId.length() == 0 || outDevicePassword.length() == 0)
	{
		// Caso raro pero posible: WiFi ya estaba guardado de antes (por
		// eso autoConnect() conecto sin mostrar portal), pero JAMAS se
		// cargaron los datos del dispositivo. Forzamos un reinicio, y la
		// proxima vuelta "missingDeviceCreds" va a ser true, asi que esta
		// vez si va a abrir el portal.
		Serial.println("[TecnovaProvisioning] Faltan credenciales del dispositivo. Reiniciando para reabrir el portal...");
		delay(2000);
		ESP.restart();
	}

	Serial.println("[TecnovaProvisioning] Configuracion completa.");
}

void checkReconfigureButton(uint8_t configButtonPin, unsigned long holdMs)
{
	// Con INPUT_PULLUP, el pin lee HIGH cuando el boton esta SUELTO, y LOW
	// cuando esta APRETADO (el boton conecta el pin a tierra/GND).
	if (digitalRead(configButtonPin) != LOW)
	{
		_buttonPressStart = 0; // se solto (o nunca se apreto) -> reiniciar el conteo
		return;
	}

	if (_buttonPressStart == 0)
	{
		// Recien detectamos que se apreto: anotamos CUANDO, para poder
		// medir cuanto tiempo pasa sin bloquear el resto de loop() (nada
		// de delay() largos aca -- eso trabaria toda la logica de
		// TecnovaIoT mientras tanto).
		_buttonPressStart = millis();
		return;
	}

	if (millis() - _buttonPressStart > holdMs)
	{
		// Importante: este boton se revisa ACA, con el firmware ya
		// funcionando -- nunca en el instante de un reset/power-up. En el
		// ESP32, el pin GPIO0 (el boton "BOOT" de la mayoria de las
		// placas) es tambien el pin de "strapping" que el propio chip usa,
		// en el momento exacto del reset, para decidir si arranca el
		// programa normal o si entra al modo de grabacion por USB. Si
		// alguien mantiene ese boton apretado justo al resetear, corre el
		// riesgo de que el ESP32 nunca llegue a ejecutar este firmware.
		// Revisandolo aca, con el chip ya arrancado hace rato, evitamos
		// ese problema por completo.
		Serial.println("[TecnovaProvisioning] Boton mantenido -- reiniciando para abrir el portal de configuracion...");
		_savePref("force_portal", "1");
		delay(300);
		ESP.restart();
	}
}

void forget()
{
	_prefs.begin("tecnova", false);
	_prefs.remove("device_id");
	_prefs.remove("device_pass");
	_prefs.end();
}

void confirmSuccess()
{
	_savePref("boot_fails", "0");
}

} // namespace TecnovaProvisioning

#else // !TECNOVA_HAS_WIFIMANAGER

// No se encontró <WiFiManager.h> en este proyecto. En vez de romper la
// compilación entera de TecnovaIoT (que sí funciona perfecto sin esto),
// dejamos una implementación "stub": compila bien, pero si el sketch
// realmente llama a alguna de estas funciones, avisa por Serial qué hay
// que instalar en vez de fallar en silencio.
namespace TecnovaProvisioning
{

void begin(String &outWifiSsid, String &outWifiPassword, String &outDeviceId, String &outDevicePassword,
		   const char *apName, uint8_t configButtonPin)
{
	(void)outWifiSsid;
	(void)outWifiPassword;
	(void)outDeviceId;
	(void)outDevicePassword;
	(void)apName;
	(void)configButtonPin;
	Serial.println("[TecnovaProvisioning] ERROR: falta agregar tzapu/WiFiManager a tus lib_deps (ver README de TecnovaIoT, seccion \"Portal cautivo\").");
	delay(5000);
	ESP.restart();
}

void checkReconfigureButton(uint8_t configButtonPin, unsigned long holdMs)
{
	(void)configButtonPin;
	(void)holdMs;
}

void forget()
{
}

void confirmSuccess()
{
}

} // namespace TecnovaProvisioning

#endif // TECNOVA_HAS_WIFIMANAGER
