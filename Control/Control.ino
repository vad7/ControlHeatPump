/*
 * Copyright (c) 2016-2021 by Vadim Kulakov vad7@yahoo.com, vad711
 * &                       by Pavel Panfilov <firstlast2007@gmail.com> pav2000
 * "Народный контроллер" для тепловых насосов.
 * Данное програмноое обеспечение предназначено для управления
 * различными типами тепловых насосов для отопления и ГВС.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 */
// Последние версии
// https://github.com/vad7/ControlHeatPump - последняя стабильная версия
// Архивные ссылки
// https://github.com/vad7/Arduino-DUE-WireSam  библиотеки доработанные vad711 при релизе добавляются в архив

#include "Arduino.h"
#include "Constant.h"                       // Должен быть первым !!!! кое что берется в стандартных либах для настройки либ, Config.h входит в него

#include <WireSam.h>                        // vad711 доработанная библиотека https://github.com/vad7/Arduino-DUE-WireSam
#include <DS3232.h>                         // vad711 Работа с часами i2c - используются при выключении питания. модифицированная https://github.com/vad7/Arduino-DUE-WireSam
#include <extEEPROM.h>                      // vad711 Работа с eeprom и fram i2c библиотека доработана для DUE!!! https://github.com/vad7/Arduino-DUE-WireSam

#include <SPI.h>                            // SPI - стандартная
#include "SdFat.h"                          // SdFat - модифицированная
#include "FreeRTOS_ARM.h"                   // модифицированная
#include <rtc_clock.h>                      // работа со встроенными часами  Это основные часы!!!
#include <ModbusMaster.h>                   // Используется МОДИФИЦИРОВАННАЯ для омрона мх2 либа ModbusMaster https://github.com/4-20ma/ModbusMaster

// Глубоко переделанные библиотеки
#include <Ethernet.h>                       // Ethernet - модифицированная
#include <Dns.h>                            // DNS - модифицированная
#include <SerialFlash.h>                    // Либа по работе с spi флешом как диском

#include "Hardware.h"
#include "HeatPump.h"
#include "Nextion.h"
#include "Message.h"
#include "Information.h"
#include "MQTT.h"
#include "Statistics.h"
#include "LCD2004.h"

void vUpdateStepperEEV(void *);
#include "StepMotor.h"

// Мютексы блокираторы железа
SemaphoreHandle_t xModbusSemaphore;                 // Семафор Modbus, инвертор запас на счетчик
SemaphoreHandle_t xWebThreadSemaphore;              // Семафор потоки вебсервера,  деление сетевой карты
SemaphoreHandle_t xI2CSemaphore;                    // Семафор шины I2C, часы, память, мастер OneWire
SemaphoreHandle_t xSPISemaphore;                    // Семафор шины SPI  сетевая карта, память. SD карта // пока не используется
SemaphoreHandle_t xLoadingWebSemaphore;             // Семафор загрузки веб морды в spi память
uint16_t lastErrorFreeRtosCode;                     // код последней ошибки операционки нужен для отладки
uint32_t startSupcStatusReg;                        // Состояние при старте SUPC Supply Controller Status Register - проверяем что с питание


#if defined(W5500_ETHERNET_SHIELD)                  // Задание имени чипа для вывода сообщений
  const char nameWiznet[] ={"W5500"};
#elif defined(W5200_ETHERNET_SHIELD)
  const char nameWiznet[] ={"W5200"};
#else
  const char nameWiznet[] ={"W5100"};
#endif

// Глобальные переменные
EthernetServer server1(80);                         // сервер
#ifdef MQTT
EthernetClient ethClient(W5200_SOCK_SYS);           // для MQTT
#endif

#ifdef RADIO_SENSORS
void check_radio_sensors(void);
void radio_sensor_send(char *cmd);
#endif

#ifdef MQTT                                 // признак использования MQTT
#include <PubSubClient.h>                   // передаланная под многозадачность  http://knolleary.net
PubSubClient w5200_MQTT(ethClient);  	    // клиент MQTT
#endif

// I2C eeprom Размер в килобитах, число чипов, страница в байтах, адрес на шине, тип памяти:
extEEPROM eepromI2C(I2C_SIZE_EEPROM,I2C_MEMORY_TOTAL/I2C_SIZE_EEPROM,I2C_PAGE_EEPROM,I2C_ADR_EEPROM,I2C_FRAM_MEMORY);
#ifdef USE_RC_CLOCK_SOURCE
RTC_clock rtcSAM3X8(RC);                                               // Внутренние часы, используется внутренний RC генератор
#else
RTC_clock rtcSAM3X8(XTAL);                                               // Внутренние часы, используется часовой кварц
#endif
DS3232  rtcI2C;                                                          // Часы 3231 на шине I2C
static Journal  journal;                                                 // системный журнал, отдельно т.к. должен инициализоваться с начала старта
static HeatPump HP;                                                      // Класс тепловой насос (в констукторе плохо проходит инициализация пинов????)
static devModbus Modbus;                                                 // Класс модбас - управление инвертором
SdFat card;                                                              // Карта памяти
#ifdef NEXTION   
Nextion myNextion;                                                     // Дисплей
#endif

// Use the Arduino core to set-up the unused USART2 on Serial4 (without serial events)
#ifdef USE_SERIAL4
RingBuffer rx_buffer5;
RingBuffer tx_buffer5;
USARTClass Serial4(USART2, USART2_IRQn, ID_USART2, &rx_buffer5, &tx_buffer5);
//void serialEvent4() __attribute__((weak));
//void serialEvent4() { }
void USART2_Handler(void)   // Interrupt handler for UART2
{
	Serial4.IrqHandler();     // In turn calls on the Serial2 interrupt handler
}
#endif


// Структура для хранения одного сокета, нужна для организации многопотоковой обработки
#define fABORT_SOCK   0                     // флаг прекращения передачи (произошел сброс сети)
#define fUser         1                     // Признак идентификации как обычный пользователь (нужно подменить файл меню)

struct type_socketData
  {
    EthernetClient client;                  // Клиент для работы с потоком
    byte inBuf[W5200_MAX_LEN];              // буфер для работы с w5200 (входной запрос)
    char *inPtr;                            // указатель в входном буфере с именем файла или запросом (копирование не делаем)
    char outBuf[3*W5200_MAX_LEN];           // буфер для формирования ответа на запрос и чтение с карты
    int8_t sock;                            // сокет потока -1 свободный сокет
    uint8_t flags;                          // Флаги состояния потока
    uint16_t http_req_type;                 // Тип запроса
  };
static type_socketData Socket[W5200_THREAD];   // Требует много памяти 4*W5200_MAX_LEN*W5200_THREAD=24 кб

// Установка вачдога таймера вариант vad711 - адаптация для DUE 1.6.11
// WDT_TIME период Watchdog таймера секунды но не более 16 секунд!!! ЕСЛИ WDT_TIME=0 то Watchdog будет отключен!!!
volatile unsigned long ulHighFrequencyTimerTicks;   // vad711 - адаптация для DUE 1.6.11
void watchdogSetup(void)
{
#if WDT_TIME == 0
	watchdogDisable();
#else 
	watchdogEnable(WDT_TIME * 1000);
#endif
}

__attribute__((always_inline)) inline void _delay(int t) // Функция задержки (мсек) в зависимости от шедулера задач FreeRtos
{
  if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(t/portTICK_PERIOD_MS);
  else delay(t);
}

// Захватить семафор с проверкой, что шедуллер работает
BaseType_t SemaphoreTake(QueueHandle_t xSemaphore, TickType_t xBlockTime)
{
	if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return pdTRUE;
	else {
		for(;;) {
			if(xSemaphoreTake(xSemaphore, 0) == pdTRUE) return pdTRUE;
			if(!xBlockTime--) break;
			vTaskDelay(1/portTICK_PERIOD_MS);
		}
		return pdFALSE;
	}
}

// Освободить семафор с проверкой, что шедуллер работает
inline void SemaphoreGive(QueueHandle_t xSemaphore)
{
	if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreGive(xSemaphore);
}

// Остановить шедулер задач, возврат 1, если получилось
uint8_t TaskSuspendAll(void) {
	if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		vTaskSuspendAll(); // запрет других задач
		return 1;
	}
	return 0;
}

void vWeb0(void *) __attribute__((naked));
void vWeb1(void *) __attribute__((naked));
void vWeb2(void *) __attribute__((naked));
void vWeb3(void *) __attribute__((naked));
void vReadSensor(void *) __attribute__((naked));
void vUpdate( void * ) __attribute__((naked));
void vUpdateEEV(void *) __attribute__((naked));
void vUpdateStepperEEV(void *) __attribute__((naked));
void vUpdateCommand(void *) __attribute__((naked));
void vServiceHP(void *) __attribute__((naked));

void setup() {
	// 1. Инициализация
	// Баг разводки дуе (вероятность). Есть проблема с инициализацией spi.  Ручками прописываем
	// https://groups.google.com/a/arduino.cc/forum/#!topic/developers/0PUzlnr7948
	// http://forum.arduino.cc/index.php?topic=243778.0;nowap
	pinMode(PIN_SPI_SS0,INPUT_PULLUP);          // Eth Pin 77
	pinMode(PIN_SPI_SS1,INPUT_PULLUP);          // SD Pin  87
	pinMode(PIN_SPI_CS_SD,INPUT_PULLUP);        // сигнал CS управление SD картой
	pinMode(PIN_SPI_CS_W5XXX,INPUT_PULLUP);     // сигнал CS управление сетевым чипом

#ifdef SPI_FLASH
	pinMode(PIN_SPI_CS_FLASH,INPUT_PULLUP);     // сигнал CS управление чипом флеш памяти
	pinMode(PIN_SPI_CS_FLASH,OUTPUT);           // сигнал CS управление чипом флеш памяти
#endif
	SPI_switchAllOFF();                         // Выключить все устройства на SPI
	// Отключить питание (VUSB) на Native USB
	Set_bits(UOTGHS->UOTGHS_CTRL, UOTGHS_CTRL_VBUSHWC);
	PIO_Configure(PIOB, PIO_OUTPUT_0, PIO_PB10A_UOTGVBOF, PIO_DEFAULT);
	PIOB->PIO_CODR = PIO_PB10A_UOTGVBOF; // =0
	//

#ifdef POWER_CONTROL                        // Включение питания платы если необходимо НАДП здесь, иначе I2C память рабоать не будет
	pinMode(PIN_POWER_ON,OUTPUT);
	digitalWriteDirect(PIN_POWER_ON, LOW);
#endif

	// Борьба с зависшими устройствами на шине  I2C (в первую очередь часы) неудачный сброс
	Recover_I2C_bus();
	delay(1);

	// Настройка сервисных выводов
	pinMode(PIN_KEY1, INPUT_PULLUP);        // Кнопка 1
	pinMode(PIN_BEEP, OUTPUT);              // Выход на пищалку
	pinMode(PIN_LED_OK, OUTPUT);            // Выход на светодиод мигает 0.5 герца - ОК  с частотой 2 герца ошибка
	digitalWriteDirect(PIN_BEEP,LOW);       // Выключить пищалку
	digitalWriteDirect(PIN_LED_OK,HIGH);    // Выключить светодиод

	// 2. Инициализация журнала
	uint8_t b;
	uint8_t ret = eepromI2C.read(I2C_COUNT_EEPROM, &b, 1);
	if(ret == 0 && b != I2C_COUNT_EEPROM_HEADER && b != 0xFF) {
#ifndef TEST_BOARD
		if(b == 0xAA) {
			if(!eepromI2C.read(I2C_SETTING_EEPROM + 2 + 1, &b, 1) && b < 147) goto xRewriteHeader;
		}
#endif
		ret = 0xFF;
	}
#ifndef DEBUG
	if(ret)
#endif
#ifndef DEBUG_NATIVE_USB
	SerialDbg.begin(UART_SPEED);                   // Если надо инициализировать отладочный порт
#endif
#ifdef LCD2004
	lcd.begin(LCD_COLS, LCD_ROWS); // Setup: cols, rows
	lcd.print("HeatPump v");
	lcd.print(VERSION);
	lcd.setCursor(0, 1);
	lcd.print("(C) vad7@yahoo.com");
	lcd.setCursor(0, 2);
	lcd.print("CFG: ");
	lcd.print(CONFIG_NAME);
	lcd.setCursor(0, 3);
	lcd.print("Loading...");
#endif
	while(ret) {
		SerialDbg.print("Wrong I2C EEPROM or setup, press KEY[D");
		SerialDbg.print(PIN_KEY1);
		SerialDbg.println("] to continue...");
		if(!digitalReadDirect(PIN_KEY1)) {
			WDT_Restart(WDT);
#ifndef TEST_BOARD
xRewriteHeader:
#endif
			b = I2C_COUNT_EEPROM_HEADER;
			ret = eepromI2C.write(I2C_COUNT_EEPROM, &b, 1);
			if(ret) {
				SerialDbg.print("Error ");
				SerialDbg.print(ret);
				SerialDbg.println(" write to EEPROM!");
#ifdef TEST_BOARD
				break;
#endif
			} else SerialDbg.println("Wait...");
			while(1) ;
		}
		for(uint8_t i = 0; i < 1000 / TIME_LED_ERR; i++) {
			digitalWriteDirect(PIN_BEEP, i & 1);
			digitalWriteDirect(PIN_LED_OK, i & 1);
			delay(TIME_LED_ERR);
		}
	}
#ifdef TEST_BOARD
	//_delay(1); // Если зависает при загрузке (START...START...) - включить или выключить эту строку
#endif
	journal.Init();
#ifdef POWER_CONTROL
	delay(200);  // Не понятно но без нее иногда на старте срабатывает вачдог.  возможно проблема с буфером
#endif
#ifdef DEMO
	journal.jprintf("DEMO - DEMO - DEMO - DEMO - DEMO - DEMO - DEMO\n");
#endif
#ifdef TEST_BOARD
	journal.jprintf("\n---> TEST BOARD!!!\n\n");
#endif
	journal.jprintf("Config: %s\n", CONFIG_NAME);
	journal.jprintf("Firmware version: %s\n", VERSION);
	showID();                                                                  // информация о чипе
	getIDchip((char*)Socket[0].inBuf);
	journal.jprintf("Chip ID SAM3X8E: %s\n", Socket[0].inBuf);// информация об серийном номере чипа
	if(GPBR->SYS_GPBR[0] & 0x80000000) GPBR->SYS_GPBR[0] = 0; else GPBR->SYS_GPBR[0] |= 0x80000000; // очистка старой причины
	lastErrorFreeRtosCode = GPBR->SYS_GPBR[0] & 0x7FFFFFFF;         // Сохранение кода ошибки при страте (причину перегрузки)
	journal.jprintf("Last reason for reset SAM3x: %s\n", ResetCause());
	journal.jprintf("Last FreeRTOS task + error: 0x%04x", lastErrorFreeRtosCode);
	if(GPBR->SYS_GPBR[4]) journal.jprintf(" (%d)", GPBR->SYS_GPBR[4]);
	if(GPBR->SYS_GPBR[5]) journal.jprintf(" Web(%d)", GPBR->SYS_GPBR[5]);
	journal.jprintf("\n");

#ifdef PIN_LED1                            // Включение (точнее индикация) питания платы если необходимо
	pinMode(PIN_LED1,OUTPUT);
	digitalWriteDirect(PIN_LED1, HIGH);
#endif
#ifdef POWER_CONTROL
	journal.jprintf("Power +5V, +3.3V on board: ON\n");
#endif

	SupplyMonitorON(SUPC_SMMR_SMTH_3_0V);           // включение монитора питания

#ifdef DRV_EEV_L9333                     // Контроль за работой драйвера ЭРВ
	pinMode(PIN_STEP_DIAG,INPUT_PULLUP);
	journal.jprintf("Control EEV driver L9333: ON \n");
#endif

#ifdef RADIO_SENSORS
	RADIO_SENSORS_SERIAL.begin(RADIO_SENSORS_PSPEED, RADIO_SENSORS_PCONFIG);
#endif

	// 3. Инициализация и проверка шины i2c
	journal.jprintf("1. Setting and checking I2C devices . . .\n");

	uint8_t eepStatus=0;
#ifdef I2C_EEPROM_64KB
	if(journal.get_err()) { // I2C память и журнал в ней уже пытались инициализировать
#endif
		Wire.begin();
		for(uint8_t i=0; i<I2C_NUM_INIT; i++ )
		{
			if ((eepStatus=eepromI2C.begin(I2C_SPEED))>0)    // переходим на I2C_SPEED и пытаемся инициализировать
			{
				journal.jprintf("$ERROR - I2C mem failed, status = %d\n", eepStatus);
#ifdef POWER_CONTROL
				digitalWriteDirect(PIN_POWER_ON, HIGH);
				digitalWriteDirect(PIN_LED1, LOW);
				_delay(2000);
				digitalWriteDirect(PIN_POWER_ON, LOW);
				digitalWriteDirect(PIN_LED1, HIGH);
				_delay(500);
				Wire.begin();
#else
				Wire.begin();
				_delay(500);
#endif
				WDT_Restart(WDT);                       // Сбросить вачдог
			}  else break;   // Все хорошо
		} // for
#ifdef I2C_EEPROM_64KB
	}
#endif
	if(eepStatus)  // если I2C память не инициализирована, делаем попытку запустится на малой частоте один раз!
	{
		if((eepStatus=eepromI2C.begin(twiClock100kHz))>0) {
			journal.jprintf("$ERROR - I2C mem init failed on speed %d kHz, status = %d\n",twiClock100kHz/1000,eepStatus);
			eepromI2C.begin(I2C_SPEED);
			goto x_I2C_init_std_message;
		} else journal.jprintf("I2C bus low speed, init on %d kHz - OK\n",twiClock100kHz/1000);
	} else {
x_I2C_init_std_message:
		journal.jprintf("I2C init on %d kHz - OK\n",I2C_SPEED/1000);
	}

	// Сканирование шины i2c
	//    if (eepStatus==0)   // есть инициализация
	{
		byte error, address;
		const byte start_address = 8;       // lower addresses are reserved to prevent conflicts with other protocols
		const byte end_address = 119;       // higher addresses unlock other modes, like 10-bit addressing

		for(address = start_address; address < end_address; address++ )
		{
#ifdef ONEWIRE_DS2482         // если есть мост
			if(address == I2C_ADR_DS2482) {
				error = OneWireBus.Init();
#ifdef ONEWIRE_DS2482_SECOND
			} else if(address == I2C_ADR_DS2482_2) {
				error = OneWireBus2.Init();
#endif
#ifdef ONEWIRE_DS2482_THIRD
			} else if(address == I2C_ADR_DS2482_3) {
				error = OneWireBus3.Init();
#endif
#ifdef ONEWIRE_DS2482_FOURTH
			} else if(address == I2C_ADR_DS2482_4) {
				error = OneWireBus4.Init();
#endif
			} else
#endif
			{
				Wire.beginTransmission(address); error = Wire.endTransmission();
			}
			if(error==0)
			{
				journal.jprintf("I2C device found at address %s",byteToHex(address));
				switch (address)
				{
#ifdef ONEWIRE_DS2482
				case I2C_ADR_DS2482_4:
				case I2C_ADR_DS2482_3:
				case I2C_ADR_DS2482_2:
				case I2C_ADR_DS2482:  		journal.jprintf(" - OneWire DS2482-100 bus: %d%s\n", address - I2C_ADR_DS2482 + 1, (ONEWIRE_2WAY & (1<<(address - I2C_ADR_DS2482))) ? " (2W)" : ""); break;
#endif
#if I2C_FRAM_MEMORY == 1
				case I2C_ADR_EEPROM:	journal.jprintf(" - FRAM FM24V%02d\n", I2C_MEMORY_TOTAL*10/1024); break;
#if I2C_MEMORY_TOTAL != I2C_SIZE_EEPROM
				case I2C_ADR_EEPROM+1:	journal.jprintf(" - FRAM second 64k page\n"); break;
#endif
#else
				case I2C_ADR_EEPROM:	journal.jprintf(" - EEPROM AT24C%d\n", I2C_SIZE_EEPROM);break; // 0x50 возможны варианты
#if I2C_MEMORY_TOTAL != I2C_SIZE_EEPROM
				case I2C_ADR_EEPROM+1:	journal.jprintf(" - EEPROM second 64k page\n"); break;
#endif
#endif
#ifdef TNTC_EXT
				case I2C_ADR_ADS1115:
				case I2C_ADR_ADS1115_2:		journal.jprintf(" - ADC ADS1115\n"); break;
#endif
				case I2C_ADR_RTC   :		journal.jprintf(" - RTC DS3231\n"); break; // 0x68
				default            :		journal.jprintf(" - Unknow\n"); break; // не определенный тип
				}
				_delay(100);
			}
			//    else journal.jprintf("I2C device bad endTransmission at address %s code %d",byteToHex(address), error);
		} // for
	} //  (eepStatus==0)

#ifndef ONEWIRE_DS2482         // если нет моста
	if(OneWireBus.Init()) journal.jprintf("Error init 1-Wire: %d\n", OneWireBus.GetLastErr());
	else journal.jprintf("1-Wire init Ok\n");
#endif

#ifdef RADIO_SENSORS
	check_radio_sensors();
	if(rs_serial_flag == RS_SEND_RESPONSE) {
		_delay(5);
		check_radio_sensors();
	}
#endif

#ifdef USE_SERIAL4
	PIO_Configure(PIOB, PIO_PERIPH_A, PIO_PB20A_TXD2 | PIO_PB21A_RXD2, PIO_DEFAULT);
	//Serial4.begin(115200);
#endif

	// 4. Инициализировать основной класс
	journal.jprintf("2. Init %s main class . . .\n",(char*)nameHeatPump);
	HP.initHeatPump();                           // Основной класс

#ifndef LCD2004
	// 5. Проверка сброса сети
	// Нажатие при включении - режим safeNetwork (настрока сети по умолчанию 192.168.0.177  шлюз 192.168.0.1, не спрашивает пароль на вход в веб морду)
	journal.jprintf("3. Read safe Network key . . .\n");
	HP.safeNetwork = !digitalReadDirect(PIN_KEY1);
	journal.jprintf(" Mode safeNetwork %s\n", HP.safeNetwork ? "ON" : "OFF");
#endif

	// 6. Чтение ЕЕПРОМ, надо раньше чем инициализация носителей веб морды, что бы знать откуда грузить
	journal.jprintf("4. Load data from I2C memory . . .\n");
	if(HP.load_motoHour()==ERR_HEADER2_EEPROM)           // Загрузить счетчики ТН,
	{
		journal.jprintf(" I2C memory is empty, a default settings will be used!\n");
		HP.save_motoHour();
	} else {
		HP.load((uint8_t *)Socket[0].outBuf, 0);    // Загрузить настройки ТН
		HP.Schdlr.load();							// Загрузка настроек расписания
		HP.Prof.convert_to_new_version();
		int e = HP.Prof.load(HP.Option.numProf);
		if(e < 0) journal.jprintf(" Error %d load profile #%d\n", e, HP.Option.numProf); // Загрузка текущего профиля
		if(HP.Option.ver <= 133) {
			HP.save();
		}
		if(TempAlarm_size == 0) {					// Если настройки ТН пустые, то заполняем лимиты температур
			set_TempAlarmMax(TCOMP, 90);
			set_TempAlarmMax(TBOILER, 85);
		} else {
			for(uint8_t i = 0; i < TempAlarm_size; i++) if(TempAlarm[i].num >= TNUMBER) TempAlarm_remove(i);
		}
	}
	// обновить хеш для пользователей
	WebSec_user.hash = WebSec_admin.hash = NULL;
	calc_WebSec_hash(&WebSec_user, (char*)NAME_USER, HP.get_passUser(), Socket[0].outBuf);
	calc_WebSec_hash(&WebSec_admin, (char*)NAME_ADMIN, HP.get_passAdmin(), Socket[0].outBuf);
#ifdef HTTP_MAP_Server
	journal.jprintf(" Microart Malina server: %s", HTTP_MAP_Server);
 #ifdef HTTP_MAP_Server_Login
	WebSec_Microart.hash = NULL;
	calc_WebSec_hash(&WebSec_Microart, (char*)HTTP_MAP_Server_Login, HP.Option.Microart_pass, Socket[0].outBuf);
 #endif
#endif

	// 7. Инициализация СД карты и запоминание результата 3 попытки
#ifndef NO_SD_CARD
	journal.jprintf("5. Init SD card . . .\n");
	HP.set_fSD(initSD());
	WDT_Restart(WDT);                          // Сбросить вачдог  иногда карта долго инициализируется
#else
	journal.jprintf("5. No SD card in config.\n");
#endif

	// 8. Инициализация spi флеш диска
#ifdef SPI_FLASH
	journal.jprintf("6. Init SPI flash disk . . .\n");
	HP.set_fSPIFlash(initSpiDisk(true));  // проверка диска с выводом инфо
#else
	journal.jprintf("6. No SPI flash in config.\n");
#endif
	digitalWriteDirect(PIN_LED_OK, LOW);        // Включить светодиод

	//  HP.set_optionHP((char*)option_WebOnSPIFlash,0);  // Установить принудительно загрузку морды с карточки (надо раскоментировать если грузится из флеш не надо)
	journal.jprintf("Web interface source: ");
	switch (HP.get_SourceWeb())
	{
	case pMIN_WEB:   journal.jprintf("internal\n"); break;
	case pSD_WEB:    journal.jprintf("SD card\n"); break;
	case pFLASH_WEB: journal.jprintf("SPI Flash\n"); break;
	default:         journal.jprintf("unknown\n"); break;
	}

	journal.jprintf("7. Start read ADC sensors\n");
	//journal.jprintf("Temperature SAM3X8E: %.2f\n", temp_DUE());
	start_ADC(); // после инициализации HP
	//journal.jprintf(" Mask ADC_IMR: 0x%08x\n",ADC->ADC_IMR);

	// 10. Сетевые настройки
	journal.jprintf("8. Setting Network . . .\n");

	if(initW5200(true)) {   // Инициализация сети с выводом инфы в консоль
		W5100.getMACAddress((uint8_t *)Socket[0].outBuf);
		journal.jprintf(" MAC: %s\n", MAC2String((uint8_t *)Socket[0].outBuf));
	}
	digitalWriteDirect(PIN_BEEP,LOW);          // Выключить пищалку

	// 11. Разбираемся со всеми часами и синхронизацией
	journal.jprintf("9. Setting time and clock . . .\n");
	set_time();

	// 12. Инициалазация уведомлений
	journal.jprintf("10. Message update IP from DNS . . .\n");
	HP.message.dnsUpdate();

	// 13. Инициалазация MQTT
#ifdef MQTT
	journal.jprintf("11. Client MQTT update IP from DNS . . .\n");
	HP.clMQTT.dnsUpdate();
#else
	journal.jprintf("11. Client MQTT disabled by config\n");
#endif

	// 14. Инициалазация Statistics
	journal.jprintf("12. Statistics ");
	if(HP.get_fSD()) {
		journal.jprintf("writing on SD card\n");
		Stats.Init();             // Инициализовать статистику
#ifdef WR_LOG_DAYS_POWER_EXCESS
		if(rtcSAM3X8.unixtime() < 1711904400) { // < Sun Mar 31 2024 20:00:00 GMT+0300
			for(uint8_t i = 0; i < sizeof(Stats_data) / sizeof(Stats_data[0]); i++) {
				if(Stats_data[i].object == STATS_OBJ_WattRouter_Excess) {
					Stats_data[i].value = 0;
					WR_Power_Excess = 0;
				}
			}
		}
#endif
	} else journal.jprintf("not available\n");

	int8_t _profile = HP.Schdlr.calc_active_profile();
	if(_profile > SCHDLR_Profile_off && _profile != HP.Prof.get_idProfile()) {
		HP.Prof.load(_profile);
		HP.set_profile();
		journal.jprintf("Profile changed to #%d\n", _profile);
	}

	if(HP.get_SaveON()==0)  HP.set_HP_OFF();    // Сбросить флаг включение ТН если стоит соответсвующий флаг в опциях
	journal.jprintf("13. Delayed start %s: ",(char*)nameHeatPump); if(HP.get_HP_ON()) journal.jprintf("YES\n"); else journal.jprintf("NO\n");

#ifdef NEXTION
	journal.jprintf("14. Nextion display: ");
	if(GETBIT(HP.Option.flags, fNextion)) {
		myNextion.init();
	} else {
		journal.jprintf(" Disabled\n");
	}
//	char enstr1[] = "...";
//	myNextion.Encode_UTF8_to_ISO8859_5((char*)Socket[0].inBuf, enstr1, 100);
//	journal.jprintf("%s=", enstr1);
//	for(uint8_t i = 0; i < strlen((char*)Socket[0].inBuf); i++) journal.jprintf("\\x%02X", Socket[0].inBuf[i]);
//	char enstr2[] = "...";
//	journal.jprintf("\n%s=", enstr2);
//	myNextion.Encode_UTF8_to_ISO8859_5((char*)Socket[0].inBuf, enstr2, 100);
//	for(uint8_t i = 0; i < strlen((char*)Socket[0].inBuf); i++) journal.jprintf("\\x%02X", Socket[0].inBuf[i]);
//	journal.jprintf("\n");
#else
	journal.jprintf("14. Nextion display is absent in config\n");
#endif

#ifdef TEST_BOARD
	// Scan oneWire - TEST.
	//HP.scan_OneWire(Socket[0].outBuf);
#endif

	// Создание задач FreeRTOS  ----------------------
	journal.jprintf("15. Create tasks FreeRTOS . . .\n");
	HP.mRTOS=236;  //расчет памяти для задач 236 - размер данных шедуллера, каждая задача требует 64 байта+ стек (он в словах!!)
	HP.mRTOS=HP.mRTOS+64+4*configMINIMAL_STACK_SIZE;  // задача бездействия
	//HP.mRTOS=HP.mRTOS+4*configTIMER_TASK_STACK_DEPTH;  // программные таймера (их теперь нет)

	// ПРИОРИТЕТ 4 Высший приоритет датчики читаются всегда и шаговик ЭРВ всегда шагает если нужно
	if (xTaskCreate(vReadSensor,"ReadSensor",140,NULL,4,&HP.xHandleReadSensor)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)    set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*140;// 200, до обрезки стеков было 300

#ifdef EEV_DEF
	if (xTaskCreate(vUpdateStepperEEV,"StepperEEV",40,NULL,4,&HP.dEEV.stepperEEV.xHandleStepperEEV)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)  set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*40; // 50, 100, 150, до обрезки стеков было 200
	vTaskSuspend(HP.dEEV.stepperEEV.xHandleStepperEEV);                                 // Остановить задачу
	HP.dEEV.stepperEEV.xCommandQueue = xQueueCreate( EEV_QUEUE, sizeof( int ) );  // Создать очередь комманд для ЭРВ
#endif

	// ПРИОРИТЕТ 3 Очень высокий приоритет Выполнение команд управления (разбор очереди комманд) - должен быть выше чем задачи обновления ТН и ЭРВ
	if(xTaskCreate(vUpdateCommand,"CommandHP", 150,NULL,3,&HP.xHandleUpdateCommand)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)     set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*150;// 200, до обрезки стеков было 300
	vTaskSuspend(HP.xHandleUpdateCommand);      // Остановить задачу разбор очереди комнад


	// ПРИОРИТЕТ 2 высокий - это управление ТН управление ЭРВ, сервис
	if(xTaskCreate(vServiceHP, "ServiceHP", STACK_vUpdateCommand, NULL, 2, &HP.xHandleSericeHP)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*STACK_vUpdateCommand;// 200, до обрезки стеков было 300
#ifdef LCD2004
	if(xTaskCreate(vKeysLCD, "KeysLCD", 90, NULL, 2, &HP.xHandleKeysLCD) == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS = HP.mRTOS+64+4* 90;
#endif

	vSemaphoreCreateBinary(HP.xCommandSemaphore);                       // Создание семафора
	if (HP.xCommandSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);

	if (xTaskCreate(vUpdate,"UpdateHP",160,NULL,2,&HP.xHandleUpdate)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)    set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*160;// 200, до обрезки стеков было 350
	vTaskSuspend(HP.xHandleUpdate);                                 // Остановить задачу обновление ТН
	HP.Task_vUpdate_run = false;

#ifdef EEV_DEF
	if (xTaskCreate(vUpdateEEV,"UpdateEEV",100,NULL,2,&HP.xHandleUpdateEEV)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)     set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*100;
	vTaskSuspend(HP.xHandleUpdateEEV);                              // Остановить задачу обновление EEV
#endif  

	// ПРИОРИТЕТ 1 средний - обслуживание вебморды в несколько потоков и дисплея Nextion
	// ВНИМАНИЕ первый поток должен иметь больший стек для обработки фоновых сетевых задач
	// 1 - поток
	if ( xTaskCreate(vWeb0,"Web0", STACK_vWebX+14,NULL,1,&HP.xHandleUpdateWeb0)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*(STACK_vWebX+14);
#if W5200_THREAD >= 2 // - потока
	if ( xTaskCreate(vWeb1,"Web1", STACK_vWebX,NULL,1,&HP.xHandleUpdateWeb1)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*STACK_vWebX;
#endif
#if W5200_THREAD >= 3 // - потока
	if ( xTaskCreate(vWeb2,"Web2", STACK_vWebX,NULL,1,&HP.xHandleUpdateWeb2)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*STACK_vWebX;
#endif
#if W5200_THREAD >= 4 // - потока
	if ( xTaskCreate(vWeb3,"Web3", STACK_vWebX,NULL,1,&HP.xHandleUpdateWeb3)==errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	HP.mRTOS=HP.mRTOS+64+4*STACK_vWebX;
#endif
	vSemaphoreCreateBinary(xLoadingWebSemaphore);           // Создание семафора загрузки веб морды в spi память
	if (xLoadingWebSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	//xLoadingWebMutex=xSemaphoreCreateMutex();

	vSemaphoreCreateBinary(xWebThreadSemaphore);               // Создание мютекса
	if (xWebThreadSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	vSemaphoreCreateBinary(xI2CSemaphore);                     // Создание мютекса
	if (xI2CSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	//vSemaphoreCreateBinary(xSPISemaphore);                     // Создание мютекса
	//if (xSPISemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	// Дополнительные семафоры (почему то именно здесь) Создается когда есть модбас
	if(Modbus.get_present())
	{
		vSemaphoreCreateBinary(xModbusSemaphore);                       // Создание мютекса
		if (xModbusSemaphore==NULL) set_Error(ERR_MEM_FREERTOS,(char*)nameFREERTOS);
	}

	journal.jprintf(" Create tasks - OK, size %d bytes\n",HP.mRTOS);

	if(HP.get_HP_ON()>0)  HP.sendCommand(pRESTART);  // если надо запустить ТН - отложенный старт

	//journal.jprintf("16. Send a notification . . .\n");
	//HP.message.setMessage(pMESSAGE_RESET,(char*)"Контроллер теплового насоса был сброшен",0);    // сформировать уведомление о сбросе контролла
	journal.jprintf("16. Information:\n");
	freeRamShow();
	HP.startRAM=freeRam()-HP.mRTOS;   // оценка свободной памяти до пуска шедулера, поправка на 1054 байта
	journal.jprintf("FREE MEMORY %d bytes\n",HP.startRAM);
	journal.jprintf("Temperature DS2331: %.2d\n",getTemp_RtcI2C());
	if(Is_otg_vbus_high()) journal.jprintf("USB connected\n");
	//HP.Stat.generate_TestData(STAT_POINT); // Сгенерировать статистику STAT_POINT точек только тестирование
	journal.jprintf("Start FreeRTOS\n");
	journal.jprintf("READY ----------------------\n");
	eepromI2C.use_RTOS_delay = 1;       //vad711
	//
	vTaskStartScheduler();              // СТАРТ !!
	journal.jprintf("FreeRTOS FAILURE!\n");
}

void loop() {}

//  З А Д А Ч И -------------------------------------------------
// Это и есть поток с минимальным приоритетом измеряем простой процессора
//extern "C" 
//{
static bool Error_Beep_confirmed = false;

extern "C" void vApplicationIdleHook(void)  // FreeRTOS expects C linkage
{
	static unsigned long countLED = 0;
	static unsigned long countBeep = 0;

	WDT_Restart(WDT);                                                            // Сбросить вачдог
	register unsigned long ticks = GetTickCount();
//	static unsigned long cpu_idle_max_count = 0; // 1566594 // максимальное значение счетчика, вычисляется при калибровке и соответствует 100% CPU idle
//	static unsigned long ulIdleCycleCount = 0;
//	ulIdleCycleCount++;                                                          // приращение счетчика
//	if(ticks - countLastTick >= 3000)		// мсек
//	{
//		countLastTick = ticks;                            // расчет нагрузки
//		if(ulIdleCycleCount > cpu_idle_max_count) cpu_idle_max_count = ulIdleCycleCount; // это калибровка запоминаем максимальные значения
//		HP.CPU_IDLE = (100 * ulIdleCycleCount) / cpu_idle_max_count;              // вычисляем текущую загрузку
//		ulIdleCycleCount = 0;
//	}
	// Светодиод мигание в зависимости от ошибки и подача звукового сигнала при ошибке
	if(HP.get_errcode() != OK) {          // Ошибка
		if(ticks - countLED > TIME_LED_ERR) {
			digitalWriteDirect(PIN_LED_OK, !digitalReadDirect(PIN_LED_OK));
			countLED = ticks;
		}
		if(HP.get_Beep() && !Error_Beep_confirmed && ticks - countBeep > TIME_BEEP_ERR) {
			digitalWriteDirect(PIN_BEEP, !digitalReadDirect(PIN_BEEP)); // звуковой сигнал
			countBeep = ticks;
		}
	} else if(ticks - countLED > TIME_LED_OK) {   // Ошибок нет и время пришло
		Error_Beep_confirmed = false;
		digitalWriteDirect(PIN_BEEP, LOW);
		digitalWriteDirect(PIN_LED_OK, !digitalReadDirect(PIN_LED_OK));
		countLED = ticks;
	}
	pmc_enable_sleepmode(0);
}

// --------------------------- W E B ------------------------
// Задача обслуживания web сервера
// Сюда надо пихать все что связано с сетью иначе конфликты не избежны
// Здесь также обслуживается посылка уведомлений MQTT
// Первый поток веб сервера - дополнительно нагружен различными сервисами
void vWeb0(void *)
{ //const char *pcTaskName = "Web server is running\r\n";
	static unsigned long timeResetW5200 = 0;
	static unsigned long thisTime;
	static unsigned long resW5200 = 0;
	static unsigned long iniW5200 = 0;
	static unsigned long pingt = 0;
#ifdef HTTP_LowConsumeRequest
	static uint16_t RepeatLowConsumeRequest = 0;
#endif
	static boolean network_last_link = true;
#ifdef MQTT
	static unsigned long narmont=0;
	static unsigned long mqttt=0;
#endif
#ifdef WEATHER_FORECAST
	static uint8_t WF_Day = 0;
#endif
#if defined(HTTP_MAP_RELAY_MAX) && defined(HTTP_MAP_Server)
	static uint32_t daily_http_time = 0;
#endif
#ifdef WATTROUTER
	memset(WR_LoadRun, 0, sizeof(WR_LoadRun));
	memset(WR_SwitchTime, 0, sizeof(WR_SwitchTime));
	for(uint8_t i = 0; i < WR_NumLoads; i++) {
		if(WR_Load_pins[i] > 0) pinMode(WR_Load_pins[i], OUTPUT);
	}
 #ifdef PIN_WR_Boiler_Substitution
	pinMode(PIN_WR_Boiler_Substitution, OUTPUT);
 #endif
#ifdef WR_CHECK_Vbat_INSTEAD_OF_MPPT_SIGN
	if(WR_Read_MAP() == -32768) journal.jprintf("WR: Error read Ubuf\n");
	journal.jprintf("WattRouter started, Ubuf=%.1d\n", WR_MAP_Ubuf);
#else
	journal.jprintf("WattRouter started\n");
#endif
#endif

	HP.timeNTP = thisTime = xTaskGetTickCount();        // В первый момент не обновляем
#ifndef WR_PowerMeter_Modbus
	Web0_FreqTime = thisTime;
#endif
	for(;;)
	{
		#define WEB_SERVER_MAIN_TASK() {\
			/*WEB_STORE_DEBUG_INFO(1);*/\
			web_server(MAIN_WEB_TASK);\
			/*WEB_STORE_DEBUG_INFO(2);*/\
			vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS);\
		}
		WEB_SERVER_MAIN_TASK();

		// СЕРВИС: Этот поток работает на любых настройках, по этому сюда ставим работу с сетью
		boolean active = true;   // ФЛАГ Одно дополнительное действие за один цикл - распределяем нагрузку, если действие проделано то active = false и новый цикл
#ifdef WR_PowerMeter_Modbus		// Синхронизируемся с чтением счетчика - сразу после
		if(WR_PowerMeter_New) {
			WR_PowerMeter_New = false;
#else
		if(xTaskGetTickCount() - Web0_FreqTime > WEB0_FREQUENT_JOB_PERIOD) {
			Web0_FreqTime = xTaskGetTickCount();
#endif
			active = HP.message.sendMessage();   // Отработать отсылку сообщений (внутри скрыта задержка после включения)
#ifdef HTTP_LowConsumeRequest
			if(active) {
				if(GETBIT(HP.Option.flags, fBackupPower) != Request_LowConsume || (RepeatLowConsumeRequest && --RepeatLowConsumeRequest == 0)) {
					strcpy(Socket[MAIN_WEB_TASK].outBuf, HTTP_LowConsumeRequest);
					_itoa(GETBIT(HP.Option.flags, fBackupPower), Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_LowConsumeRequest)-1);
					int err = Send_HTTP_Request(HTTP_LowConsumeServer, WebSec_Microart.hash, Socket[MAIN_WEB_TASK].outBuf, false);
					if(err != -2000000000) {
						if(err > -2000000000) Request_LowConsume = GETBIT(HP.Option.flags, fBackupPower);
						else RepeatLowConsumeRequest = (uint16_t)(HTTP_REQUEST_ERR_REPEAT * 1000 / WEB0_OTHER_JOB_PERIOD + 1);
					}
					active = false;
				}
			}
#endif
#ifdef WATTROUTER
			if(!active) {
				WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
				active = true;
			}
			if((GETBIT(WR.Flags, WR_fActive) || WR.PWM_FullPowerTime || WR_Refresh) /*&& HP.get_State() == pWORK_HP*/) {
				while(1) {
					static uint8_t skip_next_small_increase = 0;
					if(skip_next_small_increase) skip_next_small_increase--;
#ifdef PWM_CALC_POWER_ARRAY
					if(GETBIT(PWM_CalcFlags, PWM_fCalcNow)) break;
#endif
					uint8_t nopwr = GETBIT(WR.Flags, WR_fActive) && (GETBIT(HP.Option.flags, fBackupPower) || HP.NO_Power
#ifdef WR_PowerMeter_Modbus
									|| WR_Error_Read_PowerMeter > WR_Error_Read_PowerMeter_Max
#endif
									 );
#ifdef WR_NEXTION_FULL_SUN
					int8_t mppt = -1;
					if(GETBIT(WR_WorkFlags, WR_fWF_Read_MPPT) || WR_LastSunPowerOutCnt == 0 || nopwr) {
						mppt = WR_Check_MPPT();				// Чтение солнечного контроллера
						SETBIT0(WR_WorkFlags, WR_fWF_Read_MPPT);
						WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
					}
#endif
					// Выключить все
					if(nopwr) {
						WR_Refresh |= WR_Loads;
						// При отсутствии питания мониторим напряжение на АКБ (не должно быть ниже буферного - дельта)
						int16_t Vd;
#if defined(HTTP_MAP_Read_MAP) && defined(WR_NOPWR_READ_MAP_INSTEAD_OF_MPPT)
						active = false;
						Vd = WR_Read_MAP();
#else
						if(WR_MAP_Ubat) Vd = WR_MAP_Ubuf - WR_MAP_Ubat; else Vd = -32768;
#endif //HTTP_MAP_Read_MAP
						if(Vd != -32768) {
							if(Vd <= WR.DeltaUbatmin) { // Можно нагружать
#ifdef WR_Load_pins_Boiler_INDEX
								if(HP.sTemp[TBOILER].get_Temp() <= HP.Prof.Boiler.WR_Target && GETBIT(WR_Loads, WR_Load_pins_Boiler_INDEX)) { // Нужно греть бойлер
									if(Vd <= 0) { // Можно увеличивать нагрузку
										if(WR_LoadRun[WR_Load_pins_Boiler_INDEX] > 0 || HP.sTemp[TBOILER].get_Temp() <= HP.Prof.Boiler.WR_Target - WR_Boiler_Hysteresis) {
											if(GETBIT(WR.PWM_Loads, WR_Load_pins_Boiler_INDEX)) WR_Change_Load_PWM(WR_Load_pins_Boiler_INDEX, WR.LoadAdd);
											else WR_Switch_Load(WR_Load_pins_Boiler_INDEX, 1);
											Vd = WR_NO_POWER_WORK_DELTA_Uacc - 1; // Выключить остальные нагрузки
										}
										goto xNOPWR_OtherLoad;
									} else if(Vd > WR_NO_POWER_WORK_DELTA_Uacc) { // Уменьшаем или выключаем
										if(GETBIT(WR.PWM_Loads, WR_Load_pins_Boiler_INDEX)) WR_Change_Load_PWM(WR_Load_pins_Boiler_INDEX, -WR.LoadAdd);
									}
								} else { // Другая нагрузка
									if(WR_LoadRun[WR_Load_pins_Boiler_INDEX] > 0) {
										if(GETBIT(WR.PWM_Loads, WR_Load_pins_Boiler_INDEX)) WR_Change_Load_PWM(WR_Load_pins_Boiler_INDEX, -32768);
										else WR_Switch_Load(WR_Load_pins_Boiler_INDEX, 0);
									}
#else
								{
#endif
xNOPWR_OtherLoad:					uint32_t t = rtcSAM3X8.unixtime();
									for(uint8_t i = 0; i < WR_NumLoads; i++) { // Управляем еще одной нагрузкой
										if(i == WR_Load_pins_Boiler_INDEX || !GETBIT(WR_Loads, i)) continue;
										if(Vd <= 0) { // Можно увеличивать нагрузку
											if(WR_LoadRun[i] < WR.LoadPower[i]) {
												if(GETBIT(WR.PWM_Loads, i)) {
													WR_Change_Load_PWM(i, WR.LoadAdd);
												} else if(Vd < 0) { // +0.1V на АКБ для реле
													if(WR_SwitchTime[i] && t - WR_SwitchTime[i] <= WR.TurnOnPause) continue;
													WR_Switch_Load(i, 1);
												}
											}
										} else if(Vd > WR_NO_POWER_WORK_DELTA_Uacc) { // Уменьшаем или выключаем
											if(WR_LoadRun[i] > 0) {
												if(GETBIT(WR.PWM_Loads, i)) WR_Change_Load_PWM(i, -WR.LoadAdd); else WR_Switch_Load(i, 0);
											}
										}
										break;
									}
								}
								break; // больше ни чего не делаем
							} // отключаем нагрузку
						}
					}
					if(WR_Refresh || WR.PWM_FullPowerTime) {
						for(uint8_t i = 0; i < WR_NumLoads; i++) {
							//if(!GETBIT(WR_Loads, i)) continue;
							if(GETBIT(WR.PWM_Loads, i)) {
								if(nopwr) {
									if(WR_LoadRun[i] == 0) continue;
									WR_Change_Load_PWM(i, -32768);
								} else if(GETBIT(WR_Refresh, i) || (WR.PWM_FullPowerTime && WR_LoadRun[i] && rtcSAM3X8.unixtime() - WR_SwitchTime[i] > WR.PWM_FullPowerTime * 60)) {
									WR_Change_Load_PWM(i, 0);
								}
							} else if(GETBIT(WR_Refresh, i)) {
								if(nopwr) {
									if(WR_LoadRun[i] == 0) continue;
									WR_Switch_Load(i, 0);
								} else WR_Switch_Load(i, WR_LoadRun[i] ? true : false);
								if(WR_Load_pins[i] < 0) {
									WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
									active = false;
								}
							}
						}
						WR_Refresh = 0;
					}
#ifdef WR_Load_pins_Boiler_INDEX
					if(GETBIT(WR_Loads, WR_Load_pins_Boiler_INDEX) && !HP.dRelay[RBOILER].get_Relay()) {
#ifdef WR_Boiler_Substitution_INDEX
						if(digitalReadDirect(PIN_WR_Boiler_Substitution)) {
							if(WR_LoadRun[WR_Boiler_Substitution_INDEX] == 0 && HP.sTemp[TBOILER].get_Temp() <= HP.Prof.Boiler.WR_Target - WR_Boiler_Hysteresis) {
								digitalWriteDirect(PIN_WR_Boiler_Substitution, 0); // Переключаемся на бойлер
								if(GETBIT(WR.Flags, WR_fLog)) journal.jprintf_time("WR: SW->Boiler\n");
							}
						} else
#endif
						{
							int16_t curr = WR_LoadRun[WR_Load_pins_Boiler_INDEX];
							if(curr > 0) {
								if(WR_TestLoadStatus) {
									if(HP.sTemp[TBOILER].get_Temp() > WR_BOILER_MAX_TEMP) { // Перегрели
										if(GETBIT(WR.PWM_Loads, WR_Load_pins_Boiler_INDEX)) WR_Change_Load_PWM(WR_Load_pins_Boiler_INDEX, -32768);
										else WR_Switch_Load(WR_Load_pins_Boiler_INDEX, 0);
									}
								} else if(HP.sTemp[TBOILER].get_Temp() > HP.Prof.Boiler.WR_Target) { // Нагрели
									active = false;
									if(GETBIT(WR.PWM_Loads, WR_Load_pins_Boiler_INDEX)) WR_Change_Load_PWM(WR_Load_pins_Boiler_INDEX, -32768);
									else WR_Switch_Load(WR_Load_pins_Boiler_INDEX, 0);
									if(GETBIT(WR.Flags, WR_fLog)) journal.jprintf_time("WR: Boiler OK\n");
//									for(uint8_t i = 0; i < 5; i++) { // >1/100 sec
//										WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
//									}
									// Компенсируем
									for(uint8_t i = 0; i < WR_NumLoads; i++) {
										if(i == WR_Load_pins_Boiler_INDEX || !GETBIT(WR_Loads, i) || WR_LoadRun[i] == WR.LoadPower[i]) continue;
										if(GETBIT(WR.PWM_Loads, i)) {
											int16_t chg = WR.LoadPower[i] - WR_LoadRun[i];
											if(chg > curr) chg = curr;
											WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
											WR_Change_Load_PWM(i, chg);
											if(curr == chg) break;
											curr -= chg;
										} else {
											if(WR.LoadPower[i] > curr || (WR_SwitchTime[i] && rtcSAM3X8.unixtime() - WR_SwitchTime[i] <= WR.TurnOnPause)) continue;
											WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
											WR_Switch_Load(i, 1);
											curr -= WR.LoadPower[i];
										}
									}
									break;
								}
							}
						}
					}
#endif
					if(!active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера


#ifdef WR_CurrentSensor_4_20mA
					HP.sADC[IWR].Read();
					int32_t pnet = HP.sADC[IWR].get_Value() * HP.dSDM.get_voltage();
#elif WR_PowerMeter_Modbus
					int32_t pnet = WR_PowerMeter_Power; //round_div_int32(WR_PowerMeter_Power, 10);
					if(pnet == -1) break; // Ошибка
#else
					// HTTP power meter
					active = false;
					int err = Send_HTTP_Request(HTTP_MAP_Server, WebSec_Microart.hash, HTTP_MAP_Read_MAP, 1);
					if(err) {
						if(GETBIT(WR.Flags, WR_fLog) && !GETBIT(Logflags, fLog_HTTP_RelayError)) {
							SETBIT1(Logflags, fLog_HTTP_RelayError);
							journal.jprintf("WR: HTTP request Error %d\n", err);
						}
						break;
					} else SETBIT0(Logflags, fLog_HTTP_RelayError);
					// todo: check "_MODE" >= 3
					char *fld = strstr(Socket[MAIN_WEB_TASK].outBuf, HTTP_MAP_JSON_PNET_calc);
					if(!fld) {
						if(GETBIT(WR.Flags, WR_fLog)) journal.jprintf("WR: HTTP json wrong!\n");
						break;
					}
					char *fld2 = strchr(fld += sizeof(HTTP_MAP_JSON_PNET_calc) + 1, '"');
					if(!fld2) break;
					*(fld2 - 2) = '\0' ; // integer part "0.0"
					int pnet = atoi(fld);
#endif
					//
					if(!GETBIT(WR.Flags, WR_fActive) || nopwr) break;

					int16_t _MinNetLoad = WR.MinNetLoad;
					if(WR.MinNetLoadSunDivider) _MinNetLoad += WR_LastSunPowerOut / WR.MinNetLoadSunDivider;
#ifdef WR_TestAvailablePowerForRelayLoads
					if(WR_TestLoadStatus) { // Тестирование нагрузки
						if(++WR_TestLoadStatus > WR_TestAvailablePowerTime) {
							WR_TestLoadStatus = 0;
#ifdef WR_Boiler_Substitution_INDEX
								uint8_t idx = digitalReadDirect(PIN_WR_Boiler_Substitution) ? WR_Boiler_Substitution_INDEX : WR_TestAvailablePowerForRelayLoads;
#else
								uint8_t idx = WR_TestAvailablePowerForRelayLoads;
#endif
							WR_Change_Load_PWM(idx, -WR.LoadPower[WR_TestLoadIndex]);
							if(pnet <= _MinNetLoad) {
//								for(uint8_t i = 0; i < 5; i++) { // >1/100 sec
//									WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
//								}
								WR_Switch_Load(WR_TestLoadIndex, 1);
								if(GETBIT(WR.Flags, WR_fLogFull)) journal.jprintf("WRT: OK\n");
							} else {
								WR_LastSwitchTime = rtcSAM3X8.unixtime();
								if(GETBIT(WR.Flags, WR_fLogFull)) journal.jprintf("WRT: < %d\n", WR.LoadPower[WR_TestLoadIndex]);
							}
						}
						break;
					} else
#endif
					{
						// Если есть нагрузка, то фильтруем
//						bool need_average;
//						if(pnet < 0) need_average = false;
//						else {
//							need_average = true;
//							if(pnet > _MinNetLoad) {
//								for(int8_t i = 0; i < WR_NumLoads; i++) {
//									if(!GETBIT(WR.PWM_Loads, i) || WR_LoadRun[i] == 0 || !GETBIT(WR_Loads, i)) continue;
//									need_average = false;
//									break;
//								}
//							}
//						}
#ifdef WR_SKIP_EXTREMUM
//						if(need_average) {
							if(WR_Pnet != -32768 && /*abs*/(pnet - WR_Pnet) > WR_SKIP_EXTREMUM) {
								WR_Pnet = -32768;
								if(GETBIT(WR.Flags, WR_fLogFull)) journal.jprintf_time("WR: Skip %d\n", pnet);
								break;
							}
//						}
#endif

	#ifdef WR_PNET_MEDIAN
						if(GETBIT(WR.Flags, WR_fMedianFilter)) {
							// Медианный фильтр на увеличение
							static int median1, median2;
							if(WR_Pnet_avg_init) median1 = median2 = pnet;
							int median3 = pnet;
							if(median3 > median2) { // предыдущее меньше
								if(median1 <= median2 && median1 <= median3) {
									pnet = median2 <= median3 ? median2 : median3;
								} else if(median2 <= median1 && median2 <= median3) {
									pnet = median1 <= median3 ? median1 : median3;
								} else {
									pnet = median1 <= median2 ? median1 : median2;
								}
							}
							median1 = median2;
							median2 = median3;
						}
	#endif
#ifdef WR_PNET_AVERAGE
						if(GETBIT(WR.Flags, WR_fAverage)) {
	#if WR_PNET_AVERAGE == 1
							if(!WR_Pnet_avg_init) pnet = (WR_Pnet_avg + pnet) / 2;
							WR_Pnet_avg = pnet;
	#else
							if(WR_Pnet_avg_init) { // first time
								for(uint8_t i = 0; i < sizeof(WR_Pnet_avg) / sizeof(WR_Pnet_avg[0]); i++) WR_Pnet_avg[i] = pnet;
								WR_Pnet_avg_sum = pnet * int32_t(sizeof(WR_Pnet_avg) / sizeof(WR_Pnet_avg[0]));
							} else {
								WR_Pnet_avg_sum = WR_Pnet_avg_sum - WR_Pnet_avg[WR_Pnet_avg_idx] + pnet;
								WR_Pnet_avg[WR_Pnet_avg_idx] = pnet;
								if(WR_Pnet_avg_idx < sizeof(WR_Pnet_avg) / sizeof(WR_Pnet_avg[0]) - 1) WR_Pnet_avg_idx++; else WR_Pnet_avg_idx = 0;
							}
//							if(need_average)
								pnet = WR_Pnet_avg_sum / int32_t(sizeof(WR_Pnet_avg) / sizeof(WR_Pnet_avg[0]));
//							else WR_Pnet = pnet;
	#endif
						}
#endif
						WR_Pnet_avg_init = false;
					}
					WR_Pnet = pnet; //need_average ? pnet : median3;
#ifndef WR_NEXTION_FULL_SUN
					int8_t mppt = -1;
#endif
					if((WR.Flags & ((1<<WR_fLogFull)|(1<<WR_fLog))) == ((1<<WR_fLogFull)|(1<<WR_fLog))) {
						journal.jprintf("WR: P=%d%c", WR_Pnet, mppt == 0 ? '!' : mppt == 1 ? '+' : mppt == 2 ? '*' : mppt == 3 ? '-': '?');
						if(WR_Pnet != WR_PowerMeter_Power) journal.jprintf("(%d)", WR_PowerMeter_Power);
						journal.jprintf(",%.1d\n", WR_MAP_Ubat);
					}
					// проверка перегрузки
					if(WR_Pnet > _MinNetLoad) { // Потребление из сети больше - уменьшаем нагрузку
						pnet = WR_Pnet - _MinNetLoad + WR.MinNetLoadHyst / 2; // / 2;
						if(pnet <= 0) break;
						for(int8_t i = WR_NumLoads-1; i >= 0; i--) { // PWM only
							if(!GETBIT(WR.PWM_Loads, i) || WR_LoadRun[i] == 0 || !GETBIT(WR_Loads, i)) continue;
#ifdef WR_Load_pins_Boiler_INDEX
							if(i == WR_Load_pins_Boiler_INDEX && HP.dRelay[RBOILER].get_Relay()) continue;
#endif
#ifdef HTTP_MAP_Read_MPPT
							if(mppt == -1) {
								active = false;
								mppt = WR_Check_MPPT();					// Проверка наличия свободного солнца
							}
							if(mppt > 1) break;
#endif
							int chg = WR_LoadRun[i];
							if(chg > pnet && chg - pnet > WR_PWM_POWER_MIN) chg = pnet;
							WR_Change_Load_PWM(i, WR_Adjust_PWM_delta(i, -chg));
							pnet -= chg;
							if(pnet <= 0) break;
						}
						if(pnet > 0 && mppt <= 1) { // not PWM
							uint8_t reserv = 255;
							uint32_t t = rtcSAM3X8.unixtime();
							for(int8_t i = WR_NumLoads-1; i >= 0; i--) {  // Relay only
								if(GETBIT(WR.PWM_Loads, i) || WR_LoadRun[i] == 0 || !GETBIT(WR_Loads, i)) continue;
#ifdef WR_Load_pins_Boiler_INDEX
								if(i == WR_Load_pins_Boiler_INDEX && HP.dRelay[RBOILER].get_Relay()) continue;
#endif
								if(WR_LastSwitchTime && t - WR_LastSwitchTime <= WR.NextSwitchPause) continue;
								if(WR_SwitchTime[i] && t - WR_SwitchTime[i] <= WR.TurnOnMinTime) continue;
#ifdef HTTP_MAP_Read_MPPT
								if(mppt == -1) {
									active = false;
									if((mppt = WR_Check_MPPT()) > 1) break;				// Проверка наличия свободного солнца
								}
#endif
								if(pnet - WR.LoadHist >= WR_LoadRun[i]) {
									if(WR_Load_pins[i] < 0 && !active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
									WR_Switch_Load(i, 0);
									break;
								} else if(reserv == 255) reserv = i;
							}
							if(reserv != 255 && pnet > WR.LoadHist) { // еще не все
								if(WR_Load_pins[reserv] < 0 && !active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
								WR_Switch_Load(reserv, 0);
							}
						}
					} else if(WR_Pnet <= _MinNetLoad - WR.MinNetLoadHyst || mppt == 3) { // Увеличиваем нагрузку
#ifdef WR_Load_pins_Boiler_INDEX
						bool need_heat_boiler =	WR.LoadPower[WR_Load_pins_Boiler_INDEX] - WR_LoadRun[WR_Load_pins_Boiler_INDEX] > 0
												&& (HP.sTemp[TBOILER].get_Temp() <= HP.Prof.Boiler.WR_Target - WR_Boiler_Hysteresis) && !HP.dRelay[RBOILER].get_Relay();
						if(need_heat_boiler) {
							// Переключаемся на нагрев бойлера
							for(int8_t i = 0; i < WR_NumLoads; i++) {
								int16_t lr;
								if(i == WR_Load_pins_Boiler_INDEX || WR_LoadRun[i] == 0 || !GETBIT(WR_Loads, i)) continue;
								if(GETBIT(WR.PWM_Loads, i)) {
									lr = WR_LoadRun[i];
									WR_Change_Load_PWM(i, -(WR.LoadPower[WR_Load_pins_Boiler_INDEX] - WR_LoadRun[WR_Load_pins_Boiler_INDEX]));
									need_heat_boiler = false;
								} else {
									if(WR_SwitchTime[i] && rtcSAM3X8.unixtime() - WR_SwitchTime[i] <= WR.TurnOnMinTime) continue;
									if(WR_Load_pins[i] < 0 && !active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
									lr = WR_LoadRun[i];
									WR_Switch_Load(i, 0);
									need_heat_boiler = false;
								}
								if(!need_heat_boiler) {
									WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
									if(GETBIT(WR.PWM_Loads, WR_Load_pins_Boiler_INDEX)) WR_Change_Load_PWM(WR_Load_pins_Boiler_INDEX, lr);
									else WR_Switch_Load(WR_Load_pins_Boiler_INDEX, lr);

								}
							}
							if(!need_heat_boiler) break;
						}
#endif
						for(int8_t i = 0; i < WR_NumLoads; i++) {
							if(WR_LoadRun[i] == WR.LoadPower[i] || !GETBIT(WR_Loads, i)) continue;
#ifdef WR_Load_pins_Boiler_INDEX
							if(i == WR_Load_pins_Boiler_INDEX) {
								int16_t d = HP.Prof.Boiler.WR_Target - HP.sTemp[TBOILER].get_Temp();
								if(d < 0 || HP.dRelay[RBOILER].get_Relay()) continue;
#ifdef WR_Boiler_Substitution_INDEX
								if(digitalReadDirect(PIN_WR_Boiler_Substitution) && d <= WR_Boiler_Hysteresis) continue; // Если работает замена бойлера и t меньше гистерезиса - пропускаем бойлер
#endif

							}
#endif
							int8_t availidx = 0;
							if(!GETBIT(WR.PWM_Loads, i)) {
								uint32_t t = rtcSAM3X8.unixtime();
								if(WR_LastSwitchTime && t - WR_LastSwitchTime <= WR.NextSwitchPause) continue;
								if(WR_SwitchTime[i] && t - WR_SwitchTime[i] <= WR.TurnOnPause) continue;
								// Расчет мощности, которую можно взять от менее приритетных нагрузок
								for(int8_t ii = i + 1; ii < WR_NumLoads; ii++) {
									if(WR_LoadRun[ii] >= WR.LoadPower[i]) {
										availidx = ii;
										break;
									}
								}
							}
#ifdef HTTP_MAP_Read_MPPT
							if(mppt == -1 && pnet > 0) {
								active = false;
								mppt = WR_Check_MPPT();
							}
							if(mppt == 2 || (mppt == 0 && pnet == 0)) break;	// Проверка наличия свободного солнца
							// Отключено, на КЭС можно рассчитывать только в плане избытка
							//if(mppt == 1 && availidx == 0) break;
#endif
							if(GETBIT(WR.PWM_Loads, i)) {
#ifdef WR_Boiler_Substitution_INDEX
								if(i == WR_Boiler_Substitution_INDEX && WR_LoadRun[WR_Load_pins_Boiler_INDEX] != 0) continue;
#endif
								int16_t chg;
								if(mppt == 0 || mppt == 2) { // Добавляем помаленьку, когда ошибка или пауза у MPPT
									if(skip_next_small_increase) break;
									if(pnet > 0) {
										chg = _MinNetLoad - pnet;
										if(chg < WR_PWM_POWER_MIN) break;
									} else chg = pnet + WR_MIN_LOAD_POWER;
									skip_next_small_increase = 2;
								} else chg = WR.LoadAdd;
								WR_Change_Load_PWM(i, WR_Adjust_PWM_delta(i, chg));
								break;
							} else {
								if(/*mppt <= 1 &&*/ availidx) { // на КЭС можно рассчитывать только в плане избытка
									if(GETBIT(WR.PWM_Loads, availidx)) {
										WR_Change_Load_PWM(availidx, -WR.LoadPower[i]);
									} else {
										if(WR_SwitchTime[i] && rtcSAM3X8.unixtime() - WR_SwitchTime[i] <= WR.TurnOnPause) continue;
										WR_Switch_Load(i, 0);
									}
								// на КЭС можно рассчитывать только в плане избытка
								//} else {
								//	if(mppt < 3 && pnet > 0) continue;
								//	availidx = 0;
								}
#ifdef WR_TestAvailablePowerForRelayLoads
	#if defined(WR_Load_pins_Boiler_INDEX) && WR_TestAvailablePowerForRelayLoads == WR_Load_pins_Boiler_INDEX
		#ifdef WR_Boiler_Substitution_INDEX
								uint8_t idx = digitalReadDirect(PIN_WR_Boiler_Substitution) ? WR_Boiler_Substitution_INDEX : WR_Load_pins_Boiler_INDEX;
								if(availidx == 0 && GETBIT(WR_Loads, idx) && (idx != WR_Load_pins_Boiler_INDEX || (HP.sTemp[TBOILER].get_Temp() < WR_BOILER_MAX_TEMP && !HP.dRelay[RBOILER].get_Relay())) && WR_LoadRun[idx] < WR.LoadPower[idx]) {
		#else
								if(availidx == 0 && GETBIT(WR_Loads, WR_Load_pins_Boiler_INDEX) && (HP.sTemp[TBOILER].get_Temp() < WR_BOILER_MAX_TEMP && !HP.dRelay[RBOILER].get_Relay())) && WR_LoadRun[WR_Load_pins_Boiler_INDEX] < WR.LoadPower[WR_Load_pins_Boiler_INDEX]) {
		#endif
	#else
								uint8_t idx = WR_TestAvailablePowerForRelayLoads;
								if(availidx == 0 && GETBIT(WR_Loads, WR_TestAvailablePowerForRelayLoads) && WR_LoadRun[WR_TestAvailablePowerForRelayLoads] < WR.LoadPower[WR_TestAvailablePowerForRelayLoads]) {
	#endif
									WR_TestLoadIndex = i;
									WR_TestLoadStatus = 1;
									WR_Change_Load_PWM(idx, WR.LoadPower[i]);
									break;
								}
#endif
								if(WR_Load_pins[i] < 0 && !active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
								WR_Switch_Load(i, 1);
								break;
							}
						}
					}
					break;
				}
			} else { // NOT (GETBIT(WR.Flags, WR_fActive) || WR.PWM_FullPowerTime || WR_Refresh))
				WR_Pnet = WR_PowerMeter_Power;
				if(GETBIT(WR_WorkFlags, WR_fWF_Read_MPPT)) {
					WR_Check_MPPT();				// Чтение солнечного контроллера
					SETBIT0(WR_WorkFlags, WR_fWF_Read_MPPT);
					WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
				} else WR_LastSunSign = -1;
				if((WR.Flags & ((1<<WR_fLogFull)|(1<<WR_fLog))) == ((1<<WR_fLogFull)|(1<<WR_fLog))) {
					journal.jprintf("WR: P=%d%c", WR_Pnet, WR_LastSunSign == 0 ? '!' : WR_LastSunSign == 1 ? '+' : WR_LastSunSign == 2 ? '*' : WR_LastSunSign == 3 ? '-': '?');
					if(WR_Pnet != WR_PowerMeter_Power) journal.jprintf("(%d)", WR_PowerMeter_Power);
					journal.jprintf(",%.1d(%.1d)\n", WR_MAP_Ubat, WR_MAP_Ubuf);
				}
			}
	#ifdef RSOLINV
			if(HP.dRelay[RSOLINV].get_Relay()) { 	// Relay is ON
#ifdef WR_INVERTOR2_SUN_OFF_WHEN_NO_WORK
				if(HP.is_compressor_on()) WR_Invertor2_off_cnt = 0;
#endif
				if(WR_Invertor2_off_cnt > WR_INVERTOR2_SUN_OFF_TIMER || GETBIT(WR_WorkFlags, WR_fWF_Charging_BAT)) {
					HP.dRelay[RSOLINV].set_Relay(fR_StatusAllOff);
					WR_Invertor2_off_cnt = 0;
				} else if(WR_LastSunPowerOut <= 10 || rtcSAM3X8.get_hours() >= WR_INVERTOR2_SUN_OFF_HOUR) WR_Invertor2_off_cnt++;
				else if(WR_Invertor2_off_cnt) WR_Invertor2_off_cnt--;
			} else { 								// Relay is OFF
				if(WR_LastSunPowerOut > WR_INVERTOR2_SUN_PWR_ON && WR_Pnet > 0 && WR_LastSunPowerOut > WR_Pnet && !GETBIT(WR_WorkFlags, WR_fWF_Charging_BAT)) {
					if(GETBIT(WR.Flags, WR_fLog)) journal.jprintf("WR: Sun=%d, Net=%d\n", WR_LastSunPowerOut, WR_Pnet);
					HP.dRelay[RSOLINV].set_ON();
					WR_Invertor2_off_cnt = 0;
				}
			}
	#endif
			//
#endif // WATTROUTER
		}
		if(xTaskGetTickCount() - thisTime > WEB0_OTHER_JOB_PERIOD)
		{
			if(!active) {
				WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
				active = true;
			}
			thisTime = xTaskGetTickCount();                                      // Запомнить тики
			if(active) active = HP.message.dnsUpdate();                          // Обновить адреса через dns если надо, dnsUpdate() возвращает true если обновления не было
#ifdef MQTT
			if(active) active=HP.clMQTT.dnsUpdate();                             // Обновить адреса через dns если надо для MQTT если обновления не было то возвращает true
#endif
			// 1. Проверка захваченого семафора сети ожидаем  3 времен W5200_TIME_WAIT если мютекса не получаем то сбрасывае мютекс
			if(SemaphoreTake(xWebThreadSemaphore, ((3 + (fWebUploadingFilesTo != 0) * 30) * W5200_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
				SemaphoreGive(xWebThreadSemaphore);
				journal.jprintf_time("UNLOCK mutex xWebThread\n");
				active = false;
				HP.num_resMutexSPI++;
			} // Захват мютекса SPI или ОЖИДАНИНЕ 2 времен W5200_TIME_WAIT и его освобождение
			else SemaphoreGive(xWebThreadSemaphore);

			// Проверка и сброс митекса шины I2C  если мютекса не получаем то сбрасывае мютекс
			if(SemaphoreTake(xI2CSemaphore, (3 * I2C_TIME_WAIT / portTICK_PERIOD_MS)) == pdFALSE) {
				SemaphoreGive(xI2CSemaphore);
				journal.jprintf_time("UNLOCK mutex xI2CSemaphore\n");
				HP.num_resMutexI2C++;
			} // Захват мютекса I2C или ОЖИДАНИНЕ 3 времен I2C_TIME_WAIT  и его освобождение
			else SemaphoreGive(xI2CSemaphore);

			// 2. Чистка сокетов
			if(HP.time_socketRes() > 0) {
				WEB_STORE_DEBUG_INFO(3);
				checkSockStatus();                   // Почистить старые сокеты  если эта позиция включена
			}

			// 3. Сброс сетевого чипа по времени
			if((HP.time_resW5200() > 0) && (active))                             // Сброс W5200 если включен и время подошло
			{
				WEB_STORE_DEBUG_INFO(4);
				resW5200 = xTaskGetTickCount();
				if(timeResetW5200 == 0) timeResetW5200 = resW5200;      // Первая итерация не должна быть сразу
				if(resW5200 - timeResetW5200 > HP.time_resW5200() * 1000UL) {
					journal.jprintf_time("Reset %s by timer . . .\n", nameWiznet);
					HP.sendCommand(pNETWORK);                          // Послать команду сброса и применения сетевых настроек
					timeResetW5200 = resW5200;                         // Запомить время сброса
					active = false;
				}
			}
			// 4. Проверка связи с чипом
			if((HP.get_fInitW5200()) && (thisTime - iniW5200 > 60 * 1000UL) && (active)) // проверка связи с чипом сети раз в минуту
			{
				WEB_STORE_DEBUG_INFO(5);
				iniW5200 = thisTime;
				if(!HP.NO_Power) {
					boolean lst = linkStatusWiznet(false);
					if(!lst || !network_last_link) {
						if(!lst) journal.jprintf_time("%s no link[%02X], resetting . . .\n", nameWiznet, W5100.readPHYCFGR());
						HP.sendCommand(pNETWORK);       // Если связь потеряна то подать команду на сброс сетевого чипа
						HP.num_resW5200++;              // Добавить счетчик инициализаций
						active = false;
					}
					network_last_link = lst;
				}
			}
			// 5.Обновление времени 1 раз в сутки или по запросу (HP.timeNTP==0)
			if((HP.timeNTP == 0) || ((HP.get_updateNTP()) && (thisTime - HP.timeNTP > 60 * 60 * 24 * 1000UL) && (active))) // Обновление времени раз в день 60*60*24*1000 в тиках HP.timeNTP==0 признак принудительного обновления
			{
				WEB_STORE_DEBUG_INFO(6);
				HP.timeNTP = thisTime;
				if(HP.get_UpdateByHTTP()) set_time_HTTP(true); else set_time_NTP(true);
				active = false;
			}
			// 6. ping сервера если это необходимо
			if((HP.get_pingTime() > 0) && (thisTime - pingt > HP.get_pingTime() * 1000UL) && (active)) {
				WEB_STORE_DEBUG_INFO(7);
				pingt = thisTime;
				pingServer();
				active = false;
			}

#ifdef MQTT                                     // признак использования MQTT
			// 7. Отправка нанародный мониторинг
			if ((HP.clMQTT.get_NarodMonUse())&&(thisTime-narmont>TIME_NARMON*1000UL)&&(active))// если нужно & время отправки пришло
			{
				WEB_STORE_DEBUG_INFO(55);
				narmont=thisTime;
				sendNarodMon(false);                       // отладка выключена
				active=false;
			}  // if ((HP.clMQTT.get_NarodMonUse()))

			// 8. Отправка на MQTT сервер
			if ((HP.clMQTT.get_MqttUse())&&(thisTime-mqttt>HP.clMQTT.get_ttime()*1000UL)&&(active))// если нужно & время отправки пришло
			{
				WEB_STORE_DEBUG_INFO(56);
				mqttt=thisTime;
				if(HP.clMQTT.get_TSUse()) sendThingSpeak(false);
				else sendMQTT(false);
				active=false;
			}
#endif   // MQTT

#ifdef WEATHER_FORECAST
			if(active && rtcSAM3X8.get_days() != WF_Day) {
				WF_BoilerTargetPercent = 100;
				if(rtcSAM3X8.get_hours() == WR.WF_Time / 10 && rtcSAM3X8.get_minutes() >= (WR.WF_Time % 10) * 10 && strlen(HP.Option.WF_ReqServer)) {
					if(!active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
					int err = Send_HTTP_Request(HP.Option.WF_ReqServer, NULL, HP.Option.WF_ReqText, 4);
					if(err == OK) err = WF_ProcessForecast(Socket[MAIN_WEB_TASK].outBuf);
					if(err == OK) WF_Day = rtcSAM3X8.get_days();
					else {
						if(rtcSAM3X8.get_minutes() == 59 && rtcSAM3X8.get_seconds() >= 59 - uint8_t(WEB0_OTHER_JOB_PERIOD / 1000)) {
							journal.jprintf_time("WF: Request Error %d\n", err);
							HP.message.setMessage(pMESSAGE_ERROR,(char*)"Ошибка получения прогноза погоды", err);
						}
					}
					active = false;
				}
			}
#endif
#if defined(HTTP_MAP_RELAY_MAX) && defined(HTTP_MAP_Server)
			if(HP.IsWorkingNow()) {
				if(!active) {
					WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
					active = true;
				}
				uint32_t t = rtcSAM3X8.unixtime();
				if(t - daily_http_time > 600UL) { // дискретность 10 минут
					daily_http_time = t;
					daily_http_time -= daily_http_time % 600;
					if(DailySwitch_on & DailySwitch_on_MASK_OFF) { // Выкл. после смены профиля
						for(uint8_t i = 1; i < 8; i++) {
							if(DailySwitch_on & (1<<(i + 24 - 1))) {
								strcpy(Socket[MAIN_WEB_TASK].outBuf, HTTP_MAP_RELAY_SW_1);
								_itoa(i, Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_MAP_RELAY_SW_1)-1);
								strcat(Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_MAP_RELAY_SW_1)-1, HTTP_MAP_RELAY_SW_2);
								_itoa(0, Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_MAP_RELAY_SW_1)-1 + sizeof(HTTP_MAP_RELAY_SW_2)-1); // OFF
								if(Send_HTTP_Request(HTTP_MAP_Server, WebSec_Microart.hash, Socket[MAIN_WEB_TASK].outBuf, 3) == 1) { // Ok?
									DailySwitch_on ^= (1<<(i + 24 - 1));
									SETBIT0(Logflags, fLog_HTTP_RelayError);
									//journal.jprintf_time("Relay HTTP-%d: %s\n", rel, ds ? "ON" : "OFF");
								} else {
									if((HP.get_NetworkFlags() & ((1<<fWebLogError) | (1<<fWebFullLog))) && !GETBIT(Logflags, fLog_HTTP_RelayError)) {
										SETBIT1(Logflags, fLog_HTTP_RelayError);
										journal.jprintf(". Fail set HTTP-%d relay!\n", i);
									}
								}
								WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
							}
						}
					}
					uint32_t hhmm = rtcSAM3X8.get_hours() * 100 + rtcSAM3X8.get_minutes();
					typeof(DailySwitch_on) _dson = 0;
					for(uint8_t i = 0; i < DAILY_SWITCH_MAX; i++) {
						if(HP.Prof.DailySwitch[i].Device == 0) break;
						if(HP.Prof.DailySwitch[i].Device < RNUMBER) continue;
						int8_t ds = HP.NO_Power || GETBIT(HP.Option.flags, fBackupPower) ? 0 : HP.Prof.check_DailySwitch(i, hhmm);
						_dson |= (ds < 0 ? GETBIT(DailySwitch_on, i) : ds)<<i;
					}
					for(uint8_t i = 0; i < DAILY_SWITCH_MAX; i++) {
						if(HP.Prof.DailySwitch[i].Device == 0) break;
						if(HP.Prof.DailySwitch[i].Device < RNUMBER) continue;
						if(GETBIT((_dson ^ DailySwitch_on), i)) {
							if(!GETBIT(_dson, i)) { // off
								uint8_t j = i + 1;
								for(; j < DAILY_SWITCH_MAX; j++) {
									if(HP.Prof.DailySwitch[i].Device == HP.Prof.DailySwitch[j].Device && GETBIT(_dson, j)) break;
								}
								if(j < DAILY_SWITCH_MAX) { // Еще есть тоже реле или реле уже включено
									SETBIT0(DailySwitch_on, i);
									continue;
								}
							}
							strcpy(Socket[MAIN_WEB_TASK].outBuf, HTTP_MAP_RELAY_SW_1);
							uint32_t rel = HP.Prof.DailySwitch[i].Device - RNUMBER + 1;
							_itoa(rel, Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_MAP_RELAY_SW_1)-1);
							strcat(Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_MAP_RELAY_SW_1)-1, HTTP_MAP_RELAY_SW_2);
							_itoa(GETBIT(_dson, i), Socket[MAIN_WEB_TASK].outBuf + sizeof(HTTP_MAP_RELAY_SW_1)-1 + sizeof(HTTP_MAP_RELAY_SW_2)-1);
							if(Send_HTTP_Request(HTTP_MAP_Server, WebSec_Microart.hash, Socket[MAIN_WEB_TASK].outBuf, 3) == 1) { // Ok?
								DailySwitch_on = (DailySwitch_on & ~(1<<i)) | (_dson & (1<<i));
								SETBIT0(Logflags, fLog_HTTP_RelayError);
								//journal.jprintf_time("Relay HTTP-%d: %s\n", rel, ds ? "ON" : "OFF");
							} else {
								if((HP.get_NetworkFlags() & ((1<<fWebLogError) | (1<<fWebFullLog))) && !GETBIT(Logflags, fLog_HTTP_RelayError)) {
									SETBIT1(Logflags, fLog_HTTP_RelayError);
									journal.jprintf(". Fail set HTTP-%d relay!\n", rel);
								}
							}
							active = false;
						}
					}
					if(!active) WEB_SERVER_MAIN_TASK();	/////////////////////////////////////// Выполнить задачу веб сервера
#ifdef WR_CHECK_Vbat_INSTEAD_OF_MPPT_SIGN
					WR_Read_MAP();
#endif
				}
			}
#endif

			taskYIELD();
		} // if (xTaskGetTickCount()-thisTime>10000)

	} //for
	vTaskDelete( NULL);
}

// Второй поток
void vWeb1(void *)
{ //const char *pcTaskName = "Web server is running\r\n";
	for(;;) {
		web_server(1);
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора

	}
	vTaskDelete( NULL);
}
// Третий поток
void vWeb2(void *)
{ //const char *pcTaskName = "Web server is running\r\n";
	for(;;) {
		web_server(2);
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора

	}
	vTaskDelete( NULL);
}
// Четвертый поток
void vWeb3(void *)
{ //const char *pcTaskName = "Web server is running\r\n";
	for(;;) {
		web_server(3);
		vTaskDelay(TIME_WEB_SERVER / portTICK_PERIOD_MS); // задержка чтения уменьшаем загрузку процессора
	}
	vTaskDelete( NULL);
}

//////////////////////////////////////////////////////////////////////////
// Задача чтения датчиков
void vReadSensor(void *)
{ //const char *pcTaskName = "ReadSensor\r\n";
	static unsigned long readFC = 0;
#ifdef USE_ELECTROMETER_SDM
#if (SDM_READ_PERIOD > 0)
	static unsigned long readSDM = 0;
#endif
#endif
	static uint32_t ttime;
	static uint8_t  prtemp;
	
	for(;;) {
		int8_t i;

		ttime = GetTickCount();
#ifdef RADIO_SENSORS		
		radio_timecnt++;
#endif
		prtemp = 0;	// Очистка ошибок по шинам
		if(OW_scan_flags == 0) {
#ifndef DEMO  // Если не демо
			prtemp |= HP.Prepare_Temp(0);
#ifdef ONEWIRE_DS2482_SECOND
			prtemp |= HP.Prepare_Temp(1);
#endif
#ifdef ONEWIRE_DS2482_THIRD
			prtemp |= HP.Prepare_Temp(2);
#endif
#ifdef ONEWIRE_DS2482_FOURTH
			prtemp |= HP.Prepare_Temp(3);
#endif
#endif     // не DEMO
		}
#ifndef IWR
		for(i = 0; i < ANUMBER; i++) HP.sADC[i].Read();                  // Прочитать данные с датчиков давления
#else
		for(i = 0; i < ANUMBER - 1; i++) HP.sADC[i].Read();              // Прочитать данные с датчиков давления, кроме последнего
#endif
#ifdef SGENERATOR
		for(i = 0; i < SGENERATOR; i++) HP.sInput[i].Read();                // Прочитать данные сухой контакт
#else
		for(i = 0; i < INUMBER; i++) HP.sInput[i].Read();                // Прочитать данные сухой контакт
#endif
		//_delay(1);
#ifdef USE_ELECTROMETER_SDM   // Опрос состояния счетчика
#if (SDM_READ_PERIOD > 0)
			if((HP.dSDM.get_present()) && (GetTickCount() - readSDM > SDM_READ_PERIOD)) {
				readSDM=GetTickCount();
				HP.dSDM.get_readState(2);     // Последняя группа регистров ТОК
			}
#endif
#endif
#if defined(WR_PowerMeter_Modbus)
		if(GETBIT(WR.Flags, WR_fActive) || GETBIT(WR_WorkFlags, WR_fWF_Read_MPPT)) {
			vReadSensor_delay1ms(WEB0_FREQUENT_JOB_PERIOD / 2 - (GetTickCount() - ttime));	// через (WEB0_FREQUENT_JOB_PERIOD / 2) после начала очередного цикла чтения
			if(GETBIT(WR.Flags, WR_fLogFull) && GETBIT(HP.get_NetworkFlags(), fWebFullLog)) journal.jprintf("WR+: %d\n", GetTickCount() - ttime);
		 	WR_ReadPowerMeter();
		}
#else
#if defined(PWM_CALC_POWER_ARRAY) && defined(WR_CurrentSensor_4_20mA)
		WR_Calc_Power_Array_NewMeter(0);
#endif
#endif // defined(WR_PowerMeter_Modbus)

		vReadSensor_delay1ms(cDELAY_DS1820 - (int32_t)(GetTickCount() - ttime)); 	// Ожидать время преобразования

		for(i = 0; i < FNUMBER; i++) HP.sFrequency[i].Read();			// Получить значения датчиков потока

		if(OW_scan_flags == 0) {
			uint8_t flags = 0;
			for(i = 0; i < TNUMBER; i++) {                                   // Прочитать данные с температурных датчиков
				if((prtemp & (1<<HP.sTemp[i].get_bus())) == 0) {
					if(HP.sTemp[i].Read() == OK) flags |= HP.sTemp[i].get_setup_flags();
				}
				_delay(1);     												// пауза
			}
			// Проверка на предельные температуры
			for(i = 0; i < TempAlarm_size; i++) {
				uint8_t num = TempAlarm[i].num;
				if(HP.sTemp[num].get_setup_flags() & ((1<<fTEMP_HeatTarget)|(1<<fTEMP_HeatFloor))) continue;
				int16_t T = HP.sTemp[num].get_Temp();
				if(T < TempAlarm[i].MinTemp * 100) {
					set_Error(HP.sTemp[num].set_Err(ERR_MINTEMP), HP.sTemp[num].get_name());
				}
				if(T > TempAlarm[i].MaxTemp * 100) {
					set_Error(HP.sTemp[num].set_Err(ERR_MAXTEMP), HP.sTemp[num].get_name());
				}
			}

			int32_t temp;
			if(GETBIT(flags, fTEMP_as_TIN_average)) { // Расчет средних датчиков для TIN
				temp = 0;
				uint8_t cnt = 0;
				for(i = 0; i < TNUMBER; i++) {
					if(GETBIT(HP.Prof.SaveON.bTIN, i) && HP.sTemp[i].get_setup_flag(fTEMP_as_TIN_average) && HP.sTemp[i].get_Temp() != STARTTEMP) {
						temp += HP.sTemp[i].get_Temp();
						cnt++;
					}
				}
				if(cnt) temp /= cnt; else temp = STARTTEMP;
			} else temp = STARTTEMP;
			int16_t temp2 = temp;
			if(GETBIT(flags, fTEMP_as_TIN_min)) { // Выбор минимальной температуры для TIN
				if(HP.get_modeHouse() == pCOOL) {
					if(temp2 == STARTTEMP) temp2 = -STARTTEMP;
					for(i = 0; i < TNUMBER; i++) {
						if(GETBIT(HP.Prof.SaveON.bTIN, i) && HP.sTemp[i].get_setup_flag(fTEMP_as_TIN_min) && temp2 < HP.sTemp[i].get_Temp()) temp2 = HP.sTemp[i].get_Temp();
					}
					if(temp2 != -STARTTEMP) HP.sTemp[TIN].set_Temp(temp2);
				} else {
					for(i = 0; i < TNUMBER; i++) {
						if(GETBIT(HP.Prof.SaveON.bTIN, i) && HP.sTemp[i].get_setup_flag(fTEMP_as_TIN_min) && temp2 > HP.sTemp[i].get_Temp()) temp2 = HP.sTemp[i].get_Temp();
					}
					if(temp2 != STARTTEMP) HP.sTemp[TIN].set_Temp(temp2);
				}
			} else if(temp2 != STARTTEMP) HP.sTemp[TIN].set_Temp(temp2);
		}

#ifdef USE_ELECTROMETER_SDM   // Опрос состояния счетчика
		HP.dSDM.get_readState(0); // Основная группа регистров
#endif
		HP.calculatePower();  // Расчет мощностей и СОР
		Stats.Update();

#if defined(WR_PowerMeter_Modbus) && TIME_READ_SENSOR >= WEB0_FREQUENT_JOB_PERIOD * 2
		if((WR.Flags & ((1<<WR_fActive) | (1<<WR_fPeriod_1sec))) == ((1<<WR_fActive) | (1<<WR_fPeriod_1sec))) {
			int32_t tm = GetTickCount() - ttime;
			if((int32_t)TIME_READ_SENSOR - tm > Modbus.RS485.ModbusResponseTimeout + Modbus.RS485.ModbusMinTimeBetweenTransaction + Modbus.RS485.ModbusMinTimeBetweenTransaction / 2) {
				vReadSensor_delay1ms(WEB0_FREQUENT_JOB_PERIOD + WEB0_FREQUENT_JOB_PERIOD / 2 - tm);	// через (WEB0_FREQUENT_JOB_PERIOD * 1.5) после начала очередного цикла чтения
				if(GETBIT(WR.Flags, WR_fLogFull) && GETBIT(HP.get_NetworkFlags(), fWebFullLog)) journal.jprintf("WR2+: %d(%d)\n", GetTickCount() - ttime, tm);
			 	WR_ReadPowerMeter();
			}
		} else //WR_PowerMeter_New = true;
#endif
			vReadSensor_delay1ms((int32_t(TIME_READ_SENSOR) - int32_t(GetTickCount() - ttime)) / 2);     // 1. Ожидать время нужное для цикла чтения

		// Вычисление перегрева используются РАЗНЫЕ датчики при нагреве и охлаждении
		// Режим работы определяется по состоянию четырехходового клапана при его отсутвии только нагрев
#ifdef EEV_DEF
		HP.dEEV.set_Overheat(HP.is_heating()); // нагрев(1) или охлаждение(0)
#endif

		//  Опрос состояния инвертора
#ifdef USE_UPS
		if(HP.NO_Power) {
			if(!HP.sInput[SPOWER].is_alarm()) { // Включаемся
				if(HP.NO_Power_delay) {
					if(--HP.NO_Power_delay == 0) HP.sendCommand(pNETWORK);
				} else {
					journal.jprintf_date( "POWER RESTORED!\n");
					if(!HP.Schdlr.IsShedulerOn()) {  // Расписание не активно, иначе включаемся через расписание
						if(HP.NO_Power == 2 && HP.get_State() == pWAIT_HP) {
							HP.NO_Power = 0;
							journal.jprintf("Resuming work...\n");
							HP.sendCommand(pRESUME);
						}
					}
					HP.NO_Power = 0;
				}
			}
		} else
#endif
			if(HP.dFC.get_present() && (GetTickCount() - readFC > FC_TIME_READ) && GetTickCount() - ttime < TIME_READ_SENSOR - (Modbus.RS485.ModbusResponseTimeout + Modbus.RS485.ModbusMinTimeBetweenTransaction)) {
				readFC = GetTickCount();
				HP.dFC.get_readState();
			}

#ifdef DRV_EEV_L9333  // Опрос состяния драйвера ЭРВ
		if (digitalReadDirect(PIN_STEP_DIAG)) // Перечитываем два раза
		{
			vReadSensor_delay1ms(5);
			if (digitalReadDirect(PIN_STEP_DIAG)) set_Error(ERR_DRV_EEV,(char*)__FUNCTION__); // Контроль за работой драйвера ЭРВ
		}
#endif

#ifdef FLOW_CONTROL                // если надо проверяем потоки (защита от отказа насосов) ERR_MIN_FLOW
	if(HP.is_compressor_on())      // Только если компрессор включен 
		for(uint8_t i = 0; i < FNUMBER; i++){   // Проверка потока по каждому датчику
		#ifdef SUPERBOILER   // Если определен супер бойлер
			#ifdef FLOWCON   // если определен датчик потока конденсатора
			   if ((i==FLOWCON)&&(!HP.dRelay[RPUMPO].get_Relay())) continue; // Для режима супербойлер есть вариант когда не будет протока по контуру отопления
			#endif
		#endif
			if(HP.sFrequency[i].get_checkFlow() && HP.sFrequency[i].get_Value() < HP.sFrequency[i].get_minValue()) {     // Поток меньше минимального ошибка осанавливаем ТН
				journal.jprintf("%s low flow: %.3d\n",(char*) HP.sFrequency[i].get_name(), HP.sFrequency[i].get_Value());
				set_Error(ERR_MIN_FLOW, (char*) HP.sFrequency[i].get_name());
			}
		}
#endif
#ifdef SEVA  //Если определен лепестковый датчик протока - это переливная схема ТН - надо контролировать проток при работе
 	if(HP.dRelay[RPUMPI].get_Relay())                                                                                             // Только если включен насос геоконтура  (PUMP_IN)
		if (HP.sInput[SEVA].get_Input()==SEVA_OFF) {set_Error(ERR_SEVA_FLOW,(char*)"SEVA"); return;}                              // Выход по ошибке отсутствия протока
#endif

		//
		vReadSensor_delay1ms(TIME_READ_SENSOR - int32_t(GetTickCount() - ttime));     // Ожидать время нужное для цикла чтения

	}  // for
	vTaskDelete( NULL);
}

// Вызывается во время задержек в задаче чтения датчиков
void vReadSensor_delay1ms(int32_t ms)
{
	if(ms <= 0 || ms >= (int32_t)TIME_READ_SENSOR) return;
	if(ms <= 2) {
		vTaskDelay(ms);
		return;
	}
	uint32_t tm = GetTickCount();
	do {
#ifdef  KEY_ON_OFF // Если надо проверяем кнопку включения ТН
		static uint8_t key_debounce = 0;                              // кнопка вкл/вкл дребезг подавление
		if(!digitalReadDirect(PIN_KEY1)) {
			if(!key_debounce) {
				journal.jprintf("ON/OFF Key pressed!\n");
				if(HP.get_errcode() && !Error_Beep_confirmed) {
					Error_Beep_confirmed = true;
				} else if(HP.get_State() == pOFF_HP) {
					HP.sendCommand(pSTART);
				} else if((HP.get_State() == pWORK_HP) || (HP.get_State() == pWAIT_HP)) {
					HP.sendCommand(pSTOP);
				}
			}
			key_debounce = -1;
		} else if(key_debounce) key_debounce--;
#endif
#ifdef USE_UPS
		HP.sInput[SPOWER].Read(true);
		if(HP.sInput[SPOWER].is_alarm()) { // Электричество кончилось
			HP.HandleNoPower();
		}
#endif
#ifdef SGENERATOR
		HP.sInput[SGENERATOR].Read(true);			// Прочитать данные сухой контакт, быстро
		if(GETBIT(HP.Option.flags2, f2BackupPowerAuto)) {
			if(HP.sInput[SGENERATOR].get_Input() == HP.sInput[SGENERATOR].get_alarmInput()) { // на резерве
				HP.Option.flags |= (1<<fBackupPower);
				if(HP.fBackupPowerOffDelay) HP.fBackupPowerOffDelay = RETURN_FROM_GENERATOR_DELAY / 10;
			} else if(HP.fBackupPowerOffDelay) {
				if(--HP.fBackupPowerOffDelay == 0) {
					journal.jprintf_time("Switched to Normal power!\n");
					HP.Option.flags &= ~(1<<fBackupPower);
					if(GETBIT(HP.flags, fHP_BackupNoPwrWAIT)) {
						HP.flags &= ~(1<<fHP_BackupNoPwrWAIT);
						HP.sendCommand(pRESUME);
					}
				}
			} else HP.Option.flags &= ~(1<<fBackupPower);
		}
#endif
		if(GETBIT(HP.Option.flags, fBackupPower)) {
			if(!HP.fBackupPowerOffDelay) {			// Нужно уменьшить нагрузку
#ifdef RBOILER
				HP.dRelay[RBOILER].set_OFF();		// выключить тэн бойлера
#endif
#ifdef WATTROUTER
				for(uint8_t i = 0; i < WR_NumLoads; i++) {
					if(!GETBIT(WR_Loads, i) || WR_LoadRun[i] == 0) continue;
					if(GETBIT(WR.PWM_Loads, i)) {
						WR_Change_Load_PWM(i, -32768);
					} else {
						if(WR_Load_pins[i] >= 0) WR_Switch_Load(i, 0);
					}
				}
#endif
				if(HP.is_compressor_on() && HP.dFC.get_target() > HP.dFC.get_maxFreqGen()) {
					HP.dFC.set_target(HP.dFC.get_maxFreqGen(), true, HP.dFC.get_minFreq(), HP.dFC.get_maxFreq());
				}
				journal.jprintf_time("Switched to Backup power!\n");
				HP.fBackupPowerOffDelay = RETURN_FROM_GENERATOR_DELAY / 10;
			}
		}
#ifdef RADIO_SENSORS
		if(ms - (GetTickCount() - tm) >= 20) check_radio_sensors();
#endif
		int32_t tm2 = GetTickCount() - tm;
		if((tm2 -= ms) >= -10) {
			if(tm2 < 0) vTaskDelay(-tm2);
			break;
		}
		vTaskDelay(10);
	} while(true);
}

//////////////////////////////////////////////////////////////////////////
// Задача Управление тепловым насосом (HP.xHandleUpdate) "UpdateHP"
 void vUpdate( void * )
{ //const char *pcTaskName = "HP_Update\r\n";
	#ifdef RPUMPB
	static uint32_t RPUMPBTick=0;
	#endif
	static uint8_t  Scheduler_check_day = 0;
	for(;;)
	{
		if(!HP.Task_vUpdate_run) {
			vTaskSuspend(NULL);				// Stop vUpdate, HP.xHandleUpdate
			continue;
		}
		if (HP.get_State()==pWORK_HP){ //Код обслуживания работы ТН выполняется только если состяние ТН - работа а вот расписание всегда выполняется
			// 1. Обновится, В это время команды управления не выполняются!!!!!
			if (SemaphoreTake(HP.xCommandSemaphore,0)==pdPASS)                                           // Cемафор  захвачен
			{
				if (HP.get_State()==pWORK_HP)  HP.vUpdate();                                               // ТН работает и идет процесс контроля
				SemaphoreGive(HP.xCommandSemaphore);                                                       // Семафор отдан
			}
			// 2. Управление циркуляционным насосом ГВС
#ifdef RPUMPB
#ifdef SUPERBOILER
			if (HP.scheduleBoiler())                         // Для супербойлера игнорироуем для циркуляции флаг включения бойлера только расписание
#else
				if ((HP.scheduleBoiler())&&(HP.get_BoilerON()))  // если бойлер разрешен и разрешено греть бойлер согласно расписания или расписание выключено
#endif
				{
#ifndef SUPERBOILER                       // если не определен супер бойлер, то при нагреве ГВС циркуляция всегда рабоатет
					if ((HP.get_modWork() & pBOILER))           // Если включен нагрев ГВС всегда включать насос циркуляции ЕСЛИ НЕ СУПЕРБОЙЛЕР
					{ HP.dRelay[RPUMPB].set_ON(); }
					else
#endif  // #ifndef SUPERBOILER 
						if (HP.get_Circulation() && (!GETBIT(HP.Prof.Boiler.flags, fBoilerCircSchedule) || HP.scheduleBoiler()))   // Циркуляция разрешена
						{
#ifdef SUPERBOILER
							if((HP.dRelay[RCOMP].get_Relay()||HP.dFC.isfOnOff())&&(HP.get_onBoiler() || (HP.dRelay[RSUPERBOILER].get_Relay() && !HP.dRelay[PUMP_OUT].get_Relay()))) {
								// идет прямой нагрев ГВС через предконденсатор, насос циркуляции ВЫКЛЮЧАЕМ
								HP.dRelay[RPUMPB].set_OFF();
								goto delayTask;
							}
#else
							if((HP.dRelay[RCOMP].get_Relay()||HP.dFC.isfOnOff())&&(HP.get_onBoiler())) { // идет нагрев ГВС
                               HP.dRelay[RPUMPB].set_ON();
                               goto delayTask; // идет нагрев ГВС включаем насос циркуляции ВСЕГДА - улучшаем перемешивание
			                }
#endif
			                if (HP.get_CirculWork()==0) { HP.dRelay[RPUMPB].set_OFF(); goto delayTask;/* continue;*/}   // В условиях стоит время работы 0 - выключаем насос ГВС
							if (HP.get_CirculPause()==0) { HP.dRelay[RPUMPB].set_ON(); goto delayTask;/* continue;*/}  // В условиях стоит время паузы 0 - включаем насос ГВС
							if(HP.dRelay[RPUMPB].get_Relay())                                       // Насос включен Смотрим времена
							{
								if(xTaskGetTickCount()-RPUMPBTick > HP.get_CirculWork()*configTICK_RATE_HZ)   // ждем время мсек
								{
									RPUMPBTick=xTaskGetTickCount();
									HP.dRelay[RPUMPB].set_OFF();                                  // выключить насос
								}
							} else {                                                                // Насос выключен
								if(xTaskGetTickCount()-RPUMPBTick >  HP.get_CirculPause()*configTICK_RATE_HZ)   // ждем время мсек
								{
									RPUMPBTick=xTaskGetTickCount();
									HP.dRelay[RPUMPB].set_ON();                                    // включить насос
								}
							} // if(HP.dRealay[RPUMPB].get_Relay())
						}  //  if (HP.get_Circulation())
						else HP.dRelay[RPUMPB].set_OFF() ;                                      // if (HP.get_Circulation())        выключить насос если его управление запрещено
				} //  if (HP.scheduleBoiler())
				else  HP.dRelay[RPUMPB].set_OFF() ;                                       // По расписанию выключено или бойлер запрещен,  насос выключаем
#endif // #ifdef RPUMPB
		} // НЕ РЕЖИМ ОЖИДАНИЕ if HP.get_State()==pWORK_HP)
	
		if(!HP.Task_vUpdate_run) goto delayTask;/* continue;*/

		// 3. Расписание проверка всегда
		{  // error: jump to label [-fpermissive] GCC
			// Переключение расписания, когда текущий месяц и дясятидневка совпадают; если пропустили из-за выключенного НК или работы,
			// то пропустили. Расписание выбирается один раз, если вручную перевыбрать, то еще раз автоматически выбираться не будет до следующего года
			if(HP.Schdlr.IsShedulerOn() && !(HP.Schdlr.sch_data.AutoSelectMonthWeek[HP.Schdlr.sch_data.Active] & fSch_AS_DontSwitch)) {
				uint8_t d = rtcSAM3X8.get_days();
				if(Scheduler_check_day != d) {
					Scheduler_check_day = d;
					d /= 10;
					if(d > 2) d = 2;
					bool need_save = false;
					for(uint8_t i = 0; i < MAX_CALENDARS; i++) {
						if(HP.Schdlr.sch_data.AutoSelectMonthWeek[i]) {
							if((HP.Schdlr.sch_data.AutoSelectMonthWeek[i] & ~fSch_AS_Changed) == ((rtcSAM3X8.get_months() << 2) | d)) {
								if(HP.Schdlr.sch_data.Active != i && !(HP.Schdlr.sch_data.AutoSelectMonthWeek[i] & fSch_AS_Changed)) {
									journal.jprintf_time("Schedule %d selected\n", i + 1);
									HP.Schdlr.sch_data.Active = i;
									HP.Schdlr.sch_data.AutoSelectMonthWeek[i] |= fSch_AS_Changed;
									need_save = true;
								}
							} else if(HP.Schdlr.sch_data.AutoSelectMonthWeek[i] & fSch_AS_Changed) {
								HP.Schdlr.sch_data.AutoSelectMonthWeek[i] &= ~fSch_AS_Changed;
								need_save = true;
							}
						}
					}
					if(need_save) HP.Schdlr.save();
				}
			}
			int8_t _profile = HP.Schdlr.calc_active_profile(); // Какой профиль ДОЛЖЕН быть сейчас активен
			if(_profile != SCHDLR_NotActive) {                 // Расписание активно
				int8_t _curr_profile = HP.get_State() == pWORK_HP ? HP.Prof.get_idProfile() : SCHDLR_Profile_off;
				if(_profile != _curr_profile && HP.isCommand() == pEMPTY) { // новый режим и ни чего не выполняется?
					if(_profile == SCHDLR_Profile_off) {
						HP.sendCommand(pWAIT);
					} else if(HP.Prof.get_idProfile() != _profile) {
						int32_t _mode;
						if((_mode = HP.Prof.load_from_EEPROM_SaveON_mode(_profile)) >= 0) {
							MODE_HP currmode = HP.get_modWork();
							uint8_t frestart = _mode != pOFF && currmode != pOFF && (currmode & (pHEAT | pCOOL)) && (currmode & (pHEAT | pCOOL)) != (_mode & (pHEAT | pCOOL)); // Если направление работы ТН разное
							if(frestart) {
								HP.sendCommand(pWAIT);
								uint8_t i = DELAY_BEFORE_STOP_IN_PUMP + (HP.get_modeHouse() == pCOOL ? HP.Prof.Cool.delayOffPump : HP.Prof.Heat.delayOffPump) + 1;
								while(HP.isCommand()) {	_delay(1000); if(!--i) break; } // ждем отработки команды
								if(!HP.Task_vUpdate_run) continue;
							}
							//vTaskSuspendAll();	// без проверки
							HP.Prof.load(_profile);
							HP.set_profile();
							//xTaskResumeAll();
							journal.jprintf_time("Profile changed to #%d\n", _profile);
							if(frestart) HP.sendCommand(pRESUME);
						}
					} else if(HP.get_State() == pWAIT_HP && !HP.NO_Power && !GETBIT(HP.flags, fHP_BackupNoPwrWAIT)) {
						HP.sendCommand(pRESUME);
					}
				}
			}
		}
		// 4. Отработка пауз всегда они разные в зависимости от состояния ТН!!
delayTask:	// чтобы задача отдавала часть времени другим
		switch (HP.get_State())  // Состояние ТН
		{
		case pOFF_HP:                          // 0 ТН выключен
		case pSTOPING_HP:                      // 2 Останавливается
			journal.jprintf((const char*)" Stop task UpdateHP\n");
			HP.Task_vUpdate_run = false;
			break;
		case pSTARTING_HP: _delay(10000); break; // 1 Стартует  - этого не должно быть в этом месте
		case pWORK_HP:                           // 3 Работает   - анализ режима работы get_modWork()
			if((HP.get_modWork() & pHEAT)) {	// Отопление
				if(HP.get_ruleHeat()==pHYSTERESIS)
					vTaskDelay(TIME_CONTROL/portTICK_PERIOD_MS);    // Гистерезис
				else vTaskDelay(HP.dFC.get_Uptime() * 1000/portTICK_PERIOD_MS);                        // Время интегрирования ПИД  секунды
			} else if((HP.get_modWork() & pCOOL)) { // охлаждение
				if(HP.get_ruleCool()==pHYSTERESIS)
					vTaskDelay(TIME_CONTROL/portTICK_PERIOD_MS);    // Гистерезис
				else vTaskDelay(HP.dFC.get_Uptime() * 1000/portTICK_PERIOD_MS);                        // Время интегрирования ПИД секунды
			} else if((HP.get_modWork() & pBOILER)) { // бойлер
				if(GETBIT(HP.Prof.Boiler.flags, fBoilerPID))
					vTaskDelay(HP.dFC.get_Uptime() * 1000/portTICK_PERIOD_MS);  // Время интегрирования ПИД секунды
				else vTaskDelay(TIME_CONTROL_BOILER/portTICK_PERIOD_MS);                                        // Гистерезис
			} else { // Пауза
				vTaskDelay(TIME_CONTROL/portTICK_PERIOD_MS);                                        // Гистерезис
			}
			break;
		case pWAIT_HP:                          // 4 Ожидание ТН (расписание - пустое место)   проверям раз в 5 сек
			if(GETBIT(HP.Option.flags2, f2AutoStartGenerator) && GETBIT(HP.flags, fHP_BackupNoPwrWAIT)) {
				if(HP.get_modeHouse() == pHEAT) {
					if(GETBIT(HP.Prof.Heat.flags,fTarget) ? HP.RET : HP.sTemp[TIN].get_Temp() < HP.get_targetTempHeat() - HP.Prof.Heat.dTempGen) {
						HP.sendCommand(pRESUME);
					}
				} else if(HP.get_modeHouse() == pCOOL) {
					if(GETBIT(HP.Prof.Cool.flags,fTarget) ? HP.RET : HP.sTemp[TIN].get_Temp() > HP.get_targetTempCool() + HP.Prof.Cool.dTempGen) {
						HP.sendCommand(pRESUME);
					}
				}
			}
			_delay(UPDATE_HP_WAIT_PERIOD);
			break;
		case pERROR_HP: 						// 5 Ошибка ТН
			_delay(UPDATE_HP_WAIT_PERIOD);
			break;
		case pERROR_CODE:                       // 6 - Эта ошибка возникать не должна!
		default:
			journal.jprintf((const char*)" $ERROR: Bad state HP in function %s\n",(char*)__FUNCTION__);
		} //  switch (HP.get_State())

		if(!HP.Task_vUpdate_run) continue;
		// Солнечный коллектор
#ifdef USE_SUN_COLLECTOR
		if(HP.flags & (1<<fHP_SunSwitching)) {
			if(GetTickCount() - HP.time_Sun > SUN_VALVE_SWITCH_TIME) {
				HP.flags = (HP.flags & ~((1<<fHP_SunSwitching) | (1<<fHP_SunReady)));
				if(HP.dRelay[RSUNON].get_Relay()) {
					HP.flags |= (1<<fHP_SunReady);
					HP.time_Sun -= uint32_t(HP.Option.SunMinPause * 1000);
				}
				HP.dRelay[RSUNON].set_OFF();
				HP.dRelay[RSUNOFF].set_OFF();
			}
		} else {
			boolean fregen = GETBIT(HP.get_flags(), fSunRegenerateGeo) && HP.is_pause();
			int16_t tsun = HP.sTemp[TSUN].get_Temp();
			if(((!HP.is_pause()	&& (((HP.get_modWork() & pHEAT) && GETBIT(HP.Prof.Heat.flags, fUseSun)) || ((HP.get_modWork() & pCOOL) && GETBIT(HP.Prof.Cool.flags, fUseSun))
									|| (HP.get_onBoiler() && GETBIT(HP.Prof.Boiler.flags, fBoilerUseSun)))) || fregen)
				 && HP.get_State() != pERROR_HP && (HP.get_State() != pOFF_HP || HP.PauseStart != 0)) {
				if((HP.flags & (1<<fHP_SunWork))) {
					if(GetTickCount() - HP.time_Sun > uint32_t(HP.Option.SunMinWorktime * 1000)) {
						if(fregen) {
							if(tsun < HP.Option.SunRegGeoTemp || HP.sTemp[TSUNOUTG].get_Temp() < HP.Option.SunRegGeoTempGOff) HP.Sun_OFF();
						} else if(HP.sTemp[TSUNOUTG].get_Temp() < HP.sTemp[TEVAOUTG].get_Temp() + HP.Option.SunGTDelta) HP.Sun_OFF();
					}
				} else if(tsun >= (fregen ? HP.Option.SunRegGeoTemp : HP.sTemp[TEVAOUTG].get_Temp()) + HP.Option.SunTDelta) {
					HP.Sun_ON();
				}
			} else {
				HP.Sun_OFF();
				HP.time_Sun = GetTickCount() - uint32_t(HP.Option.SunMinPause * 1000);	// выключить задержку последующего включения
			}
			uint8_t fl = HP.flags & ((1<<fHP_SunNotInited) | (1<<fHP_SunReady) | (1<<fHP_SunSwitching) | (1<<fHP_SunWork));
			if((fl == (1<<fHP_SunReady) || fl == (1<<fHP_SunNotInited)) && tsun < HP.Option.SunTempOff) {
				HP.flags = (HP.flags & ~((1<<fHP_SunReady) | (1<<fHP_SunNotInited))) | (1<<fHP_SunSwitching);
				HP.dRelay[RSUNON].set_OFF();
				HP.dRelay[RSUNOFF].set_ON();
				HP.time_Sun = GetTickCount();
			}
		}
#endif
	}// for
	vTaskDelete( NULL );
}

// Задача Управление ЭРВ, "UpdateEEV"
#ifdef EEV_DEF
void vUpdateEEV(void *)
{ //const char *pcTaskName = "HP_UpdateEEV\r\n";
	for(;;) {
		while(!(HP.get_startCompressor() && (rtcSAM3X8.unixtime() - HP.get_startCompressor() > HP.dEEV.get_delayOnPid() && HP.dEEV.get_delayOnPid() != 255))) { // ЭРВ контролирует если прошла задержка после включения компрессора (пауза перед началом работы ПИД) и задержка != 255
			vTaskDelay(TIME_EEV_BEFORE_PID / portTICK_PERIOD_MS); // Период управления ЭРВ (цикл управления)
			if(HP.dEEV.get_flags() & (1<<fEEV_StartPosByTemp)) { // Скорректировать ЭРВ по температуре подачи
				if(!(HP.get_modWork() & pBOILER) || !GETBIT(HP.dEEV.get_flags(), fEEV_BoilerStartPos)) {
					HP.dEEV.set_EEV(HP.dEEV.get_StartPos());
				}
			}
		}
		HP.dEEV.resetPID();
xContinue:
		if(!HP.is_compressor_on()) {
			switch((uint8_t)HP.get_State()) {
			case pOFF_HP:
			case pSTOPING_HP:
			case pWAIT_HP:
				// Если компрессор не работает, то остановить задачу Обновления ЭРВ
				journal.jprintf((const char*) " Stop task UpdateEEV\n");
				vTaskSuspend(NULL);				// Stop vUpdateEEV
				continue; // продолжение задачи работы ЭРВ начитается с этого места, по этому сразу на начало цикла контроля
			}
		} else {
			HP.dEEV.CorrectOverheat();
			// Обновить и выполнить итерацию по контролю ЭРВ Для алгоритма таблица передаем СРЕДНИЕ (IN+OUT)/2 температуры
			HP.dEEV.Update();
			// штатная пауза (в зависимости от настроек)
			vTaskDelay(HP.dEEV.get_PID_time() * 1000 / portTICK_PERIOD_MS);  // ПИД
			goto xContinue;
		}
		vTaskDelay(TIME_EEV / portTICK_PERIOD_MS);
	} // for
	vTaskDelete( NULL);
}
#endif

#ifdef EEV_DEF
// Задача обеспечения движения шаговика EEV (StepperEEV)
void vUpdateStepperEEV(void *)
{ //const char *pcTaskName = "HP_UpdateStepperEEV\r\n";
  // Размер стека не позволяет использовать внутри jprintf.* !!!
	static int16_t cmd = 0;
	for(;;) {
		// Полный цикл движения шаговика с разгребанием очереди команд,
		// В очереди лежат АБСОЛЮТНЫЕ координаты
		// При этом если очередь содержит более одной команды - просто суммируем все команды и двигаемся по итоговой сумме
		// Это значит что шаговик не успевает за темпом выдачи команд программой. Экономим время

		// 1. Чтение очереди команд, для выяснения все таки куда надо двигаться, переходим на относительные координаты
		int16_t steps_left, *step_number = &HP.dEEV.EEV; // получить текущее положение шаговика абсолютное в начале очереди
		if(*step_number < 0) *step_number = 0;
		while(xQueueReceive(HP.dEEV.stepperEEV.xCommandQueue,&cmd,0) == pdPASS) ;   // Читаем очередь пока есть чего читать
		if(HP.dEEV.setZero) {
			*step_number = (HP.dEEV.stepperEEV.number_of_steps + EEV_SET_ZERO_OVERRIDE + 7) / 8 * 8; // Если выполняется команда установки 0 то все остальные команды игнорируются до ее выполнения.
			cmd = 0;
		}
		steps_left = cmd - *step_number;
		// 2. Движение
		while(steps_left != 0) {
			if(steps_left > 0) {// направление в увеличение
				(*step_number)++;
				steps_left--;
			} else { 			// направление в уменьшение
				(*step_number)--;
				steps_left++;
			}
			if(*step_number < 0) {
				HP.dEEV.set_error(ERR_MIN_EEV);
				break;
			}
#if EEV_PHASE == PHASE_4  // 4 фазы движения
			HP.dEEV.stepperEEV.stepOne(*step_number % 4);                  // Сделать один шаг //
#else                     // остальные варианты  8 фаз движения
			HP.dEEV.stepperEEV.stepOne(*step_number % 8);                   // Сделать один шаг //
#endif
			vTaskDelay(HP.dEEV.stepperEEV.step_delay / portTICK_PERIOD_MS);      // Ожидать step_delay для следующего шага.
		}
		if(HP.dEEV.setZero) { // если стоит признак установки нуля, обнулить и сбросить признак
			*step_number = 0;
			HP.dEEV.setZero = false;
		}

		// 3. Остановить выполнение команад, если очередь пуста, но могли накидать пока двигались
		if(xQueuePeek(HP.dEEV.stepperEEV.xCommandQueue,&cmd,0) == errQUEUE_EMPTY) {
			if(!HP.dEEV.get_HoldMotor()) HP.dEEV.stepperEEV.off();                                   // выключить двигатель если нет удержания
			HP.dEEV.stepperEEV.offBuzy();                                                            // признак Мотор остановлен
			vTaskSuspend(NULL);               // Приостановить задучу vUpdateStepperEEV
		}
		// Дошли до сюда новая, очередь не пуста и новая итерация по разбору очереди
	} // for
	vTaskDelete( NULL);
}
#endif

// vUpdateCommand /////////////////////////////////////////////////////// 
// Задача Разбор очереди команд
void vUpdateCommand(void *)
{ //const char *pcTaskName = "HP_UpdateCommand\r\n";
	for(;;) {
		HP.runCommand();       // Выполнение команд управления ТН
		vTaskSuspend(NULL);    // Команды выполнены, остановить задачу vUpdateCommand, пуск осуществляется при посылке команды
	}
	vTaskDelete( NULL);
}

// ServiceHP ///////////////////////////////////////////////
// Графики в ОЗУ, счетчики моточасов, сохранение статистики, работа насосов в простое, дисплей Nextion
void vServiceHP(void *)
{
#ifdef NEXTION
	static uint32_t NextionTick = 0;
#endif
	static uint8_t  task_updstat_countm = rtcSAM3X8.get_minutes();
	static uint8_t  task_dailyswitch_countm = task_updstat_countm;
	static TickType_t timer_sec = xTaskGetTickCount(), timer_idle = 0, timer_total = 0;
	static uint16_t restart_cnt;
	for(;;) {
		STORE_DEBUG_INFO(70);
		register uint32_t t = xTaskGetTickCount();
		if(t - timer_sec >= 1000) { // 1 sec
			WDT_Restart(WDT);
			extern TickType_t xTickCountZero;
			TickType_t ttmpt = t - xTickCountZero - timer_total;
			if(ttmpt > 5000) {
				if(t < timer_sec) {
					vTaskResetRunTimeCounters();
					timer_idle = timer_total = 0;
				} else {
					TickType_t ttmp = timer_idle;
					HP.CPU_LOAD = 100 - (((timer_idle = vTaskGetRunTimeCounter(xTaskGetIdleTaskHandle())) - ttmp) / (ttmpt / 100UL));
					timer_total = t - xTickCountZero;
				}
			}
			timer_sec = t;
			if(HP.IsWorkingNow()) {
#ifdef RHEAT
				if(HP.RHEAT_timer < USHRT_MAX) HP.RHEAT_timer++;
#endif
				if(++task_updstat_chars >= HP.get_tChart()) {
					task_updstat_chars = 0;
					if((Charts_when_comp_on && HP.is_compressor_on()) || (!Charts_when_comp_on && HP.get_State() != pOFF_HP)) { // пришло время
						STORE_DEBUG_INFO(71);
						HP.updateChart();                                       // Обновить графики
						STORE_DEBUG_INFO(72);
					} else {
#ifdef WATTROUTER
#ifdef WR_PowerMeter_Modbus
						if(ChartsModSetup[0].object == STATS_OBJ_WattRouter_In) HP.Charts[0].add_Point(WR_PowerMeter_Power);
#else
						if(ChartsModSetup[0].object == STATS_OBJ_WattRouter_In) HP.Charts[0].add_Point(WR_Pnet);
#endif
#endif
					}
				}
				uint8_t m = rtcSAM3X8.get_minutes();
				if(m != task_updstat_countm) { 								// Через 1 минуту
					task_updstat_countm = m;
					STORE_DEBUG_INFO(73);
					uint8_t h = rtcSAM3X8.get_hours();
					TarifNightNow = h >= TARIF_NIGHT_START || h < TARIF_NIGHT_END;
					HP.updateCount();                                       // Обновить счетчики моточасов
					STORE_DEBUG_INFO(74);
					if(task_updstat_countm == 59) HP.save_motoHour();		// сохранить раз в час
					Stats.History();                                        // запись истории в файл
					taskYIELD();
#ifdef USE_ELECTROMETER_SDM
					if(GETBIT(HP.Option.flags2, f2LogEnergy)) {
						if(m == 0 && (h == 0 || h == TARIF_NIGHT_START || h == TARIF_NIGHT_END)) {
							static float tmp;
							if(Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_AC_ENERGY, &tmp) == OK
									|| Modbus.readInputRegistersFloat(SDM_MODBUS_ADR, SDM_AC_ENERGY, &tmp) == OK)
								journal.jprintf_time("ENERGY: %.3f\n", tmp);
						}
					}
#endif
#ifdef RADIO_SENSORS
					// Проверка потерянных радиодатчиков
					for(uint8_t i = 0; i < RADIO_SENSORS_MAX; i++) {
						if(radio_received[i].RSSI != 255) continue;
						for(uint8_t j = 0; j < TNUMBER; j++) {
							if(HP.sTemp[j].get_flag(fErrorWasSend)) continue;
							if((HP.sTemp[j].get_setup_flags() & ((1<<fTEMP_HeatTarget)|(1<<fTEMP_HeatFloor)|(1<<fTEMP_as_TIN_min)|(1<<fTEMP_as_TIN_average))) || j == TOUT || j == TIN) {
								uint8_t *addr = HP.sTemp[j].get_address();
								if(*addr == tRadio && memcmp(&radio_received[i].serial_num, addr + 1, sizeof(radio_received[0].serial_num)) == 0) {
									if(HP.message.setMessage(pMESSAGE_WARNING, (char*) "Нет связи с радиодатчиком ", 0)) {
										HP.message.setMessage_add_text(HP.sTemp[j].get_name());
										HP.sTemp[j].set_flag(fErrorWasSend, 1);
									}
									break;
								}
							}
						}
					}
#endif

				} else if(m != task_dailyswitch_countm) {
					task_dailyswitch_countm = m;
					typeof(DailySwitch_on) _dson = 0;
					uint32_t hhmm = rtcSAM3X8.get_hours() * 100 + m;
					for(uint8_t i = 0; i < DAILY_SWITCH_MAX; i++) {
						if(HP.Prof.DailySwitch[i].Device == 0) break;
						if(HP.Prof.DailySwitch[i].Device >= RNUMBER) continue;
						int8_t ds = HP.NO_Power || GETBIT(HP.Option.flags, fBackupPower) ? 0 : HP.Prof.check_DailySwitch(i, hhmm);
						_dson |= (ds < 0 ? GETBIT(DailySwitch_on, i) : ds)<<i;
					}
					for(uint8_t i = 0; i < DAILY_SWITCH_MAX; i++) {
						if(HP.Prof.DailySwitch[i].Device == 0) break;
						if(HP.Prof.DailySwitch[i].Device >= RNUMBER) continue;
						if(GETBIT((_dson ^ DailySwitch_on), i)) { //GETBIT(_dson, i) != HP.dRelay[HP.Prof.DailySwitch[i].Device].get_Relay()) {
							if(!GETBIT(_dson, i)) { // off
								uint8_t j = i + 1;
								for(; j < DAILY_SWITCH_MAX; j++) {
									if(HP.Prof.DailySwitch[i].Device == HP.Prof.DailySwitch[j].Device && GETBIT(_dson, j)) break;
								}
								if(j < DAILY_SWITCH_MAX) { // Еще есть тоже реле или реле уже включено
									SETBIT0(DailySwitch_on, i);
									continue;
								}
							}
							HP.dRelay[HP.Prof.DailySwitch[i].Device].set_Relay(GETBIT(_dson, i) ? fR_StatusDaily : -fR_StatusDaily);
							DailySwitch_on = (DailySwitch_on & ~(1<<i)) | (_dson & (1<<i));
						}
					}
				}
				STORE_DEBUG_INFO(75);
			}
			if(HP.PauseStart) {
				if(HP.PauseStart == 1) {
					restart_cnt = HP.isCommand() == pRESTART ? HP.Option.delayStartRes : HP.Option.delayRepeadStart;  // Определение времени задержки
					HP.PauseStart = 3;
					journal.jprintf("Start over %d sec . . .\n", restart_cnt);
				} else if(HP.PauseStart == 2) { // Быстрый рестарт
					restart_cnt = DELAY_REPEAT_FAST;
					HP.PauseStart = 3;
					journal.jprintf("Start over %d sec . . .\n", restart_cnt);
				} else if(restart_cnt-- == 0) {
					HP.PauseStart = 0;
					HP.sendCommand(pAUTOSTART);
				}
			}
			if(HP.startPump) {  // Если разрешена работа насоса (0 - останов задачи, > 1 - работа)
				if(HP.startPump == 4) { // Отработка после останова компрессора
					if(HP.pump_in_pause_timer == 0) {
						HP.PUMPS_OFF;
						if(HP.get_State() == pOFF_HP) {
							HP.startPump = 0;							// Задача насосов в паузе выключена
						} else if(HP.get_workPump()) {
							HP.pump_in_pause_timer = HP.get_pausePump();
							HP.startPump = 1;							// Поставить признак запуска задачи насос
						}
					} else HP.pump_in_pause_timer--;
				} else if(HP.get_workPump()) {
					if(HP.pump_in_pause_timer <= 1) {
						if(HP.startPump <= 2) { 						// включить
							HP.pump_in_pause_set(true);
							HP.pump_in_pause_timer = HP.get_workPump();
							HP.startPump = 3;
						} else if(HP.get_pausePump()) { 				// выключить
							HP.pump_in_pause_set(false);
							HP.pump_in_pause_timer = HP.get_pausePump();
							HP.startPump = 2;
						}
					} else HP.pump_in_pause_timer--;
				}
			}
			STORE_DEBUG_INFO(76);
			Stats.CheckCreateNewFile();
			taskYIELD();

			//  Синхронизация часов с I2C часами если стоит соответсвующий флаг
			static uint32_t oldTime = GetTickCount();
			if(HP.get_updateI2C())  // если надо обновить часы из I2c
			{
				if(GetTickCount() - oldTime > (uint32_t)TIME_I2C_UPDATE) // время пришло обновляться надо Период синхронизации внутренних часов с I2C часами (сек)
				{
					oldTime = rtcSAM3X8.unixtime();
					uint32_t t = TimeToUnixTime(getTime_RtcI2C());       // Прочитать время из часов i2c тут проблема
					if(t) {
						rtcSAM3X8.set_clock(t);                		 // Установить внутренние часы по i2c
						HP.updateDateTime(t > oldTime ? t - oldTime : -(oldTime - t));  // Обновить переменные времени с новым значением часов
						journal.jprintf((const char*) "Sync from I2C RTC: %s %s\n", NowDateToStr(), NowTimeToStr());
					} else {
						journal.jprintf("Error read I2C RTC\n");
					}
					oldTime = GetTickCount();
				}
			}
			// Проверки граничных температур для уведомлений, если разрешено!
			static uint16_t countTEMP = 0;        // Для проверки критических температур для рассылки уведомлений
			if(HP.message.get_fMessageTemp()) {
				if(++countTEMP > TIME_MESSAGE_TEMP) {
					countTEMP = 0;
					if(HP.sTemp[TIN].get_Temp() != STARTTEMP && HP.message.get_mTIN() > HP.sTemp[TIN].get_Temp()) HP.message.setMessage(pMESSAGE_TEMP, (char*) "Критическая температура в доме,", HP.sTemp[TIN].get_Temp());
					else if(HP.sTemp[TBOILER].get_Temp() != STARTTEMP && HP.message.get_mTBOILER() > HP.sTemp[TBOILER].get_Temp()) HP.message.setMessage(pMESSAGE_TEMP, (char*) "Критическая температура ГВС,", HP.sTemp[TBOILER].get_Temp());
					else if(HP.sTemp[TCOMP].get_Temp() != STARTTEMP && HP.message.get_mTCOMP() < HP.sTemp[TCOMP].get_Temp()) HP.message.setMessage(pMESSAGE_TEMP, (char*) "Критическая температура компрессора,", HP.sTemp[TCOMP].get_Temp());
				}
			}
			static uint8_t last_life_h = 255;
			uint8_t hour = rtcSAM3X8.get_hours();
			if(hour == HOUR_SIGNAL_LIFE && hour != last_life_h) {
				if(HP.message.get_fMessageLife()) { // Подача сигнала жизни если разрешено!
					if(HP.message.setMessage(pMESSAGE_LIFE, (char*) "Контроллер работает . . .", 0)) last_life_h = hour;
				} else last_life_h = hour;
				for(uint8_t j = 0; j < TNUMBER; j++) { // Очистка флага отправки сообщений
					HP.sTemp[j].set_flag(fErrorWasSend, 0);
				}
			} else last_life_h = hour;
		}
		STORE_DEBUG_INFO(77);
#ifdef NEXTION
		myNextion.readCommand();                 // прочитать сообщения от дисплея
		STORE_DEBUG_INFO(78);
		if(xTaskGetTickCount() - NextionTick > NEXTION_UPDATE) {
			STORE_DEBUG_INFO(79);
			myNextion.Update();                  // Обновление дисплея
			STORE_DEBUG_INFO(80);
			NextionTick = xTaskGetTickCount();
		}
#endif
		t = GetTickCount() - timer_sec;
		vTaskDelay(t < NEXTION_READ ? t : NEXTION_READ); // задержка чтения уменьшаем загрузку процессора
	}
	vTaskDelete(NULL);
}
