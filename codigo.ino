// Importaciones de las Librerias
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <EEPROM.h>

// Pines de Salida
const int PIN_SALA = 2;
const int PIN_COMEDOR = 3;
const int PIN_COCINA = 4;
const int PIN_BANO = 5;
const int PIN_HAB = 6;
const int PIN_FAN = 9;  // Ventilador
const int PIN_SERVO = 10; // Puerta
const int PIN_BOTON_PUERTA = 12; // Botón fisico para la puerta

// Nuevos Pines LED de Estado
const int PIN_LED_AZUL = 7;
const int PIN_LED_VERDE = 8;
const int PIN_LED_ROJO = 11;

// Objetos
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo puertaServo;

// Variables Globales a Utilizar
String comando = "";
bool ventiladorEncendido = false;
bool puertaAbierta = false;
String nombreEscena = "Manual";
bool escenaActiva = false;

// Estado de LEDs y Errores
bool errorSistema = false;
unsigned long tiempoUltimoParpadeoVerde = 0;
int parpadeosVerdesRestantes = 0;
bool estadoVerde = false;
unsigned long tiempoServo = 0;
bool servoEnMovimiento = false;

// EEPROM - Direcciones de memoria
const int DIR_MAGIC = 0;
const int DIR_FAN = 1;
const int DIR_PUERTA = 2;
// Cada escena: 1 byte pasos + 1 byte fan + 16 bytes msg + 75 bytes pasos = 93 bytes.
// Usamos saltos de 100 bytes.
const int DIR_ESCENA_FIESTA = 10;
const int DIR_ESCENA_RELAJADO = 110;
const int DIR_ESCENA_NOCHE = 210;
const int DIR_ESCENA_ENCENDER = 310;
const int DIR_ESCENA_APAGAR = 410;
const int DIR_ESCENA_CUSTOM = 510;

// Variables para el Nuevo Intérprete .org
bool confIniRecibido = false;
int direccionCargaActual = -1;
String mensajeEscenaActual = "";
bool fanInicialEscena = false;

// Variables Para el Sistema de Escenas
bool modoCarga = false;
String nombreNuevaEscena = "";
struct PasoEscena{
  byte pin;
  bool estado;
  unsigned int duracion; // en milisegundos
  byte repeticiones;
};
PasoEscena pasosEscena[15]; 
int totalPasos = 0;

// Variables para ejecucion no bloqueante de escenas
unsigned long ultimosTiempos[15];
int repeticionesActuales[15];
bool estadoLuces[15];
bool escenaIniciada = false;

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100); // Evita que readStringUntil bloquee las escenas

  pinMode(PIN_SALA, OUTPUT);  
  pinMode(PIN_COMEDOR, OUTPUT);  
  pinMode(PIN_COCINA, OUTPUT);  
  pinMode(PIN_BANO, OUTPUT);  
  pinMode(PIN_HAB, OUTPUT);  
  pinMode(PIN_FAN, OUTPUT);  
  
  pinMode(PIN_LED_AZUL, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BOTON_PUERTA, INPUT_PULLUP);

  puertaServo.attach(PIN_SERVO);
  lcd.init();
  lcd.backlight();

  // Inicializar EEPROM si es la primera vez (Formateo)
  if (EEPROM.read(DIR_MAGIC) != 0xAA) {
    Serial.println(F("Inicializando EEPROM por primera vez..."));
    inicializarEEPROM();
  }

  // Recuperar estados anteriores
  recuperarEstado();
  actualizarLCD();

  Serial.println(F("PROYECTO FINAL ORGANIZACION COMPUTACIONAL 1S 2026\n"));
}

void loop() {
  gestionarLEDsStatus();
  verificarBotonPuerta();

  if (Serial.available() > 0){
    comando = Serial.readStringUntil('\n'); 
    comando.trim();
    comando.toUpperCase();
    
    // Ignorar Bluetooth durante carga .org
    if(modoCarga){
      procesarLineaEscena(comando);
    } else {
      interpretarComando(comando);
    }
  }
 
  if (escenaActiva){
    ejecutarEscenaNonBlocking();
  }

  // Auto-Detach del Servo para que se detenga a la fuerza
  if (servoEnMovimiento && (millis() - tiempoServo >= 1000)) {
    puertaServo.detach();
    servoEnMovimiento = false;
  }
}

void interpretarComando(String cmd){
  bool procesado = true;
  
  if (cmd == "L1" || cmd == "L1ON") { digitalWrite(PIN_SALA,HIGH); }
  else if (cmd == "L1OFF") { digitalWrite(PIN_SALA,LOW); }
  else if (cmd == "L2" || cmd == "L2ON") { digitalWrite(PIN_COMEDOR,HIGH); }
  else if (cmd == "L2OFF") { digitalWrite(PIN_COMEDOR,LOW); }
  else if (cmd == "L3" || cmd == "L3ON") { digitalWrite(PIN_COCINA,HIGH); }
  else if (cmd == "L3OFF") { digitalWrite(PIN_COCINA,LOW); }
  else if (cmd == "L4" || cmd == "L4ON") { digitalWrite(PIN_BANO,HIGH); }
  else if (cmd == "L4OFF") { digitalWrite(PIN_BANO,LOW); }
  else if (cmd == "L5" || cmd == "L5ON") { digitalWrite(PIN_HAB,HIGH); }
  else if (cmd == "L5OFF") { digitalWrite(PIN_HAB,LOW); }
  else if (cmd == "ENCENDER_TODO" || cmd == "ENCENDER TODO") { cargarEscenaDesdeEEPROM(DIR_ESCENA_ENCENDER, "Encender Todo"); }
  else if (cmd == "APAGAR_TODO" || cmd == "APAGAR TODO") { cargarEscenaDesdeEEPROM(DIR_ESCENA_APAGAR, "Apagar Todo"); }
  else if (cmd == "FANON" || cmd == "FAN_ON") { setVentilador(true); }
  else if (cmd == "FANOFF" || cmd == "FAN_OFF") { setVentilador(false); }
  else if (cmd == "DOOR") { togglePuerta(); }
  else if (cmd == "DOOROPEN") { moverPuerta(true); }
  else if (cmd == "DOORCLOSE") { moverPuerta(false); }
  else if (cmd == "MODO_FIESTA" || cmd == "FIESTA") { cargarEscenaDesdeEEPROM(DIR_ESCENA_FIESTA, "Fiesta"); }
  else if (cmd == "MODO_RELAJADO" || cmd == "RELAX") { cargarEscenaDesdeEEPROM(DIR_ESCENA_RELAJADO, "Relajado"); }
  else if (cmd == "MODO_NOCHE" || cmd == "NIGHT") { cargarEscenaDesdeEEPROM(DIR_ESCENA_NOCHE, "Noche"); }
  else if (cmd == "LOAD_SCENA") {
    modoCarga = true;
    confIniRecibido = false;
    totalPasos = 0;
    direccionCargaActual = DIR_ESCENA_CUSTOM; // Por defecto
    enviarConfirmacion("ESPERANDO conf_ini...");
  }
  else if(cmd == "STOP"){
    escenaActiva = false;
    escenaIniciada = false;
    nombreEscena = "Manual";
  }
  else if(cmd == "STATUS" || cmd == "ESTADO"){ imprimirEstado(); }
  else if(cmd == "RESET"){ resetSistema(); }
  else if(cmd == "FORMAT"){ inicializarEEPROM(); Serial.println(F("OK: EEPROM RESTAURADA")); }
  else if(cmd == "PLAY_SCENA"){ cargarEscenaDesdeEEPROM(DIR_ESCENA_CUSTOM, "Custom EEPROM"); }
  else {
    procesado = false;
    Serial.println(F("ERROR: Comando Desconocido"));
  }

  if(procesado){
    errorSistema = false; // Limpiar errores previos si el comando fue exitoso
    enviarConfirmacion(cmd);
    actualizarLCD();
    guardarEstadoActual();
  }
}

void enviarConfirmacion(String accion) {
  Serial.print(F("OK: "));
  Serial.println(accion);
  // El LCD se actualiza automaticamente al final de interpretarComando
}

void gestionarLEDsStatus() {
  // LED Azul encendido siempre que el sistema procesa
  digitalWrite(PIN_LED_AZUL, HIGH);
  
  // LED Rojo de Error
  if (errorSistema) {
    digitalWrite(PIN_LED_ROJO, HIGH);
  } else {
    digitalWrite(PIN_LED_ROJO, LOW);
  }

  // LED Verde de Exito (3 parpadeos no bloqueantes)
  if (parpadeosVerdesRestantes > 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - tiempoUltimoParpadeoVerde >= 250) { // 250ms por toggle
      estadoVerde = !estadoVerde;
      digitalWrite(PIN_LED_VERDE, estadoVerde ? HIGH : LOW);
      tiempoUltimoParpadeoVerde = currentMillis;
      if (!estadoVerde) { // Un ciclo completado al apagarse
        parpadeosVerdesRestantes--;
      }
    }
  } else {
    digitalWrite(PIN_LED_VERDE, LOW);
  }
}

void iniciarParpadeoVerde() {
  parpadeosVerdesRestantes = 3;
  estadoVerde = true;
  digitalWrite(PIN_LED_VERDE, HIGH);
  tiempoUltimoParpadeoVerde = millis();
}

void setVentilador(bool encendido){
  digitalWrite(PIN_FAN, encendido ? HIGH : LOW);
  ventiladorEncendido = encendido;
}

void moverPuerta(bool abrir){
  puertaAbierta = abrir;
  if (!puertaServo.attached()) {
    puertaServo.attach(PIN_SERVO);
  }
  puertaServo.write(abrir ? 20 : 0);
  tiempoServo = millis();
  servoEnMovimiento = true;
}

void togglePuerta(){ moverPuerta(!puertaAbierta); }

void verificarBotonPuerta() {
  static bool estadoBotonAnterior = HIGH;
  static bool estadoEstableBoton = HIGH;
  static unsigned long ultimoTiempoRebote = 0;
  const unsigned long tiempoRebote = 50; // 50ms anti-rebote

  bool lecturaBoton = digitalRead(PIN_BOTON_PUERTA);
  if (lecturaBoton != estadoBotonAnterior) {
    ultimoTiempoRebote = millis();
  }

  if ((millis() - ultimoTiempoRebote) > tiempoRebote) {
    if (lecturaBoton != estadoEstableBoton) {
      estadoEstableBoton = lecturaBoton;
      if (estadoEstableBoton == LOW) { // El boton se presiono (conectado a GND)
        togglePuerta();
        actualizarLCD();
        guardarEstadoActual();
        Serial.println(F("OK: BOTON FISICO DETECTADO - PUERTA MOVIDA"));
      }
    }
  }
  estadoBotonAnterior = lecturaBoton;
}

void allOn(){
  digitalWrite(PIN_SALA, HIGH);
  digitalWrite(PIN_COMEDOR, HIGH);
  digitalWrite(PIN_COCINA, HIGH);
  digitalWrite(PIN_BANO, HIGH);
  digitalWrite(PIN_HAB, HIGH);
  digitalWrite(PIN_FAN, HIGH);
}

void allOff(){
  digitalWrite(PIN_SALA, LOW);
  digitalWrite(PIN_COMEDOR, LOW);
  digitalWrite(PIN_COCINA, LOW);
  digitalWrite(PIN_BANO, LOW);
  digitalWrite(PIN_HAB, LOW);
  digitalWrite(PIN_FAN, LOW);
}

void procesarLineaEscena(String linea){ 
  linea.trim();
  if (linea.startsWith("//") || linea.length() == 0) return;

  String lineaUpper = linea;
  lineaUpper.toUpperCase();

  // 1. Validar Inicio
  if (linea.equalsIgnoreCase("conf_ini")) {
    confIniRecibido = true;
    totalPasos = 0;
    mensajeEscenaActual = "Modo: Cargando";
    fanInicialEscena = false;
    Serial.println(F("OK: conf_ini detectado"));
    return;
  }

  if (!confIniRecibido) {
    marcarErrorArchivo();
    return;
  }

  // 2. Identificar Modo (Dirección)
  if (lineaUpper.startsWith("MODO_") || lineaUpper.startsWith("ENCENDER_") || lineaUpper.startsWith("APAGAR_")) {
    if (lineaUpper.indexOf("FIESTA") != -1) { direccionCargaActual = DIR_ESCENA_FIESTA; nombreEscena = "Fiesta"; }
    else if (lineaUpper.indexOf("RELAJ") != -1 || lineaUpper.indexOf("RELAX") != -1) { direccionCargaActual = DIR_ESCENA_RELAJADO; nombreEscena = "Relajado"; }
    else if (lineaUpper.indexOf("NOCHE") != -1 || lineaUpper.indexOf("NIGHT") != -1) { direccionCargaActual = DIR_ESCENA_NOCHE; nombreEscena = "Noche"; }
    else if (lineaUpper.indexOf("ENCENDER_TODO") != -1) { direccionCargaActual = DIR_ESCENA_ENCENDER; nombreEscena = "Encender Todo"; }
    else if (lineaUpper.indexOf("APAGAR_TODO") != -1) { direccionCargaActual = DIR_ESCENA_APAGAR; nombreEscena = "Apagar Todo"; }
    else { direccionCargaActual = DIR_ESCENA_CUSTOM; nombreEscena = "Custom"; }
    return;
  }

  // 3. Mensaje LCD
  if (linea.startsWith("Mensaje en LCD:")) {
    int start = linea.indexOf('"') + 1;
    int end = linea.lastIndexOf('"');
    if (start > 0 && end > start) {
      mensajeEscenaActual = linea.substring(start, end);
    }
    return;
  }

  // 4. Ventilador y Luces Individuales
  if (lineaUpper.startsWith("VENTILADOR:")) {
    fanInicialEscena = (lineaUpper.indexOf("ON") != -1);
    return;
  }
  
  if (lineaUpper.startsWith("SALA:")) { agregarPasoManual(PIN_SALA, lineaUpper.indexOf("ON") != -1, 100, 0); return; }
  if (lineaUpper.startsWith("COMEDOR:")) { agregarPasoManual(PIN_COMEDOR, lineaUpper.indexOf("ON") != -1, 100, 0); return; }
  if (lineaUpper.startsWith("COCINA:")) { agregarPasoManual(PIN_COCINA, lineaUpper.indexOf("ON") != -1, 100, 0); return; }
  if (lineaUpper.startsWith("BANO:")) { agregarPasoManual(PIN_BANO, lineaUpper.indexOf("ON") != -1, 100, 0); return; }
  if (lineaUpper.startsWith("HABITACION:") || lineaUpper.startsWith("HAB:")) { 
    agregarPasoManual(PIN_HAB, lineaUpper.indexOf("ON") != -1, 100, 0); 
    return; 
  }

  // 5. Patrón de LEDs (Alternandose)
  if (linea.startsWith("LED'S:")) {
    if (linea.indexOf("Alternandose") != -1) {
      // Paso 1: Sala y Hab ON, el resto OFF
      agregarPasoManual(PIN_SALA, true, 500, 20);
      agregarPasoManual(PIN_HAB, true, 500, 20);
      agregarPasoManual(PIN_COMEDOR, false, 500, 20);
      agregarPasoManual(PIN_COCINA, false, 500, 20);
      // Paso 2: El inverso (Simulado por la lógica de ejecución que alterna)
    }
    return;
  }

  // 6. Validar Fin
  if (linea.equalsIgnoreCase("conf:fin")) {
    guardarEscenaEnEEPROM(direccionCargaActual);
    modoCarga = false;
    confIniRecibido = false;
    errorSistema = false;
    iniciarParpadeoVerde();
    enviarConfirmacion("CARGA FINALIZADA");
    actualizarLCD();
  }
}

void agregarPasoManual(byte pin, bool estado, int dur, byte rep) {
  if (totalPasos < 15) {
    pasosEscena[totalPasos].pin = pin;
    pasosEscena[totalPasos].estado = estado;
    pasosEscena[totalPasos].duracion = dur;
    pasosEscena[totalPasos].repeticiones = rep;
    totalPasos++;
  }
}

void marcarErrorArchivo() {
  Serial.println(F("Error: Formato .org invalido o falta conf_ini."));
  errorSistema = true; 
  modoCarga = false;
  lcd.clear();
  lcd.print(F("Error: conf_ini"));
}

byte getPinFromAmbiente(String amb){
  if(amb == "SALA") return PIN_SALA;
  if(amb == "COMEDOR") return PIN_COMEDOR;
  if(amb == "COCINA") return PIN_COCINA;
  if(amb == "BANO") return PIN_BANO;
  if(amb == "HABITACION" || amb == "HAB") return PIN_HAB;
  if(amb == "VENTILADOR") return PIN_FAN;
  return 0;
}

void inicializarEEPROM() {
  for (unsigned int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.update(i, 0);
  }
  
  // Guardar "Fiesta"
  totalPasos = 0;
  nombreEscena = "Fiesta";
  mensajeEscenaActual = "Modo: FIESTA";
  fanInicialEscena = false;
  agregarPasoManual(PIN_SALA, true, 500, 40);
  agregarPasoManual(PIN_COMEDOR, false, 500, 40);
  agregarPasoManual(PIN_COCINA, true, 300, 66);
  agregarPasoManual(PIN_BANO, false, 300, 66);
  agregarPasoManual(PIN_HAB, true, 200, 100);
  guardarEscenaEnEEPROM(DIR_ESCENA_FIESTA);

  // Guardar "Relax"
  totalPasos = 0;
  nombreEscena = "Relax";
  mensajeEscenaActual = "Modo: RELAX";
  fanInicialEscena = false;
  agregarPasoManual(PIN_SALA, true, 2000, 5);
  agregarPasoManual(PIN_COMEDOR, true, 2000, 5);
  agregarPasoManual(PIN_HAB, true, 3000, 5);
  guardarEscenaEnEEPROM(DIR_ESCENA_RELAJADO);

  // Guardar "Noche"
  totalPasos = 0;
  nombreEscena = "Noche";
  mensajeEscenaActual = "Modo: NOCHE";
  fanInicialEscena = false;
  agregarPasoManual(PIN_SALA, false, 1000, 1);
  agregarPasoManual(PIN_COMEDOR, false, 1000, 1);
  agregarPasoManual(PIN_COCINA, false, 1000, 1);
  agregarPasoManual(PIN_BANO, false, 1000, 1);
  agregarPasoManual(PIN_HAB, false, 1000, 1);
  guardarEscenaEnEEPROM(DIR_ESCENA_NOCHE);

  // Guardar "Encender Todo" 
  totalPasos = 0;
  nombreEscena = "Encender Todo";
  mensajeEscenaActual = "ENCENDER TODO";
  fanInicialEscena = true;
  agregarPasoManual(PIN_SALA, true, 100, 0);
  agregarPasoManual(PIN_COMEDOR, true, 100, 0);
  agregarPasoManual(PIN_COCINA, true, 100, 0);
  agregarPasoManual(PIN_BANO, true, 100, 0);
  agregarPasoManual(PIN_HAB, true, 100, 0);
  guardarEscenaEnEEPROM(DIR_ESCENA_ENCENDER);

  // Guardar "Apagar Todo"
  totalPasos = 0;
  nombreEscena = "Apagar Todo";
  mensajeEscenaActual = "APAGAR TODO";
  fanInicialEscena = false;
  agregarPasoManual(PIN_SALA, false, 100, 0);
  agregarPasoManual(PIN_COMEDOR, false, 100, 0);
  agregarPasoManual(PIN_COCINA, false, 100, 0);
  agregarPasoManual(PIN_BANO, false, 100, 0);
  agregarPasoManual(PIN_HAB, false, 100, 0);
  guardarEscenaEnEEPROM(DIR_ESCENA_APAGAR);

  // Marcar EEPROM como inicializada
  EEPROM.update(DIR_MAGIC, 0xAA);
  totalPasos = 0;
}

void guardarEscenaEnEEPROM(int dirBase){
  EEPROM.update(dirBase, totalPasos);
  EEPROM.update(dirBase + 1, fanInicialEscena);
  
  // Guardar mensaje LCD (max 16 chars)
  for (unsigned int i = 0; i < 16; i++) {
    char c = (i < mensajeEscenaActual.length()) ? mensajeEscenaActual[i] : ' ';
    EEPROM.update(dirBase + 2 + i, c);
  }

  for(int i = 0; i < totalPasos; i++){
    int addr = dirBase + 18 + (i * 5);
    EEPROM.update(addr, pasosEscena[i].pin);
    EEPROM.update(addr + 1, pasosEscena[i].estado);
    EEPROM.update(addr + 2, lowByte(pasosEscena[i].duracion));
    EEPROM.update(addr + 3, highByte(pasosEscena[i].duracion));
    EEPROM.update(addr + 4, pasosEscena[i].repeticiones);
  }
}

void cargarEscenaDesdeEEPROM(int dirBase, String nombre) {
  totalPasos = EEPROM.read(dirBase);
  if (totalPasos == 255) totalPasos = 0;
  
  fanInicialEscena = EEPROM.read(dirBase + 1);
  
  // Leer mensaje LCD
  mensajeEscenaActual = "";
  for (int i = 0; i < 16; i++) {
    char c = EEPROM.read(dirBase + 2 + i);
    mensajeEscenaActual += c;
  }
  mensajeEscenaActual.trim();

  for(int i = 0; i < totalPasos; i++){
    int addr = dirBase + 18 + (i * 5);
    pasosEscena[i].pin = EEPROM.read(addr);
    pasosEscena[i].estado = EEPROM.read(addr + 1);
    pasosEscena[i].duracion = word(EEPROM.read(addr + 3), EEPROM.read(addr + 2));
    pasosEscena[i].repeticiones = EEPROM.read(addr + 4);
  }
  
  nombreEscena = nombre;
  setVentilador(fanInicialEscena);
  escenaActiva = true;
  escenaIniciada = false;
}

void recuperarEstado(){
  ventiladorEncendido = EEPROM.read(DIR_FAN);
  puertaAbierta = false; // Siempre inicia cerrada según requerimiento
  setVentilador(ventiladorEncendido);
  moverPuerta(puertaAbierta);
}

void guardarEstadoActual(){
  EEPROM.update(DIR_FAN, ventiladorEncendido);
  // La puerta ya no se guarda en EEPROM para que no recuerde su estado
}
void actualizarLCD(){
  if (errorSistema) return; 

  lcd.clear();
  delay(2); // Retraso de seguridad para el controlador HD44780
  lcd.setCursor(0,0);
  if (mensajeEscenaActual != "") {
    lcd.print(mensajeEscenaActual);
  } else {
    lcd.print(F("Modo: "));
    lcd.print(nombreEscena);
  }

  lcd.setCursor(0,1);
  lcd.print(ventiladorEncendido ? F("FAN:ON ") : F("FAN:OFF "));
  lcd.print(puertaAbierta ? F("P:ABR") : F("P:CER"));
}

void imprimirEstado(){
  Serial.println(F("*** ESTADO ACTUAL DEL SISTEMA ***"));
  Serial.print(F("SALA : ")); Serial.println(digitalRead(PIN_SALA) ? F("ON") : F("OFF"));
  Serial.print(F("COMEDOR : ")); Serial.println(digitalRead(PIN_COMEDOR) ? F("ON") : F("OFF"));
  Serial.print(F("COCINA : ")); Serial.println(digitalRead(PIN_COCINA) ? F("ON") : F("OFF"));
  Serial.print(F("BANO : ")); Serial.println(digitalRead(PIN_BANO) ? F("ON") : F("OFF"));
  Serial.print(F("HAB : ")); Serial.println(digitalRead(PIN_HAB) ? F("ON") : F("OFF"));
  Serial.print(F("FAN : ")); Serial.println(ventiladorEncendido ? F("ON") : F("OFF"));
  Serial.print(F("PUERTA : ")); Serial.println(puertaAbierta ? F("ON") : F("OFF"));
  Serial.print(F("ESCENA : ")); Serial.println(nombreEscena);
}

void resetSistema(){
  allOff();
  setVentilador(false);
  moverPuerta(false);
  escenaActiva = false;
  escenaIniciada = false;
  nombreEscena = "Reset";
  Serial.println(F("*** Sistema Reiniciado ***"));
}

void listarEscenas(){
  Serial.println(F("*** Escenas Internas EEPROM ***"));
  Serial.println(F(" >. Fiesta"));
  Serial.println(F(" >. Relax"));
  Serial.println(F(" >. Noche"));
  Serial.println(F(" >. Encender Todo"));
  Serial.println(F(" >. Apagar Todo"));
  Serial.println(F(" >. Custom EEPROM"));
}

void ejecutarEscenaNonBlocking(){
  if (!escenaIniciada) {
    for (int i = 0; i < totalPasos; i++) {
      ultimosTiempos[i] = millis();
      repeticionesActuales[i] = 0;
      estadoLuces[i] = pasosEscena[i].estado;
      if (pasosEscena[i].pin > 0) {
        digitalWrite(pasosEscena[i].pin, estadoLuces[i] ? HIGH : LOW);
      }
    }
    escenaIniciada = true;
  }

  bool algunPasoActivo = false;
  unsigned long tiempoActual = millis();

  for (int i = 0; i < totalPasos; i++) {
    // Si repeticiones es 0, nunca entrara aqui, logrando que el estado sea permanente.
    if (repeticionesActuales[i] < pasosEscena[i].repeticiones) {
      algunPasoActivo = true;
      if (tiempoActual - ultimosTiempos[i] >= pasosEscena[i].duracion) {
        estadoLuces[i] = !estadoLuces[i];
        digitalWrite(pasosEscena[i].pin, estadoLuces[i] ? HIGH : LOW);
        ultimosTiempos[i] = tiempoActual;
        repeticionesActuales[i]++;
      }
    }
  }

  if (!algunPasoActivo && totalPasos > 0) {
    escenaActiva = false;
    escenaIniciada = false;
    
    // Dejar la escena estatica si corresponde a los comandos generales permanentes
    if (nombreEscena != "Todo ON" && nombreEscena != "Todo OFF") {
      nombreEscena = "Manual";
    }
    actualizarLCD();
  }
}
