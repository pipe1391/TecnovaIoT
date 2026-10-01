# Cambios

Las versiones siguen [semver](https://semver.org/lang/es/): `MAYOR.MENOR.PARCHE`.

## 1.5.0 — 29 de septiembre de 2026

Equipos con pantalla. Con la configuración por defecto, un dispositivo con
la 1.4.0 se comporta igual: todo lo nuevo hay que pedirlo. Las únicas
diferencias que se notan sin pedir nada son las de "Cambiado" y
"Corregido".

### Agregado

- **`setAutoRestart(false)`: un modo sin reinicios**, pensado para equipos
  con pantalla, donde un reinicio se ve como un apagón y un `loop()` que
  espera congela la imagen. `begin()` vuelve enseguida y `loop()` hace la
  conexión de a pasos, reintentando solo lo que falle y cada vez más
  espaciado. Nunca llama a `delay()` ni a `ESP.restart()`; lo único que
  puede tardar es el pedido HTTPS de credenciales, por eso la librería va
  en una tarea propia de FreeRTOS. Qué hace ante cada problema:

  | Qué pasa | Qué hace | Pedidos al webhook |
  |---|---|---|
  | No hay WiFi al arrancar | `begin()` vuelve al instante; empujones al WiFi a los 15, 45 y 105 s, y después cada 60 s. | 0 |
  | Se corta el WiFi andando | A los 10 s da por muerta la sesión MQTT vieja; al volver, esp-mqtt reconecta solo. | 0 (1 si en 30 s no reconecta) |
  | Se corta Internet con el WiFi andando | esp-mqtt lo nota por su *keepalive* (30 s en este modo, en vez de 120), en menos de un minuto; 30 s después pide credenciales. | Como cuando falla el webhook |
  | El broker rechaza usuario/clave (CONNACK 4 o 5) | Pide credenciales enseguida, si la espera lo permite. | 1 |
  | El webhook falla (5xx, sin respuesta, certificado o JSON inválidos) | Reintenta a los 5, 10, 20, 40, 80 y 120 s, y después cada 120 s, más un 0-20 % al azar. Cada intento, como mucho unos 35 s. | Como mucho 1 cada 2 min |
  | El panel responde 401, 403 o 404 | Reintenta cada 5 min, más el azar. | 1 cada 5 min |
  | El webhook anda pero el MQTT nunca conecta | Pide credenciales cada 30 s o lo que diga la espera, lo que sea mayor. | 30, 30, 30, 40, 80, 120... s |
  | Cambió la clave del router | Sigue empujando al WiFi cada 60 s. No abre el portal solo. | 0 |

  La espera entre pedidos es una sola para todos los caminos y vuelve a
  cero cuando conecta el MQTT: un corte en el aula ya no se convierte en
  una tormenta de pedidos contra el servidor.
- **`getState()` y `stateName()`**: en qué paso está la conexión, para
  mostrarlo en una pantalla. Se leen desde cualquier tarea, sin candados, y
  funcionan en los dos modos.

  | Estado | Qué significa |
  |---|---|
  | `TECNOVA_IDLE` | Todavía no se llamó a `begin()`. |
  | `TECNOVA_WIFI_CONNECTING` | Esperando el WiFi. |
  | `TECNOVA_FETCHING_CREDENTIALS` | Pidiendo las credenciales al panel. |
  | `TECNOVA_CREDENTIALS_REJECTED` | El panel dice que el `dId` o el password no valen (401/403/404). |
  | `TECNOVA_SERVER_UNAVAILABLE` | El pedido falló por otra causa (red, certificado, servidor, JSON). |
  | `TECNOVA_MQTT_CONNECTING` | Esperando que el broker acepte la sesión. |
  | `TECNOVA_CONNECTED` | Sesión MQTT activa. |
- **`sendNow()`**: publica ya el último valor de una variable, sin esperar
  su frecuencia de envío -- para un control que alguien acaba de tocar.
  Como mucho un envío cada 250 ms por variable, y el último valor siempre
  sale: si la conexión se cae, aunque sea justo en el envío, el pedido
  queda en pie y sale apenas vuelva. No publica adentro de la llamada (lo
  hace `loop()`), así que se puede llamar desde cualquier tarea. Esa
  garantía es entera con `setAutoRestart(false)`; en el modo por defecto,
  un reinicio por corte de WiFi o un nuevo pedido de credenciales (tras
  30 s sin MQTT) descartan el pedido y el último valor.
- **En `TecnovaProvisioning`**: `setAutoRestart()` (el mismo modo, del lado
  del portal: no cuenta arranques, no reinicia y no conecta el WiFi; un
  WiFi caído al arrancar no abre el portal), `setPortalTimeout()`,
  `onPortalOpen()` (por ejemplo, para mostrar en la pantalla un QR con la
  red del portal) y `requestReconfigure()` (para un botón "Configurar
  conexión" en una pantalla táctil). En ese modo, mientras el portal está
  abierto, la tarea baja a la prioridad de IDLE: el lazo de WiFiManager no
  cede el núcleo, y en el núcleo 0 el vigilante (watchdog) reiniciaba el
  ESP32 si un celular dejaba una conexión abierta sin mandar nada. Y el
  formulario no trae precargado el password del dispositivo (vacío = no
  cambia), porque ahí el portal se abre con un par de toques.
- **El ejemplo `NetworkTask`**: la librería en su propia tarea, sin
  reinicios.
- **La sección "Dispositivos con pantalla" del README**: la receta, qué
  corre en qué tarea y cómo combinarla con LVGL sin romper nada.

### Cambiado

- **El pedido de credenciales al webhook valida el certificado TLS**, con
  el mismo paquete de CA que el MQTT. Antes usaba `setInsecure()`, y
  alguien en la misma red (el WiFi de un aula, por ejemplo) podía hacerse
  pasar por el servidor y quedarse con el `dId`, el password del
  dispositivo y las credenciales MQTT.
  - **Por qué no afecta a los equipos que hoy funcionan**: es el mismo
    host y el mismo certificado que el del broker. Si el MQTT valida -- y
    valida, porque si no el equipo no andaría --, esto también.
  - **Qué pasa si el servidor cambia a una CA que no está en el paquete**:
    el MQTT tampoco conectaría, igual que con la 1.4.0; lo que cambia es
    cómo falla. Con la 1.4.0 el equipo pedía credenciales cada 30 s, sin
    reiniciar. Con la 1.5.0, en el modo por defecto, no consigue las
    credenciales y reinicia cada unos 10 s (con `TecnovaProvisioning`, al
    tercer arranque abre el portal). El monitor serie muestra la causa en
    una línea nueva, `Detalle TLS: ...`. Esa línea sale solo cuando el
    error es de mbedTLS: si el `-1` es de red (sin conexión TCP o tiempo
    agotado), no sale, porque el núcleo usa ese mismo `-1` para todo eso y
    se leería como un "Generic error" de TLS.

### Corregido (errores de la librería)

- **Un deadlock entre la publicación y un comando entrante.** Lo que se
  veía: el equipo dejaba de publicar y de recibir comandos, para siempre,
  sin ningún mensaje de error y sin que el vigilante (watchdog) lo
  rescatara. Por qué pasaba: `loop()` publicaba con el candado de las
  variables tomado, y publicar necesita el candado interno del cliente
  MQTT. La tarea del MQTT, cuando entrega un comando, tiene ese candado
  interno tomado y pide el de las variables. Si un comando llegaba justo
  mientras se publicaba, cada tarea se quedaba esperando a la otra. Hacía
  falta que coincidieran en el mismo instante, pero cuantos más comandos y
  publicaciones, más probable. Ahora se decide qué publicar con el candado
  y se publica después de soltarlo.
- **No compilaba con arduino-esp32 3.x**, aunque el README decía que sí:
  usaba `arduino_esp_crt_bundle_*`, que en 3.x no existe. Ahora compila
  con 3.x (ver "Nota conocida"), y desde ESP-IDF 5.4 (arduino-esp32 3.2 en
  adelante) traduce en memoria el paquete de CA al formato nuevo: con el
  formato viejo, ESP-IDF lo rechazaba sin avisar.
- **Una carrera en `printStats()`**: leía el último mensaje recibido sin
  el candado, mientras la tarea del MQTT podía estar reescribiéndolo, y
  podía leer memoria recién liberada. Ahora lo copia con el candado; lo
  que imprime es lo mismo.
- **El `dId` y el password viajaban al webhook sin codificar.** El pedido
  es `application/x-www-form-urlencoded`, donde `+` quiere decir espacio,
  `&` separa campos y `%41` es una letra: un password como `clave+2026` o
  `a&b` le llegaba cambiado al servidor, y el panel lo rechazaba aunque
  fuera el correcto. Ahora todo lo que no es letra, número o `- _ . ~` se
  manda como `%XX`. **No afecta a los equipos que hoy funcionan**: con un
  password de letras y números salen exactamente los mismos bytes, y
  cualquier password que ya funcionaba le sigue llegando igual al
  servidor.

### Corregido (documentación y ejemplos)

- **El README, `TecnovaIoT.h` y el ejemplo `BasicSensor` decían que
  `loop()` reconecta el WiFi.** En el modo por defecto no lo hace: si se
  corta el WiFi, espera 15 s y reinicia el ESP32. (El MQTT sí se reconecta
  solo.) Lo mismo decían, con otras palabras, la introducción del README,
  el encabezado de `TecnovaIoT.h` y la descripción de `library.properties`,
  que además ahora aclara que también se valida el webhook.
- **El aviso sobre contraseñas en el monitor serie nombraba mal el nivel
  de WiFiManager** (`DEBUG_DEV` no existe). Con `WM_DEBUG_VERBOSE` ya
  imprime el password del dispositivo, y con `WM_DEBUG_DEV`, también la
  clave del WiFi.
- **El README decía que el portal guarda los datos "si funcionan".** Se
  guardan al tocar Guardar, antes de probar la conexión.
- **El README hablaba del "tercer arranque fallido"** en la recuperación
  automática. Es el tercer arranque seguido sin `confirmSuccess()`, o sea,
  después de dos fallidos (también corregido en `TecnovaProvisioning.h`).
- **La sección de compatibilidad del README se reescribió**: qué núcleo
  está probado en hardware y cuál solo compila.

### Cómo migrar

Nada obligatorio: alcanza con recompilar. Si tenés un equipo con pantalla
(o un `loop()` que no puede quedarse esperando), leé la sección nueva
"Dispositivos con pantalla" del README y el ejemplo `NetworkTask`.

### Nota conocida

- La cadena de certificados del servidor valida hoy gracias al certificado
  de GTS Root R4 firmado de forma cruzada por GlobalSign Root CA, que vence
  el 28 de enero de 2028. El proveedor (Cloudflare) puede cambiar de
  Autoridad Certificadora cuando quiera, porque el dominio no tiene
  registro CAA. Está previsto ampliar el paquete en una 1.5.x.
- El código para arduino-esp32 3.x se verificó leyendo las fuentes y
  compilando: `NetworkTask`, `BasicSensor` y `CaptivePortal` con la 3.3.12
  (ESP-IDF 5.5.5, que usa la traducción del paquete), y `NetworkTask` y
  `BasicSensor` con la 3.1.3 (ESP-IDF 5.3, formato viejo). **No se probó
  en hardware**, y la rama para 3.0.0 a 3.0.3 no se compiló.
- WiFiManager 2.0.17 compila con la 3.3.12, pero tampoco se probó en
  hardware. Ojo: con 3.x, `CaptivePortal` ocupa unos 1,3 MB y no entra en
  la partición de programa por defecto de un ESP32 de 4 MB (1,25 MB): hace
  falta otra tabla de particiones, por ejemplo
  `board_build.partitions = min_spiffs.csv`.
- Sin reinicio, cerrar el cliente MQTT para pedir credenciales nuevas
  puede tardar unos segundos.
- El portal es una red WiFi abierta, y WiFiManager deja habilitadas, sin
  clave, las páginas para subir otro firmware (`/update`) y para borrar la
  configuración (`/erase`). Con la configuración por defecto, además, el
  formulario muestra el password del dispositivo guardado. Abrilo solo
  cuando lo vas a usar (ver "Seguridad del portal" en el README). Queda
  pendiente una clave opcional para la red del portal.

## 1.4.0 — 25 de septiembre de 2026

Esta versión acompaña un cambio del panel: **el valor que manda un control
ya no es un texto que se escribe a mano, sino un valor tipado que decide el
tipo de control**. Si tenés un actuador andando, leé la nota de migración.

Importante: **ese cambio es del panel, no de la librería**. Ya está activo
en tus dispositivos, tengan la versión que tengan. Lo que la 1.4.0 arregla
es la documentación y los ejemplos, que enseñaban a leer el valor de una
forma que ya no corresponde -- más dos errores de la librería que salieron
a la luz revisando esto (ver "Corregido").

### El problema que se arregla

Hasta ahora, el widget "botón" del panel publicaba tal cual el texto libre
de un campo llamado *"Mensaje a enviar"* del formulario de Variables. Ese
campo era opcional y estaba al final del formulario, así que quedaba vacío
y salía al aire `{"value":""}` — que todo firmware razonable lee como
"apagar". El botón se veía pulsar, el panel decía "listo", y el equipo
nunca encendía. Sin un solo error a la vista.

El campo ya no existe. Ahora el payload lo decide el control:

| Control en el panel | Qué publica |
|---|---|
| Interruptor | `{"value": true}` / `{"value": false}` |
| Botón de pulso | `{"value": true}` — siempre, no tiene estado |
| Deslizador | `{"value": 128}` — un número |

### Corregido (errores de la librería)

- **Un corte de red de más de 30 segundos dejaba al dispositivo en un
  bucle.** Al reintentar, `loop()` pedía credenciales y arrancaba el
  cliente MQTT, pero no reiniciaba el reloj de la desconexión: la
  condición "lleva más de 30 s desconectado" seguía siendo verdadera en
  **cada vuelta**. Con el `delay(50)` de los ejemplos, eso destruía el
  cliente MQTT cuando llevaba 50 ms de negociación TLS -- que no alcanza
  ni de cerca -- y disparaba un POST HTTPS al webhook de credenciales por
  vuelta. Con varios equipos en el aula, un corte del router se convertía
  en una tormenta de pedidos contra el servidor, y el dispositivo se
  recuperaba de casualidad. Ahora el cliente nuevo arranca con sus
  propios 30 s.
- **`setValue()` sobre una variable de salida borraba la evidencia del
  último comando recibido.** Guardaba el valor en el mismo campo donde
  vive el último comando que mandó el panel, que es lo que `printStats()`
  muestra en la columna "Last V". O sea: justo cuando alguien depuraba
  por qué su relé no hacía lo esperado, la tabla le mostraba el valor que
  él mismo había escrito en vez del que llegó, y lo mandaba a buscar el
  problema al cableado. Ahora no se pisa nada: solo se avisa.

### Corregido (documentación y ejemplos)

- **El ejemplo del encabezado de `TecnovaIoT.h` comparaba el comando contra
  el texto `"true"`.** Esa comparación da `false` ante un booleano JSON, así
  que el actuador nunca se movía. La versión 1.2.2 ya había corregido esto
  en los ejemplos `.ino`, pero se salteó el comentario del header — que es
  justamente el más copiado, porque es lo primero que se ve al abrir la
  librería. Es el error que costó una tarde de trabajo a un profesor.
- **El ejemplo `RGBLed` esperaba un color hexadecimal por texto**
  (`{"value":"#FF8800"}`), que ningún control del panel puede mandar:
  compilaba, pero nadie podía accionarlo nunca. Rehecho con tres variables
  de salida (`rojo`, `verde`, `azul`), cada una con un deslizador de 0 a
  255. Enseña lo mismo (PWM y mezcla de color) y además funciona.
- **`depends` en `library.properties` no tenía tope de versión**, mientras
  que `library.json` sí lo tenía (`^6.19.4`). El gestor de librerías del
  IDE de Arduino instalaba ArduinoJson 7. La librería compila con la 7,
  pero con avisos de obsolescencia (`StaticJsonDocument` y
  `DynamicJsonDocument` están marcados como deprecados desde esa versión).
  Ahora ambos archivos piden lo mismo: `>=6.19.4 && <7.0.0`.
- **`keywords.txt` estaba congelado en la 1.0.0**: le faltaban
  `enablePowerSave`, `deepSleepSeconds` y los tres métodos de
  `TecnovaProvisioning` (`confirmSuccess`, `checkReconfigureButton`,
  `forget`), más la clase misma. Sin eso el IDE no los colorea.
- Faltaba el campo `license` en `library.properties`, aunque
  `library.json` y el archivo `LICENSE` dicen MIT desde el principio.

### Agregado

- **Aviso cuando `setValue()` no puede publicar.** Una variable de salida
  nunca se publica (se recibe con `onCommand()`), pero `setValue()` sobre
  ella devolvía `true` igual y el valor se quedaba guardado sin salir jamás
  al aire. Ahora la librería lo dice por el monitor serie, una sola vez por
  variable para no inundarlo. Si querés que el equipo informe el estado
  real de un actuador, creá una segunda variable de entrada y publicá esa.
- **Documentación del contrato de comandos**, que antes no estaba escrita
  en ninguna parte: qué publica cada control, cómo leerlo, y la pregunta
  "¿Qué hace esta variable?" del panel que determina si una variable puede
  recibir comandos. En el README y en el comentario de
  `TecnovaCommandCallback`.
- Este CHANGELOG.

### Cómo migrar

1. **Si tu código compara contra texto** (`value["value"] == "true"`),
   cambialo por `value["value"].as<bool>()`. Es el cambio importante.
2. **Si usás la forma defensiva** `v.is<bool>() ? v.as<bool>() :
   (v.as<String>() == "true")`, seguí tranquilo: funciona igual. Ya no hace
   falta, pero no molesta.
3. **Si copiaste el ejemplo `RGBLed`**, tenés que crear en el panel tres
   variables de salida (`rojo`, `verde`, `azul`) con un deslizador cada una,
   en vez de la vieja `color_rgb`.
4. **Si llamabas a `setValue()` sobre una variable de salida**, esa llamada
   nunca publicó nada. Ahora vas a ver el aviso en el monitor serie.
5. **Revisá en el panel** que cada variable de actuador esté marcada como
   "El panel la acciona". Hasta ahora, una variable creada a mano en
   Variables quedaba siempre como "El equipo la mide".

### Nota conocida

La librería usa `StaticJsonDocument` y `DynamicJsonDocument`, deprecados en
ArduinoJson 7 en favor de `JsonDocument`. Compila y funciona con la 7 (se
verificó), pero por ahora la dependencia queda fijada en la 6, que es la
versión con la que está probada. Migrar a la API de la 7 queda pendiente.

## 1.3.0 y anteriores

Este archivo empieza en la 1.4.0. Para lo anterior, ver el historial de
commits y las etiquetas del repositorio.
