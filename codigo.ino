// Importaciones de las Librerias
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

// Configuración Bluetooth en pines analógicos
// Conecta: HC-05 TXD -> Arduino A0
// Conecta: HC-05 RXD -> Arduino A1 (Usar divisor de voltaje 1k/2k)
SoftwareSerial BT(A0, A1); 


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
String mensajeLCD_L2 = ""; // Segunda línea personalizada
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
PasoEscena pasosEscena[10]; // Reducido de 15 a 10 para ahorrar RAM
int totalPasos = 0;

// Variables para ejecucion no bloqueante de escenas
unsigned long ultimosTiempos[10];
int repeticionesActuales[10];
bool estadoLuces[10];
bool escenaIniciada = false;

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(500); 
  
  BT.begin(9600); // Inicializar Bluetooth a 9600 baudios
  BT.setTimeout(500);

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
  if (EEPROM.read(DIR_MAGIC) != 0xAB) {
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

  // Leer comandos desde el Monitor Serial (PC)
  if (Serial.available() > 0){
    comando = Serial.readStringUntil('\n'); 
    procesarComandoEntrante(comando);
  }

  // Leer comandos desde el Bluetooth (Celular)
  if (BT.available() > 0){
    comando = BT.readStringUntil('\n');
    procesarComandoEntrante(comando);
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

// Función auxiliar para centralizar el procesamiento de comandos
void procesarComandoEntrante(String cmd) {
  cmd.trim();
  cmd.toUpperCase(); // Volver a hacer que no importe si es mayúscula o minúscula
  
  if(modoCarga){
    procesarLineaEscena(cmd);
  } else {
    interpretarComando(cmd);
  }
}

void interpretarComando(String cmd){
  bool procesado = true;
  
  if (cmd == "SALA: ON") { digitalWrite(PIN_SALA,HIGH); escenaActiva = false; }
  else if (cmd == "SALA: OFF") { digitalWrite(PIN_SALA,LOW); escenaActiva = false; }
  else if (cmd == "SALA") { digitalWrite(PIN_SALA, !digitalRead(PIN_SALA)); escenaActiva = false; }

  else if (cmd == "COMEDOR: ON" ) { digitalWrite(PIN_COMEDOR,HIGH); escenaActiva = false; }
  else if (cmd == "COMEDOR: OFF") { digitalWrite(PIN_COMEDOR,LOW); escenaActiva = false; }
  else if (cmd == "COMEDOR") { digitalWrite(PIN_COMEDOR, !digitalRead(PIN_COMEDOR)); escenaActiva = false; }

  else if (cmd == "COCINA: ON") { digitalWrite(PIN_COCINA,HIGH); escenaActiva = false; }
  else if (cmd == "COCINA: OFF") { digitalWrite(PIN_COCINA,LOW); escenaActiva = false; }
  else if (cmd == "COCINA") { digitalWrite(PIN_COCINA, !digitalRead(PIN_COCINA)); escenaActiva = false; }

  else if (cmd == "BANO: ON") { digitalWrite(PIN_BANO,HIGH); escenaActiva = false; }
  else if (cmd == "BANO: OFF") { digitalWrite(PIN_BANO,LOW); escenaActiva = false; }
  else if (cmd == "BANO") { digitalWrite(PIN_BANO, !digitalRead(PIN_BANO)); escenaActiva = false; }

  else if (cmd == "HABITACION: ON") { digitalWrite(PIN_HAB,HIGH); escenaActiva = false; }
  else if (cmd == "HABITACION: OFF") { digitalWrite(PIN_HAB,LOW); escenaActiva = false; }
  else if (cmd == "HABITACION") { digitalWrite(PIN_HAB, !digitalRead(PIN_HAB)); escenaActiva = false; }

  else if (cmd == "VENTILADOR: ON") { setVentilador(true); escenaActiva = false; }
  else if (cmd == "VENTILADOR: OFF") { setVentilador(false); escenaActiva = false; }
  else if (cmd == "VENTILADOR") { setVentilador(!ventiladorEncendido); escenaActiva = false; }

  else if (cmd == "ENCENDER_TODO") { cargarEscenaDesdeEEPROM(DIR_ESCENA_ENCENDER, "Encender Todo"); }
  else if (cmd == "APAGAR_TODO") { cargarEscenaDesdeEEPROM(DIR_ESCENA_APAGAR, "Apagar Todo"); }

  else if (cmd == "PUERTA: ON") { moverPuerta(true); }
  else if (cmd == "PUERTA: OFF") { moverPuerta(false); }
  else if (cmd == "PUERTA") { togglePuerta(); }

  else if (cmd == "MODO_FIESTA") { cargarEscenaDesdeEEPROM(DIR_ESCENA_FIESTA, "Fiesta"); }
  else if (cmd == "MODO_RELAJADO") { cargarEscenaDesdeEEPROM(DIR_ESCENA_RELAJADO, "Relajado"); }
  else if (cmd == "MODO_NOCHE") { cargarEscenaDesdeEEPROM(DIR_ESCENA_NOCHE, "Noche"); }
  else if (cmd == "CARGAR_ESCENA") {
    modoCarga = true;
    confIniRecibido = false;
    totalPasos = 0;
    direccionCargaActual = DIR_ESCENA_CUSTOM; // Por defecto
    enviarConfirmacion(F("ESPERANDO conf_ini..."));
  }
  else if(cmd == "STOP"){
    escenaActiva = false;
    escenaIniciada = false;
    nombreEscena = "Manual";
  }
  else if(cmd == "ESTADO"){ imprimirEstado(); }
  else if(cmd == "RESET"){ resetSistema(); }
  else if(cmd == "FORMATEAR"){ 
    String msgProgreso = F("PROCESANDO: Borrando memoria EEPROM...");
    Serial.println(msgProgreso);
    BT.println(msgProgreso);
    inicializarEEPROM(); 
    enviarConfirmacion(F("EEPROM RESTAURADA")); 
    procesado = true;
  }
  else if(cmd == "PLAY_SCENA"){ cargarEscenaDesdeEEPROM(DIR_ESCENA_CUSTOM, "Custom EEPROM"); }
  else {
    procesado = false;
    errorSistema = true;
    mensajeEscenaActual = "ERROR:";
    mensajeLCD_L2 = "Modo invalido";
    Serial.println(F("ERROR: Modo invalido"));
    BT.println(F("ERROR: Modo invalido"));
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
  BT.print(F("OK: "));
  BT.println(accion);
}

void enviarConfirmacion(const __FlashStringHelper* accion) {
  Serial.print(F("OK: "));
  Serial.println(accion);
  BT.print(F("OK: "));
  BT.println(accion);
}

void gestionarLEDsStatus() {
  // LED Azul - Efecto de Latido (Heartbeat)
  // Como el Pin 7 no es PWM, usamos un patrón de parpadeo rítmico cada 2 segundos
  unsigned long currentMillis = millis();
  static unsigned long lastHeartbeat = 0;
  if (currentMillis - lastHeartbeat >= 2000) lastHeartbeat = currentMillis;

  unsigned long elapsed = currentMillis - lastHeartbeat;
  // Doble destello corto
  bool heartbeatState = (elapsed < 80) || (elapsed > 200 && elapsed < 280);
  digitalWrite(PIN_LED_AZUL, heartbeatState ? HIGH : LOW);
  
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
  puertaServo.write(abrir ? 90 : 0); // Ajustado a 90 grados para apertura completa
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
        String msgBoton = F("PUERTA ABIERTA / CERRADA");
        Serial.println(msgBoton);
        BT.println(msgBoton);
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

    // Animación estética en LCD al recibir configuración (Punto 3)
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(F("Leyendo .org..."));
    for(int i=0; i<16; i++) {
      lcd.setCursor(i, 1);
      lcd.print((char)0xFF); // Carácter de bloque lleno
      delay(20); 
    }
    delay(200);
    actualizarLCD();

    String msgIni = F("OK: conf_ini detectado");
    Serial.println(msgIni);
    BT.println(msgIni);
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
  if (lineaUpper.startsWith("HABITACION:")) { agregarPasoManual(PIN_HAB, lineaUpper.indexOf("ON") != -1, 100, 0); return; }

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
    mensajeEscenaActual = "Modo: Cargando";
    mensajeLCD_L2 = "";
    fanInicialEscena = false;
    errorSistema = false;
    iniciarParpadeoVerde();
    enviarConfirmacion("CARGA FINALIZADA");
    actualizarLCD();
  }
}

void agregarPasoManual(byte pin, bool estado, int dur, byte rep) {
  if (totalPasos < 10) {
    pasosEscena[totalPasos].pin = pin;
    pasosEscena[totalPasos].estado = estado;
    pasosEscena[totalPasos].duracion = dur;
    pasosEscena[totalPasos].repeticiones = rep;
    totalPasos++;
  }
}

void marcarErrorArchivo() {
  String errorMsg = F("Error: Formato .org invalido o falta conf_ini.");
  Serial.println(errorMsg);
  BT.println(errorMsg);
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
  if(amb == "HABITACION") return PIN_HAB;
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
  mensajeEscenaActual = "MODO: FIESTA.";
  fanInicialEscena = true; // Según tabla: Ventilador ON
  agregarPasoManual(PIN_SALA, true, 500, 40);
  agregarPasoManual(PIN_COMEDOR, false, 500, 40);
  agregarPasoManual(PIN_COCINA, true, 300, 66);
  agregarPasoManual(PIN_BANO, false, 300, 66);
  agregarPasoManual(PIN_HAB, true, 200, 100);
  guardarEscenaEnEEPROM(DIR_ESCENA_FIESTA);
  // Nota: El mensaje LCD se configura en cargarEscenaDesdeEEPROM

  // Guardar "Relax"
  totalPasos = 0;
  nombreEscena = "Relax";
  mensajeEscenaActual = "MODO: RELAX";
  fanInicialEscena = false;
  // Según tabla: LED'S OFF
  agregarPasoManual(PIN_SALA, false, 100, 0);
  agregarPasoManual(PIN_COMEDOR, false, 100, 0);
  agregarPasoManual(PIN_HAB, false, 100, 0);
  guardarEscenaEnEEPROM(DIR_ESCENA_RELAJADO);

  // Guardar "Noche"
  totalPasos = 0;
  nombreEscena = "Noche";
  mensajeEscenaActual = "MODO: NOCHE";
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
  EEPROM.update(DIR_MAGIC, 0xAB);
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
  
  // Configurar mensajes según la tabla de la imagen
  if (dirBase == DIR_ESCENA_FIESTA) {
    mensajeEscenaActual = "Modo: FIESTA";
    mensajeLCD_L2 = "Vent: ON LED: ALT";
  } else if (dirBase == DIR_ESCENA_RELAJADO) {
    mensajeEscenaActual = "Modo: RELAJADO";
    mensajeLCD_L2 = "Vent: OFF LED: OFF";
  } else if (dirBase == DIR_ESCENA_NOCHE) {
    mensajeEscenaActual = "Modo: NOCHE";
    mensajeLCD_L2 = "Vent: OFF LED: OFF";
  } else if (dirBase == DIR_ESCENA_ENCENDER) {
    mensajeEscenaActual = "LED'S: ON";
    mensajeLCD_L2 = "Ventilador: ON";
  } else if (dirBase == DIR_ESCENA_APAGAR) {
    mensajeEscenaActual = "LED'S: OFF.";
    mensajeLCD_L2 = "Ventilador: OFF.";
  } else {
    // Si no es un modo estándar, usa el mensaje de la EEPROM si existe
    mensajeLCD_L2 = ""; 
  }

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
  if (mensajeLCD_L2 != "") {
    lcd.print(mensajeLCD_L2);
  } else {
    lcd.print(ventiladorEncendido ? F("FAN:ON ") : F("FAN:OFF "));
    lcd.print(puertaAbierta ? F("P:ABR") : F("P:CER"));
  }
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

  // Enviar también al Bluetooth para visualización en el celular
  BT.println(F("*** ESTADO ACTUAL DEL SISTEMA ***"));
  BT.print(F("SALA : ")); BT.println(digitalRead(PIN_SALA) ? F("ON") : F("OFF"));
  BT.print(F("COMEDOR : ")); BT.println(digitalRead(PIN_COMEDOR) ? F("ON") : F("OFF"));
  BT.print(F("COCINA : ")); BT.println(digitalRead(PIN_COCINA) ? F("ON") : F("OFF"));
  BT.print(F("BANO : ")); BT.println(digitalRead(PIN_BANO) ? F("ON") : F("OFF"));
  BT.print(F("HAB : ")); BT.println(digitalRead(PIN_HAB) ? F("ON") : F("OFF"));
  BT.print(F("FAN : ")); BT.println(ventiladorEncendido ? F("ON") : F("OFF"));
  BT.print(F("PUERTA : ")); BT.println(puertaAbierta ? F("ON") : F("OFF"));
  BT.print(F("ESCENA : ")); BT.println(nombreEscena);
}

void resetSistema(){
  allOff();
  setVentilador(false);
  moverPuerta(false);
  escenaActiva = false;
  escenaIniciada = false;
  nombreEscena = "Reset";
  String msgReset = F("*** Sistema Reiniciado ***");
  Serial.println(msgReset);
  BT.println(msgReset);
}

void listarEscenas(){
  Serial.println(F("*** Escenas Internas EEPROM ***"));
  Serial.println(F("- Fiesta"));
  Serial.println(F("- Relax"));
  Serial.println(F("- Noche"));
  Serial.println(F("- Encender Todo"));
  Serial.println(F("- Apagar Todo"));
  Serial.println(F("- Custom EEPROM"));
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
