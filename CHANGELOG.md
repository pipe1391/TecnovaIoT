# Cambios

Las versiones siguen [semver](https://semver.org/lang/es/): `MAYOR.MENOR.PARCHE`.

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
