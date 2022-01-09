#include "Arduino.h"
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_freertos_hooks.h"

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/*RC522-SPI
 SDA-GPIO5
 MISO-GPIO19
 MOSI-GPIO23
 SCK-GPIO18
 RST-EN-Pin9
 /*
 /*DHT11-Serial
 VCC-5V
 GND-0V
 DATA- GPIO4/*
 /*LCD SSD1306 128x32-I2C
 Slave ADDRESS-0X3C
 SDA-GPIO21
 SCL-GPIO22
 VCC-3.3V
 GND-0V
 */

#define mainDELAY_LOOP_COUNT  4000000
#define SS_PIN 5
#define RST_PIN 9
#define DHTPIN 4     // what pin we're connected to
#define DHTTYPE DHT11
#define tag_count 3

#define PWM1_Ch    2
#define PWM1_Res   8
#define PWM1_Freq  15000

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

typedef struct Porfiles {
	int number;
	int temperature[2];
	int fan_speed[2];
	long tag_uid;

} MyPorfiles;

const int Resistor_led = 13;
const int fan_pwm = 16;
const int button = 17;
//const int fan_tachometer = 25;
const int signal_led = 26;
const int onboard_button = 27;
DHT dht(DHTPIN, DHTTYPE);
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
// Inicializalc�o de um array para guardar os bytes do ID serie da tag

void vTask1(void *pvParameters);
void vTask2(void *pvParameters);
void vTask3(void *pvParameters);
//void vTask4(void *pvParameters);
void vTask5(void *pvParameters);
bool my_vApplicationIdleHook(void);
TaskHandle_t xTask2Handle;
SemaphoreHandle_t xBinarySemaphore;
SemaphoreHandle_t xBinarySemaphoreLCD;
const char *Task1_name = "TASK1-READING RFID\r\n"; //Leitura das tags
const char *Task2_name = "TASK2-ATRIBUICAO DE PERFIS\r\n"; //Compara��o da Tag com os perfis
const char *Task3_name = "TASK3-TRATAMENTO DE DADOS\t\n"; //Compara��o da temperatura  e velocidade da ventoinha atuais com os valores estabelecidos nos perfis e atua��o atrav�s de PWM na ventoinha e resit�ncia de aquecimento on/off
const char *IDLE_TASK_name = "TASK IDLE\t\n";
const char *Task5_name = "TASK5- LCD\r\n"; //Display da informa��o

/* Declare a variable of type QueueHandle_t.  This is used to store the queue
 that is accessed by tasks. */
QueueHandle_t xQueue; // tag uid
QueueHandle_t xQueue2; // Queue com os perfis associados
QueueHandle_t xQueue3; // real time fan speed
QueueHandle_t xQueueLCD;
volatile byte half_revolutions;
volatile int display_mode;
void setup() {

	vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
	esp_register_freertos_idle_hook(my_vApplicationIdleHook);

	Wire.begin();
	Serial.begin(9600);
	SPI.begin(); // Init SPI bus
	rfid.PCD_Init(); // Init MFRC522
	dht.begin();
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
	pinMode(Resistor_led, OUTPUT);
	pinMode(signal_led, OUTPUT);
	digitalWrite(signal_led, LOW);
	pinMode(button, OUTPUT);
	attachInterrupt(button, vInterruptHandler, FALLING);
	attachInterrupt(onboard_button, vInterruptLCD, FALLING);
	//attachInterrupt(fan_tachometer, vMeasure_fan_speed, RISING);

	ledcSetup(PWM1_Ch, PWM1_Freq, PWM1_Res);
	ledcAttachPin(fan_pwm, PWM1_Ch);

	xQueue = xQueueCreate(1, sizeof(int));
	xQueue2 = xQueueCreate(1, sizeof(MyPorfiles));
	xQueue3 = xQueueCreate(1, sizeof(float));
	//xQueue4 = xQueueCreate(1, sizeof(int)); Queue para guardar as rpm da ventoinha
	xQueueLCD = xQueueCreate(1, sizeof(MyPorfiles));
	vSemaphoreCreateBinary(xBinarySemaphore);
	vSemaphoreCreateBinary(xBinarySemaphoreLCD);
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);
	display.println("SCE-Projeto");
	display.setCursor(0, 10);
	display.print("Realizado por:");
	display.setCursor(0, 20);
	display.print("Ruben");
	display.setCursor(30, 20);
	display.print("e Rafael ");
	display.display();
	vTaskDelay(5000 / portTICK_PERIOD_MS);
	if (xQueue != NULL && xQueue2 != NULL && xQueue3 != NULL
			&& xQueueLCD != NULL && xBinarySemaphore != NULL
			&& xBinarySemaphore != NULL) {
		xTaskCreatePinnedToCore(vTask1, "RFID Reader Task", 1024,
				(void*) Task1_name, 3, NULL, 1);
		xTaskCreatePinnedToCore(vTask2, "Porfile Selection Task", 1024,
				(void*) Task2_name, 2, &xTask2Handle, 1);
		xTaskCreatePinnedToCore(vTask3, "Data Processing Task", 1024,
				(void*) Task3_name, 3, NULL, 1);

		//xTaskCreatePinnedToCore(my_vApplicationIdleHook, "IDLE Task", 1024, (void*) IDLE_TASK_name, 1, NULL, 1);

		xTaskCreatePinnedToCore(vTask5, "LCD Display Task", 1024,
				(void*) Task5_name, 3, NULL, 1);

	}
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
//void printHex(byte *buffer, byte bufferSize) {
//for (byte i = 0; i < bufferSize; i++) {
//Serial.print(buffer[i] < 0x10 ? " 0" : " ");
//Serial.print(buffer[i], HEX);
//}
//}
/**
 * Helper routine to dump a byte array as dec values to Serial.
 * Converte formato c�digo ASCII para decimal
 *
 */

void vTask1(void *pvParameters) {
	TickType_t xLastWakeTime;
	unsigned portBASE_TYPE uxPriority;
	xLastWakeTime = xTaskGetTickCount();
	// Recebe prioridade da tarefa
	for (;;) {

		Serial.print("\nTASK1 IS RUNNING");

		//Serial.println(F("\nTASK1 IS RUNNING"));
		// Repeat anti collision loop
		//Verifica se existe colis�o entre leituras, entra num loop de verifica��o de anti colos�o

		if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {

			Serial.println(F("Tag:"));
			printDec(rfid.uid.uidByte, rfid.uid.size);
			Serial.println();

			// Halt PICC
			rfid.PICC_HaltA();
			// Stop encryption on PCD
			rfid.PCD_StopCrypto1();
			//vTaskPrioritySet(xTask2Handle, (uxPriority + 2));
			uxPriority = uxTaskPriorityGet( NULL);
			vTaskPrioritySet(xTask2Handle, (uxPriority + 1));
			Serial.print("Raise the Task2 priority to ");
			Serial.println(uxPriority + 1);
			vTaskDelayUntil(&xLastWakeTime, 3000 / portTICK_PERIOD_MS); //executa de 3 em 3 segundos para detetar tags

		}

	}

}

void vTask2(void *pvParameters) {

	char *Task_Name;
	int lReceivedValue;

	unsigned portBASE_TYPE uxPriority;

	TickType_t xLastWakeTime;
	MyPorfiles my_porfiles[3];
	MyPorfiles p_selected;

	my_porfiles[0].number = 605;
	my_porfiles[0].temperature[0] = 20;
	my_porfiles[0].temperature[1] = 21;
	my_porfiles[0].fan_speed[0] = 140;
	my_porfiles[0].fan_speed[1] = 160;
	my_porfiles[0].tag_uid = 39200186180;
	my_porfiles[1].number = 526;
	my_porfiles[1].temperature[0] = 18;
	my_porfiles[1].temperature[1] = 20;
	my_porfiles[1].fan_speed[0] = 100;
	my_porfiles[1].fan_speed[1] = 120;
	my_porfiles[1].tag_uid = 12147227140;
	my_porfiles[2].number = 151;
	my_porfiles[2].temperature[0] = 16;
	my_porfiles[2].temperature[1] = 20;
	my_porfiles[2].fan_speed[0] = 80;
	my_porfiles[2].fan_speed[1] = 100;
	my_porfiles[2].tag_uid = 2365954;

	/*my_porfiles[0] = { 605, (20, 22), (10, 35), 39200186180 };	//White tag
	 my_porfiles[1] = { 526, (22, 24), (36, 70), 12147227140 };	//Blue tag
	 my_porfiles[2] = { 151, (24, 26), (71, 100), 2365954 };   //yellow tag*/

	portBASE_TYPE xStatus;
	portBASE_TYPE xStatus2;
	portBASE_TYPE xStatusLCD;
	xLastWakeTime = xTaskGetTickCount();

	for (;;) {
		Serial.print("\nTASK2 IS RUNNING");

		if (uxQueueMessagesWaiting(xQueue) >= 1)
			xStatus = xQueueReceive(xQueue, &lReceivedValue, 0);

		if (xStatus == pdPASS) {
			uxPriority = uxTaskPriorityGet( NULL);
			Serial.print("\nxQueue data received: ");
			Serial.println(lReceivedValue);

			for (int i = 0; i < tag_count; i++) {
				Serial.print("\nTASK2 IS RUNNING");
				if (lReceivedValue == my_porfiles[i].number) {
					Serial.print("\nPorfile with tag ");
					Serial.print(my_porfiles[i].number);
					Serial.print("\tselected");
					Serial.print("\nDesired temperature");
					Serial.print(my_porfiles[i].temperature[0]);
					Serial.print("-");
					Serial.print(my_porfiles[i].temperature[1]);
					Serial.print("�C");
					Serial.print("\nFan speed range");
					Serial.print(my_porfiles[i].fan_speed[0]);
					Serial.print("-");
					Serial.print(my_porfiles[i].fan_speed[1]);
					Serial.print("%");

					if (i == tag_count
							&& lReceivedValue != my_porfiles[i].number)
						Serial.print("\nTag not recognizable ");

					p_selected = my_porfiles[i];
					xStatus2 = xQueueSendToBack(xQueue2, &p_selected, 0); // queue do perfil selectionado
					xStatusLCD = xQueueSendToBack(xQueueLCD, &p_selected, 0);
					vTaskPrioritySet(xTask2Handle, (uxPriority - 2)); //apos enviar a queue com o perfil reduz a prioridade para deixar as outras correrem
					//vTaskDelayUntil(&xLastWakeTime, (4000 / portTICK_PERIOD_MS));

				}

			}
			//vTaskPrioritySet(xTask2Handle, (uxPriority - 2)); //apos enviar a queue com o perfil reduz a prioridade para deixar as outras correrem
			//vTaskDelayUntil(&xLastWakeTime, (4000 / portTICK_PERIOD_MS));
		}
		vTaskDelayUntil(&xLastWakeTime, (2000 / (portTICK_PERIOD_MS)));
	}

}

void vTask3(void *pvParameters) {
	float t = dht.readTemperature();
	float temp_diff = 0;
	int fan_rpm;
	int PWM_levels = 255;
	TickType_t xLastWakeTime;
	MyPorfiles p;

	portBASE_TYPE xStatus2;
	portBASE_TYPE xStatus3;

	xLastWakeTime = xTaskGetTickCount();
	for (;;) {
		//Serial.println(F("\nTASK3 IS RUNNING"));
		//xSemaphoreTake( xBinarySemaphore, portMAX_DELAY );//portMAX_DELAY

		float t = dht.readTemperature();

		xStatus2 = xQueuePeek(xQueue2, &p, 0);
		//xStatus4 = xQueuePeek(xQueue4, &fan_rpm, 0);

		if (xStatus2 == pdPASS) {

			// caso haja dados na queue vai receber a queue2 na variavel p

			xStatus2 = xQueueReceive(xQueue2, &p, 0);

			if (p.tag_uid != 0) { // caso o uid seja diferente de 0 corresponde a um perfil sen\ao significa que houve reset
				digitalWrite(signal_led, LOW);
				Serial.print("\nxQueue2 data received with tag NUMBER: ");
				Serial.print(p.number);

				Serial.print("\nTemperature read: ");
				Serial.print(t);
				/*if (xStatus4 == pdPASS) {

				 Serial.print("\nxQueue4 RPM count received: ");
				 Serial.print(fan_rpm);
				 Serial.print("RPM");
				 }*/
				float duty_cycle =
						 ((p.temperature[0]+ ((p.temperature[1] - p.temperature[0]) / 2)))*2.55; //Quanto maior o pwm menos velocidade da ventoinha logo a subtra��o

				ledcWrite(PWM1_Ch, (duty_cycle));

				if (t != NAN) {
					xStatus3 = xQueueSendToBack(xQueue3, &t, 0);
					if (t > p.temperature[1]) {

						Serial.print(

								"\nTemperature is too high! Setting heating resistor OFF!");
						digitalWrite(Resistor_led, LOW);
					} else if (t < p.temperature[0]) {
						digitalWrite(Resistor_led, HIGH);
						Serial.print(

								"\nTemperature is too low! Setting heating reasistor ON!");
					}
				} else
					Serial.print(

					"\nTemperature was not read");

			} else {

				ledcWrite(PWM1_Ch, 255); // DESLIGAR FAN
				Serial.print("ISR activated by button");
				digitalWrite(Resistor_led, LOW);
				digitalWrite(signal_led, HIGH);
				Serial.println("XQueue2 received with reset porfile");
			}
		}

	}

	vTaskDelayUntil(&xLastWakeTime, (1500 / (portTICK_PERIOD_MS)));
}

void vTask5(void *pvParameters) {
	float t = 0;

	TickType_t xLastWakeTime;
	MyPorfiles p;

	portBASE_TYPE xStatus2;		// queue do perfil selecionado
	portBASE_TYPE xStatus3;		//queue da temperatura atual

	p.number = 0;
	p.temperature[0] = 0;
	p.temperature[1] = 0;
	p.fan_speed[0] = 0;
	p.fan_speed[1] = 0;
	p.tag_uid = 0;

	xLastWakeTime = xTaskGetTickCount();

	for (;;) {

		xSemaphoreTake(xBinarySemaphoreLCD, 0);
		if (xSemaphoreTake(xBinarySemaphoreLCD, 0) == pdTRUE)
			Serial.print(
					"\nwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww");

		Serial.print("\nTASK5 IS RUNNING");

		xStatus2 = xQueueReceive(xQueueLCD, &p, 0);
		xStatus3 = xQueueReceive(xQueue3, &t, 0);
		//xStatus4 = xQueuePeek(xQueue4, &rpm, 0);
		if (display_mode == 1) { /*se o botao de interrupcao continuar premido seleciona se a informacao
		 no display ser� o perfil selecionado ou os parametros atuais medidos*/

			display.clearDisplay();
			display.setCursor(0, 0);
			display.print("Porfile:");
			display.setCursor(60, 0);						//pos x=10,y=0
			display.println(p.number);
			display.setCursor(0, 10);
			display.print("Temp:");
			display.setCursor(60, 10);						//pos x=15,y=10
			display.println(p.temperature[0]);
			display.setCursor(62, 10);
			display.print("-");
			display.setCursor(70, 10);
			display.println(p.temperature[1]);
			display.setCursor(0, 20);
			display.print("Speed:");
			display.setCursor(60, 20);
			display.println(p.fan_speed[0]);
			display.setCursor(63, 20);
			display.print("-");
			display.setCursor(70, 20);
			display.println(p.fan_speed[1]);
			display.display();

		} else if (display_mode == -1) {

			display.clearDisplay();
			display.setCursor(0, 0);
			display.print("Current Parameters:");
			display.setCursor(0, 10);
			display.print("Temperature:");
			display.setCursor(80, 10);						//pos x=15,y=10
			display.println(t);
			display.setCursor(85, 10);
			display.print("C");
			display.setCursor(0, 20);
			//display.print("Speed:");
			//display.setCursor(10, 20);
			//display.print(rpm);
			display.display();

		}
		vTaskDelayUntil(&xLastWakeTime, (1000 / (portTICK_PERIOD_MS)));

	}
}

bool my_vApplicationIdleHook(void) {

	return true;

}
void loop() {
	vTaskDelete( NULL);
}

void printDec(byte *buffer, byte bufferSize) {

	byte id[bufferSize];
	portBASE_TYPE xStatus;
	int Sum = 0;
	long Tag_UID = 0;
	long Tag_UID2 = 0;
	long Tag_UID3 = 0;

	Tag_UID = (buffer[0]) + (buffer[1] << 8) + (buffer[2] << 16)
			+ (buffer[3] << 24);

	Sum = buffer[0] + buffer[1] + buffer[2] + buffer[3];

	if (bufferSize >= 7) {
		Tag_UID2 = (buffer[4] << 32) + (buffer[5] << 40) + (buffer[6] << 48);

		Sum = Sum + buffer[4] + buffer[5] + buffer[6];

		if (bufferSize == 10) {

			Tag_UID3 = (buffer[7] << 56) + (buffer[8] << 64)
					+ (buffer[9] << 72);
		}
		Sum = Sum + buffer[7] + buffer[8] + buffer[9];

	}

	for (byte i = 0; i < bufferSize; i++) {
		id[i] = (Tag_UID + Tag_UID2 + Tag_UID3) >> 8 * i;
	}

	Sum = buffer[0] + buffer[1] + buffer[2] + buffer[3];

	for (byte i = 0; i < bufferSize; i++) {
		Serial.print(id[i] < 0x10 ? " 0" : " ");
		Serial.print(id[i]);

	}
//Apenas manda para a queue 4 bytes, correspondentes a 1 long, porque as tags que utilizamos s�o apenas de 4 bytes
	xStatus = xQueueSendToBack(xQueue, &Sum, 0);
	Serial.print("\t\tSum:");
	Serial.print(Sum);
	/*Caso se usassem tags de bytes superiores a 4 neste projeto
	 *
	 * xStatus = xQueueSendToBack( xQueue, & Tag_UID, 0 );
	 xStatus = xQueueSendToBack( xQueue, & Tag_UID2, 0 );
	 xStatus = xQueueSendToBack( xQueue, & Tag_UID3, 0 );
	 */
	if (xStatus != pdPASS)
		Serial.print("Could not send to the queue.\r\n");
	if (xStatus == errQUEUE_FULL)
		Serial.print("Queue is full.\r\n");

}

void vInterruptHandler(void) //interrup��o para mudar a tela do lcd (VTASK5)
		{
	static portBASE_TYPE xHigherPriorityTaskWoken;
	MyPorfiles p;
	p.number = 0;
	p.temperature[0] = 0;
	p.temperature[1] = 0;
	p.fan_speed[0] = 0;
	p.fan_speed[1] = 0;
	p.tag_uid = 0;

	xHigherPriorityTaskWoken = pdFALSE;
	xQueueSendToBackFromISR(xQueue2, &p,
			(portBASE_TYPE*)&xHigherPriorityTaskWoken); //enviar para a queue 2 um perfil reset com todos os parametros a 0
	xSemaphoreGiveFromISR(xBinarySemaphore,
			(portBASE_TYPE*)&xHigherPriorityTaskWoken);

	if (xHigherPriorityTaskWoken == pdTRUE) {
		vPortYield();
	}
}
void vInterruptLCD(void) //interrup��o para mudar a tela do lcd (VTASK5)
		{

	static portBASE_TYPE xHigherPriorityTaskWoken;

	if (display_mode == -1)
		display_mode = 1;
	else if (display_mode == 1)
		display_mode = -1;
	else
		display_mode = 1;

	Serial.print("ISR LCD ACTIVATED");
	xHigherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(xBinarySemaphoreLCD,
			(portBASE_TYPE*)&xHigherPriorityTaskWoken);

	if (xHigherPriorityTaskWoken == pdTRUE) {
		vPortYield();
	}

}
void vMeasure_fan_speed(void) {
	portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
	taskENTER_CRITICAL(&myMutex);
	{
		Serial.print(half_revolutions);
		half_revolutions++;
	}
	taskEXIT_CRITICAL(&myMutex);

}

