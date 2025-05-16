/*
 * Copyright (c) 2016-2021 by Vadim Kulakov vad7@yahoo.com, vad711
 * Частотный преобразователь Vacon 10
 * Автор vad711, vad7@yahoo.com
 *
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
//
#ifndef _VaconFC_h
#define _VaconFC_h

#include "Constant.h"

#define RECOVER_OIL_PERIOD_MUL		2	// Множитель периода
#define FC_COOLER_FUN_HYSTERESIS	3	// Гестерезис температуры для управления вентилятором инвертора, градусы

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifdef FC_VLT
	#define FC_VACON_NAME "FC-" TOSTRING(FC_VLT)
	#define FC_VACON

// Регистры Danfoss VLT (Basic Drive FC-101)
// Нумерация - номер параметра "n-nn" = nnn * 10 (нормер ячейки Modbus)
// Параметр 8-3 = 2 (Modbus RTU)
// Чтение
#define FC_TEMP			16340		// Температуры радиатора частотника, C
#define FC_POWER_HOURS	15000		// Наработка, часов, int32
#define FC_RUN_HOURS	15010		// Счетчик работы двигателя, часов, int32
#define FC_CNT_POWER	15020		// Счетчик кВтч, uint16
// 15030 - Кол-во включений питания, int32
// 15040 - Кол-во перегревов
// 15050 - Кол-во перенапряжений
// 15060 - 1 - Сброс счетчика кВтч
// 15070 - 1 - Сброс счетчика наработки
// 15300..15309 - Журнал ошибок

// Системные:
// Чтение
#define FC_STATUS		2910		// Слово состояния STW (16030)
#define FC_SPEED		16020		// Фактическая скорость REF, -16384..+16384 (-100%..+100%)
#define FC_FREQ			16130		// Выходная частота, +/- 0.1 Гц
#define FC_FREQ_MUL		10			// приведение к двум знакам после запятой
#define FC_RPM			16170		// Скорость двигателя, +/- об/мин
#define FC_CURRENT		16140		// Ток двигателя, 0.01 A
#define FC_TORQUE		16220		// Крутящий момент двигателя от номинального, +/- 1 %
#define FC_POWER		16100		// Мощность двигателя от номинального, кВт
//#define FC_POWER_IN_PERCENT			// Мощность (FC_POWER) в десятых %
#define FC_VOLTAGE		16120		// Напряжение двигателя, В
#define FC_VOLTAGE_DC	16300		// Напряжение шины постоянного тока, 1 В
#define FC_ERROR		16900		// Коды активного отказа, ALARM
#define FC_ERROR2		16910		// Коды активного отказа, ALARM 2 (Не доступно для FC-51)
#define FC_WARNING		16920		// Коды предупреждения, WARNING
#define FC_WARNING2		16930		// Коды предупреждения, WARNING 2 (Не доступно для FC-51)
#define FC_EX_STATUS	16940		// Доп. состояние
#define FC_EX_STATUS2	16950		// Доп. состояние 2 (Не доступно для FC-51)

// Запись
#define FC_CONTROL		2810		// Слово управления CTW
#define FC_SET_SPEED	2811		// Задание скорости MAV, -16384..+16384 (-100%..+100%)

// Биты
// FC_STATUS (STW)
#define FC_S_CONTROL_RDY (1<<0)	// Управление готово
const char *FC_S_CONTROL_RDY_str0	= {"No_Ctrl,"};
#define FC_S_RDY		(1<<1)	// Привод готов
const char *FC_S_RDY_str			= {"Ready,"};
#define FC_S_RUN		(1<<11)	// Привод работает
const char *FC_S_RUN_str			= {"Run,"};
#define FC_S_DIR		0		// Не используется (задание направления вращение через знак MRV (Main Reference Value)
#define FC_S_FLT		(1<<4)	// Действующий отказ
const char *FC_S_FLT_str			= {"Fault,"};
#define FC_S_W			(1<<7)	// Сигнал тревоги
const char *FC_S_W_str				= {"Warn,"};
#define FC_S_AREF		(1<<8)	// 0 - Линейное изменение скорости, 1 - Задание скорости достигнуто
const char *FC_S_AREF_str0			= {"Ramping,"};
#define FC_S_START		(1<<2)	// 1 - преобразователь частоты запускает двигатель командой пуска
const char *FC_S_START_str			= {"Start,"};
#define FC_S_TRIP		(1<<3)	// 1 - преобразователь частоты отключается
const char *FC_S_TRIP_str			= {"Trip,"};
#define FC_S_TRIPLOCK	(1<<3)	// 1 - преобразователь частоты отключен и блокирован
const char *FC_S_TRIPLOCK_str		= {"Lock,"};
#define FC_S_BUS		(1<<9)	// 0 - Управлять преобразователем частоты через канал последовательной связи нельзя
const char *FC_S_BUS_str0			= {"NO_BUS,"};
#define FC_S_FREQ_LIM	(1<<10)	// 1 - выходная частота достигла предела (параметры 4-12, 4-14)
const char *FC_S_FREQ_LIM_str		= {"Freq_Limit,"};
#define FC_S_OVERTEMP	(1<<12)	// 1 - преобразователь частоты остановлен из-за перегрева
const char *FC_S_OVERTEMP_str		= {"Over_Temp,"};
#define FC_S_VOLTAGE	(1<<13)	// 1 - Напряжение постоянного тока в цепи постоянного тока преобразователя частоты слишком мало или велико
const char *FC_S_VOLTAGE_str		= {"Err_V,"};
#define FC_S_CURRENT	(1<<14)	// 1 - превышен предел по току (параметр 4-18 Current Limit)
const char *FC_S_CURRENT_str		= {"Over_I,"};
#define FC_S_HEAT		(1<<15)	// 1 - таймеры для тепловой защиты двигателя и тепловой защиты преобразователя частоты превысели 100%
const char *FC_S_HEAT_str			= {"HEAT,"};

// FC_CONTROL (CTW)
#define FC_C_RUN		((1<<6)|(1<<10))	// 0 - Останов, 1 - Пуск
#define FC_C_STOP		((0<<6)|(1<<10))
#define FC_C_DIR		0					// Не используется (направления вращение через знак MAV (Main Actual Value)
#define FC_C_RST		((1<<7)|(1<<10))	// Сброс отказа
#define FC_C_COOLER_FAN ((1<<11)|(1<<10))	// Вкл. вентилятора (Реле 1, параметр 5-40[0] = 36)
//#define FC_C_COOLER_FAN ((1<<12)|(1<<10))	// Вкл. вентилятора (Реле 2, параметр 5-40[1] = 36)
#define FC_C_COOLER_FAN_STR "Relay 1"

#define FC_NonCriticalFaults  0x4D10 // Не критичные ошибки FC_ERROR (ALARM), которые можно сбросить, битовое поле

const char *FC_Alarm_str[] = {	"", // битовое поле, если меньше 32, то в конце NULL
								"Pwr.Card Temp",
								"Earth fault",
								"",
								"Ctrl.Word Timeout",
								"Over Current",
								"",
								"Motor Th. Over",
								"Motor ETR Over",
								"Inverter Overld.",
								"DCV under",	//10
								"DCV over",
								"Short Circuit",
								"",
								"Mains ph. loss",
								"AMA Not OK",
								"Live Zero Error",
								"Internal Fault",
								"",
								"U phase Loss",
								"V phase Loss",		//20
								"W phase Loss",
								"",
								"Ctrl. V Fault",
								"",
								"VDD1 low",
								"",
								"",
								"Earth fault",
								"Drive Init",//29
								NULL
							};
const char *FC_Warning_str[] = {"", // битовое поле, если меньше 32, то в конце NULL
								"Pwr.Card Temp",
								"Earth fault",
								"",
								"Ctrl.Word Timeout",
								"Over Current",
								"",
								"Motor Th. Over",
								"Motor ETR Over",
								"Inverter Overld.",
								"DCV under",	//10
								"DCV over",
								"",
								"",
								"Mains ph. loss",
								"No Motor",
								"Live Zero Error",
								"",
								"",
								"",
								"",		//20
								"",
								"",
								"24V low",
								"",
								"Current Limit",
								"Low temp",	// 26
								NULL
							};
const char *FC_ExtStatus_str[] = {// битовое поле, если меньше 32, то в конце NULL
								"Ramping",
								"AMA running",
								"Start CCW",
								"",
								"",
								"Feedback high",
								"Feedback low",
								"Out I high",
								"Out I low",
								"Out Freq high",
								"Out Freq low",	//10
								"",
								"",
								"Braking",
								"",
								"OVC active",
								"AC brake",
								"",
								"",
								"Ref. high",
								"Ref. low",		//20
								NULL
							};
const char *FC_ExtStatus2_str[] = {// битовое поле, если меньше 32, то в конце NULL
								"Off",
								"Auto",
								"",
								"",
								"",
								"",
								"",
								"Ctrl.Ready",
								"Ready",
								"Quick Stop",
								"DC Brake",	//10
								"Stop",
								"",
								"Freeze Out Req",
								"Freeze Out",
								"Jog Req",
								"Jog",
								"Start Req",
								"Start",
								"",
								"Start Delay",		//20
								"Sleep",
								"Sleep boost",
								"Running",
								"Bypass",
								"Fire Mode",
								"External Interlock",
								"Fire mode limit exceed",
								"FlyStart Active",
								NULL
							};


#else //FC_VLT

#define FC_VACON_20
#define FC_VACON_NAME "Vacon"
// Регистры Vacon 10/20
// Чтение
#define FC_FREQ_OUT		1			// Выходная частота, поступающая на двигатель
#define FC_FREQ_REF		2			// Опорная частота для управления двигателем
#define FC_TEMP			8			// Температуры радиатора частотника, C
#define FC_TEMP_MOTOR	9			// Расчетная температура двигателя, %
#define FC_COMM_STATUS	808			// Состояние связи по шине Modbus. Формат: xx.yyy где xx = 0 – 64 (число сообщений об ошибках), yyy = 0 - 999 (число положительных сообщений)
#define FC_POWER_DAYS	828			// Наработка, дней
#define FC_POWER_HOURS	829			// Наработка, часов
#define FC_RUN_DAYS		840			// Счетчик работы двигателя, дней
#define FC_RUN_HOURS	841			// Счетчик работы двигателя, часов
#define FC_NUM_FAULTS	842			// Счетчик отказов

// Чтение / запись
#define FC_REMOTE_CTRL	172			// Выбор источника сигналов управления: 0 = Клемма ввода/вывода, 1 = Шина Fieldbus
#define FC_REMOTE_REF	117			// Выбор опорной частоты: 1 = Предустановленная скорость 0-7, 2 = Клавиатура, 3 = Шина Fieldbus, 4 = AI1, 5 = AI2, 6 = ПИ-регулятор
#define FC_FREQ_MIN		101			// Мин. частота 0.01 Гц
#define FC_FREQ_MAX		102			// Макс. частота 0.01 Гц
#define FC_MOTOR_CTRL	600			// Режим управления двигателем: 0 = Управление частотой, 1 = Управление скоростью с разомкнутым контуром
#define FC_MOTOR_Uf		108			// Вид кривой U/f: 0 = Линейная, 1 = Квадратичная, 2 = Программируемая
#define FC_TORQUE_BOOST	109			// Форсировка момента: 0 = Запрещено, 1 = Разрешено
#define FC_SW_FREQ		601			// Частота коммутации (ШИМ), 0.1 кГц
#define FC_COMM_ST_RESET 815		// 1 = Сброс состояния связи FC_COMM_STATUS
#define FC_MOTOR_NVOLT	110			// Номинальное напряжение двигателя
#define FC_MOTOR_NA		113			// Номинальный ток двигателя
#define FC_MOTOR_NCOS	120			// коэфф. мощности
#define FC_MOTOR_MA		107			// Максимальный ток двигателя

// Системные:
// Чтение
#define FC_STATUS		2101		// Слово состояния FB
#define FC_SPEED		2103		// Фактическая скорость FB, 0.01 %
#define FC_FREQ			2104		// Выходная частота, +/- 0.01 Гц
#define FC_FREQ_MUL		1			// приведение к двум знакам после запятой
#define FC_RPM			2105		// Скорость двигателя, +/- об/мин
#define FC_CURRENT		2106		// Ток двигателя, 0.01 A
#define FC_TORQUE		2107		// Крутящий момент двигателя от номинального, +/- 0.1 %
#define FC_POWER		2108		// Мощность двигателя от номинального, +/- 0.1 %
#define FC_POWER_IN_PERCENT			// Мощность (FC_POWER) в десятых %
#define FC_VOLTAGE		2109		// Напряжение двигателя, 0.1 В
#define FC_VOLTAGE_DC	2110		// Напряжение шины постоянного тока, 1 В
#define FC_ERROR		2111		// Код активного отказа

// Запись
#define FC_CONTROL		2001		// Слово управления FB
#define FC_SET_SPEED	2003		// Задание скорости FB, 0.01 %
#define FC_SOURCE		2004		// источник уставки, если P15.1=3
#define FC_FEEDBACK		2005		// источник обратной связи, если P15.4=2

// Биты
// FC_STATE
#define FC_S_RDY		0x01	// Привод готов
const char *FC_S_RDY_str			= {"Ready,"};
#define FC_S_RUN		0x02	// Привод работает
const char *FC_S_RUN_str			= {"Run,"};
#define FC_S_DIR		0x04	// 0 - По часовой стрелке, 1 - Против часовой стрелки
const char *FC_S_DIR_str			= {"CCW,"};
#define FC_S_FLT		0x08	// Действующий отказ
const char *FC_S_FLT_str			= {"Fault,"};
#define FC_S_W			0x10	// Сигнал тревоги
const char *FC_S_W_str				= {"Alarm,"};
#define FC_S_AREF		0x20	// 0 - Линейное изменение скорости, 1 - Задание скорости достигнуто
const char *FC_S_AREF_str0			= {"Ramping,"};
#define FC_S_Z			0x40	// 1 - Привод работает на нулевой скорости
const char *FC_S_Z_str				= {"Stopped,"};
// FC_CONTROL
#define FC_C_RUN		0x01	// 0 - Останов, 1 - Выполнение
#define FC_C_STOP		0
#define FC_C_DIR		0x02	// 0 - По часовой стрелке, 1 - Против часовой стрелки
#define FC_C_RST		0x04	// Сброс отказа
#define FC_C_COOLER_FAN (1<<13)	// Вкл. вентилятора
#define FC_C_COOLER_FAN_STR "FB.B13"

const uint8_t FC_NonCriticalFaults[] = { 1, 2, 8, 9, 13, 14,/**/15, 16, 17, 25, 34, 41, 53 }; // Не критичные ошибки, которые можно сбросить

const uint8_t FC_Faults_code[] = {
	0,
	1,  // FC_ERR_Overcurrent
	2,  // FC_ERR_Overvoltage
	3,  // FC_ERR_Earth_fault
	8,  // FC_ERR_System_fault
	9,  // FC_ERR_Undervoltage
	11, // FC_ERR_Output_phase_fault
	13, // FC_ERR_FC_Undertemperature
	14, // FC_ERR_FC_Overtemperature
	15, // FC_ERR_Motor_stalled
	16, // FC_ERR_Motor_overtemperature
	17, // FC_ERR_Motor_underload
	22, // FC_ERR_EEPROM_checksum_fault
	25, // FC_ERR_MC_watchdog_fault
	27, // FC_ERR_Back_EMF_protection
	34, // FC_ERR_Internal_bus_comm
	35, // FC_ERR_Application_fault
	41, // FC_ERR_IGBT_Overtemperature
	50, // FC_ERR_Analog_input_wrong
	51, // FC_ERR_External_fault
	53, // FC_ERR_Fieldbus_fault
	55, // FC_ERR_Wrong_run_faul
	57  // FC_ERR_Idenfication_fault
};

const char *FC_Faults_str[] = {	"Ok", // нет ошибки
								"Overcurrent",
								"Overvoltage",
								"Earth fault",
								"System fault",
								"Undervoltage",
								"Output phase fault",
								"FC Undertemperature",
								"FC Overtemperature",
								"Motor stalled",
								"Motor overtemperature",
								"Motor underload",
								"EEPROM checksum fault",
								"MC watchdog fault",
								"Back EMF protection",
								"Internal bus comm",
								"Application fault",
								"IGBT Overtemperature",
								"Analog input wrong",
								"External fault",
								"Fieldbus fault",
								"Wrong run faul",
								"Idenfication fault",

								"Unknown"}; // sizeof(FC_Faults_code)+1
#endif //FC_VLT

#ifdef FC_VACON
#define ERR_LINK_FC 0         	    // Состояние инертора - нет связи по Modbus
#endif

class devVaconFC
{
public:
  int8_t	initFC();                               // Инициализация Частотника
  __attribute__((always_inline)) inline boolean get_present(){return GETBIT(flags,fFC);} // Наличие датчика в текущей конфигурации
  int8_t	get_err(){return err;}                  // Получить последню ошибку частотника
  uint16_t	get_numErr(){return numErr;}            // Получить число ошибок чтения
  void		get_paramFC(char *var, char *ret);      // Получить параметр инвертора в виде строки - get_pFC('x')
  boolean	set_paramFC(char *var, float f);        // Установить параметр инвертора из строки - set_pFC('x')

   // Получение отдельных значений 
  uint16_t get_Uptime(){return _data.Uptime;}				     // Время обновления алгоритма пид регулятора (сек) Основной цикл управления
  int16_t get_PidFreqStep(){return _data.PidFreqStep;}          // Максимальный шаг (на увеличение) изменения частоты при ПИД регулировании в 0.01 Гц Необходимо что бы ЭРВ успевал
  int16_t get_PidStop(){return _data.PidStop;}				     // Проценты от уровня защит (мощность, ток, давление, темпеартура) при которой происходит блокировка роста частоты пидом
  int16_t get_dtCompTemp(){return _data.dtCompTemp;}    		 // Защита по температуре компрессора - сколько градусов не доходит до максимальной (TCOMP) и при этом происходит уменьшение частоты
  int16_t get_startFreq(){return _data.startFreq;}              // Стартовая частота инвертора (см компрессор) в 0.01
  int16_t get_startFreqBoiler(){return _data.startFreqBoiler;}  // Стартовая частота инвертора (см компрессор) в 0.01  ГВС
  int16_t get_minFreq(){return _data.minFreq;}                  // Минимальная  частота инвертора (см компрессор) в 0.01
  int16_t get_minFreqCool(){return _data.minFreqCool;}          // Минимальная  частота инвертора при охлаждении в 0.01
  int16_t get_minFreqBoiler(){return _data.minFreqBoiler;}      // Минимальная  частота инвертора при нагреве ГВС в 0.01
  int16_t get_minFreqUser(){return _data.minFreqUser;}          // Минимальная  частота инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01
  int16_t get_maxFreq(){return _data.maxFreq;}                  // Максимальная частота инвертора (см компрессор) в 0.01
  int16_t get_maxFreqCool(){return _data.maxFreqCool;}          // Максимальная частота инвертора в режиме охлаждения  в 0.01
  int16_t get_maxFreqBoiler(){return _data.maxFreqBoiler;}      // Максимальная частота инвертора в режиме ГВС в 0.01 Гц поглощение бойлера обычно меньше чем СО
  int16_t get_maxFreqUser(){return _data.maxFreqUser;}          // Максимальная частота инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01
  int16_t get_stepFreq(){return _data.stepFreq;}                // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока (см компрессор) в 0.01
  int16_t get_stepFreqBoiler(){return _data.stepFreqBoiler;}    // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока ГВС в 0.01
  int16_t get_dtTemp(){return _data.dtTemp;}                    // Привышение температуры от уставок (подача) при которой срабатыват защита (уменьшается частота) в сотых градуса
  int16_t get_dtTempBoiler(){return _data.dtTempBoiler;}        // Привышение температуры от уставок (подача) при которой срабатыват защита ГВС в сотых градуса
  int16_t get_maxFreqGen(){return _data.maxFreqGen;}            // Максимальная частота инвертора при работе от генератора в 0.01
  uint16_t get_PidMaxStep(){return _data.PidMaxStep;}
  uint16_t get_MaxPower() { return _data.MaxPower; }
  uint16_t get_MaxPowerBoiler() { return _data.MaxPowerBoiler; }
#ifdef DEFROST
  int16_t get_defrostFreq(){ return _data.defrostFreq; }
#endif
  
  // Управление по модбас
  uint16_t	get_power(void) { return power; }		  // Получить текущую мощность в Вт
  uint16_t	get_current();							  // Получить текущий ток в 0.01А
  void		get_infoFC(char *buf);                   // Получить информацию о частотнике
  void		get_infoFC_status(char *buffer, uint16_t st); // Вывести в buffer строковый статус.
  boolean	reset_errorFC();                        // Сброс ошибок инвертора
  boolean	reset_FC();               		      // Сброс состояния связи модбас
  int16_t	CheckLinkStatus(void);				   // Получить Слово состояния FB, ERR_LINK_FC - ошибка связи
  int16_t	read_stateFC();                        // Текущее состояние инвертора
  __attribute__((always_inline)) inline int16_t get_state(void) { return state; };
  int16_t	read_tempFC();                         // Tемпература радиатора
  void		set_nominal_power(void);
   
  int16_t	get_target() {return FC_target;}                    // Получить целевую скорость в сотых %
  int8_t	set_target(int16_t x,boolean show, int16_t _min, int16_t _max);// Установить целевую скорость в %, show - выводить сообщение или нет + границы
  int16_t	get_frequency() { return FC_curr_freq; }	// Получить текущую частоту в сотых Гц
  inline uint32_t get_startTime(){return startCompressor;}// Получить время старта компрессора
  int8_t	get_readState();                          // Прочитать (внутренние переменные обновляются) состояние Инвертора, возвращает или ОК или ошибку
  int8_t	start_FC();                                // Команда ход на инвертор (целевая скорость выставляется)
  int8_t	stop_FC();                                 // Команда стоп на инвертор
  inline bool isfOnOff(){ return GETBIT(flags,fOnOff); }// получить состояние инвертора вкл или выкл
  inline bool isRetOilWork(){ return GETBIT(flags, fFC_RetOilSt); }
  bool		check_blockFC();                          // Установить запрет на использование инвертора
  inline bool get_blockFC() { return GETBIT(flags, fErrFC); }// Получить флаг блокировки инвертора

#ifdef FC_VLT
  void get_fault_str(char *ret, const char *arr[], uint32_t code); // Возвращает строкове представление ошибки или предупреждения
#else
  const char *get_fault_str(uint8_t fault); // Возвращает название ошибки
#endif

#ifdef FC_ANALOG_CONTROL
  // Аналоговое управление
  int16_t get_DAC(){return dac;};                  // Получить установленное значеие ЦАП
#endif
  // Сервис
  char *	get_note(){return  note;}                // Получить описание
  char *	get_name(){return  name;}                // Получить имя
  uint8_t  *get_save_addr(void) { return (uint8_t *)&_data; } // Адрес структуры сохранения
  uint16_t  get_save_size(void) { return sizeof(_data); } // Размер структуры сохранения

#ifndef FC_ANALOG_CONTROL    // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
  int16_t  read_0x03_16(uint16_t cmd);             // Функция Modbus 0х03 прочитать 2 байта
  uint32_t read_0x03_32(uint16_t cmd);             // Функция Modbus 0х03 прочитать 4 байта
  int8_t   write_0x06_16(uint16_t cmd, uint16_t data);// Запись данных (2 байта) в регистр cmd возвращает код ошибки
#endif
#ifdef FC_POWER_IN_PERCENT
  uint16_t nominal_power;							// Номинальная мощность двигателя Вт
#endif

 private:
  void     Adjust_EEV(int16_t freq_delta);

  int8_t   err;										// ошибка частотника (работа) при ошибке останов ТН
  uint16_t numErr;									// число ошибок чтение по модбасу
  uint8_t  number_err;								// Число ошибок связи при превышении FC_NUM_READ блокировка инвертора
   // Управление по 485
  int16_t  FC_target;								// Целевая скорость инвертора в 0.01 %
  int16_t  FC_curr_freq;							// Чтение: текущая частота двигателя в 0.01 Гц
  uint16_t power;									// Чтение: Текущая мощность двигателя в Вт
  int8_t   FC_Temp;									// Чтение: Температура радиатора инвертора, градусы
#ifdef FC_MAX_CURRENT
  uint16_t current;									// Чтение: Текущий ток двигателя в 0.01 Ампер единицах
#endif
#ifdef FC_VLT
  uint32_t state;									// Чтение: Состояние ПЧ регистр FC_STATUS
#else
  int16_t  state;									// Чтение: Состояние ПЧ регистр FC_STATUS
#endif
  int16_t  minFC;									// Минимальная скорость инвертора в 0.01 %
  int16_t  maxFC;									// Максимальная скорость инвертора в 0.01 %
  uint32_t startCompressor;							// время старта компрессора

#ifdef FC_ANALOG_CONTROL
  // Аналоговое управление
  int16_t dac;                                     // Текущее значение ЦАП
  uint8_t pin;                                     // Ножка куда прицеплено FC
#endif
  
  char *note;                                      // Описание
  char *name;                                      // Имя

 // Структура для сохранения настроек, Uptime всегда первый
  struct {
	  uint16_t Uptime;				    // Время обновления алгоритма пид регулятора (сек) Основной цикл управления
	  int16_t  PidFreqStep;             // Максимальный шаг (на увеличение) изменения частоты при ПИД регулировании в 0.01 Гц Необходимо что бы ЭРВ успевал
	  int16_t  PidStop;				    // Проценты от уровня защит (мощность, ток, давление, температура) при которой происходит блокировка роста частоты пидом
	  int16_t  dtCompTemp;    		    // Защита по температуре компрессора - сколько градусов не доходит до максимальной (TCOMP) и при этом происходит уменьшение частоты
	  int16_t  startFreq;               // Стартовая скорость инвертора (см компрессор) в 0.01 %
	  int16_t  startFreqBoiler;         // Стартовая скорость инвертора (см компрессор) в 0.01 % ГВС
	  int16_t  minFreq;                 // Минимальная  скорость инвертора (см компрессор) в 0.01 %
	  int16_t  minFreqCool;             // Минимальная  скорость инвертора при охлаждении в 0.01 %
	  int16_t  minFreqBoiler;           // Минимальная  скорость инвертора при нагреве ГВС в 0.01 %
	  int16_t  minFreqUser;             // Минимальная  скорость инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01 %
	  int16_t  maxFreq;                 // Максимальная скорость инвертора (см компрессор) в 0.01 %
	  int16_t  maxFreqCool;             // Максимальная скорость инвертора в режиме охлаждения  в 0.01 %
	  int16_t  maxFreqBoiler;           // Максимальная скорость инвертора в режиме ГВС в 0.01 Гц поглощение бойлера обычно меньше чем СО
	  int16_t  maxFreqUser;             // Максимальная скорость инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01 %
	  int16_t  stepFreq;                // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока (см компрессор) в 0.01 %
	  int16_t  stepFreqBoiler;          // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока ГВС в 0.01 %
	  int16_t  dtTemp;                  // Превышение температуры от уставок (подача) при которой срабатыват защита (уменьшается частота) в сотых градуса
	  int16_t  dtTempBoiler;            // Превышение температуры от уставок (подача) при которой срабатыват защита ГВС в сотых градуса
	  uint16_t setup_flags;             // флаги настройки - см. define FC_SAVED_FLAGS
	  int16_t  ReturnOilPeriod;			// в FC_TIME_READ
	  int16_t  ReturnOilPerDivHz;		// Уменьшение периода в FC_TIME_READ на каждый Гц
	  uint16_t ReturnOilTime;			// Время возврата, в периодах опроса инвертора (FC_TIME_READ)
	  int16_t  maxFreqGen;				// Максимальная скорость инвертора при работе от генератора в 0.01 %
	  int16_t  AdjustEEV_k;				// Подстройки ЭРВ при изменении оборотов, множитель, сотые шага ЭРВ
	  uint16_t PidMaxStep;				// Максимальный шаг изменения частоты инвертора у PID регулятора, сотые
	  uint16_t ReturnOilMinFreq;		// Частота меньше которой должен происходить возврат масла, в сотых Гц
	  uint16_t ReturnOilFreq;			// Частота возврата масла, в сотых %
	  int16_t  ReturnOil_AdjustEEV_k;	// Подстройки ЭРВ при изменении оборотов, множитель, сотые шага ЭРВ
	  uint16_t MaxPower;				// Максимальная мощность инвертора, Вт
	  uint16_t MaxPowerBoiler;			// Максимальная мощность инвертора при нагреве бойлера, Вт
	  int8_t   FC_MaxTemp;				// Максимальная температура внутри инвертора, градусы, 0 - не проверяется
	  int8_t   FC_TargetTemp;			// Целевая температура внутри инвертора, градусы, 0 - не регулируется
#ifdef DEFROST
	  int16_t  defrostFreq;             // Скорость инвертора при разморозки в 0.01 %
#endif
#ifdef FC_ANALOG_CONTROL
	  int16_t  level0;                  // Отсчеты ЦАП соответсвующие 0   скорость
	  int16_t  level100;                // Отсчеты ЦАП соответсвующие максимальной скорости
#endif
   } _data;  // Структура для сохранения настроек
   uint16_t flags;  					// рабочие флаги
   int16_t	ReturnOilTimer;
   int16_t  Adjust_EEV_delta;
 };

#endif
