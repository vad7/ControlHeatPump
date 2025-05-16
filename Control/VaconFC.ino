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
#include "VaconFC.h"

int8_t devVaconFC::initFC()
{
	err = OK; // ошибка частотника (работа) при ошибке останов ТН
	numErr = 0; // число ошибок чтение по модбасу для статистики
	number_err = 0; // Число ошибок связи при превышении FC_NUM_READ блокировка инвертора
	FC_target = 0; // Целевая скорость частотика
	FC_curr_freq = 0; // текущая частота инвертора
	power = 0; // Тееущая мощность частотника
#ifdef FC_MAX_CURRENT
	current = 0; // Текуший ток частотника
#endif
	startCompressor = 0; // время старта компрессора
	state = ERR_LINK_FC; // Состояние - нет связи с частотником по Modbus
	Adjust_EEV_delta = 0;

	name = (char*) FC_VACON_NAME; // Имя
	note = (char*) noteFC_NONE; // Описание инвертора   типа нет его

	_data.Uptime = DEF_FC_UPTIME;				       // Время обновления алгоритма пид регулятора (мсек) Основной цикл управления
	_data.PidFreqStep = DEF_FC_PID_FREQ_STEP;          // Максимальный шаг (на увеличение) изменения частоты при ПИД регулировании в 0.01 Гц Необходимо что бы ЭРВ успевал
	_data.PidStop = DEF_FC_PID_STOP;				   // Проценты от уровня защит (мощность, ток, давление, темпеартура) при которой происходит блокировка роста частоты пидом
	_data.dtCompTemp = DEF_FC_DT_COMP_TEMP;    		   // Защита по температуре компрессора - сколько градусов не доходит до максимальной (TCOMP) и при этом происходит уменьшение частоты
	_data.startFreq = DEF_FC_START_FREQ;               // Стартовая скорость инвертора (см компрессор) в 0.01
	_data.startFreqBoiler = DEF_FC_START_FREQ_BOILER;  // Стартовая скорость инвертора (см компрессор) в 0.01 ГВС
	_data.minFreq = DEF_FC_MIN_FREQ;                   // Минимальная  скорость инвертора (см компрессор) в 0.01
	_data.minFreqCool = DEF_FC_MIN_FREQ_COOL;          // Минимальная  скорость инвертора при охлаждении в 0.01
	_data.minFreqBoiler = DEF_FC_MIN_FREQ_BOILER;      // Минимальная  скорость инвертора при нагреве ГВС в 0.01
	_data.minFreqUser = DEF_FC_MIN_FREQ_USER;          // Минимальная  скорость инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01
	_data.maxFreq = DEF_FC_MAX_FREQ;                   // Максимальная скорость инвертора (см компрессор) в 0.01
	_data.maxFreqCool = DEF_FC_MAX_FREQ_COOL;          // Максимальная скорость инвертора в режиме охлаждения  в 0.01
	_data.maxFreqBoiler = DEF_FC_MAX_FREQ_BOILER;      // Максимальная скорость инвертора в режиме ГВС в 0.01 Гц поглощение бойлера обычно меньше чем СО
	_data.maxFreqUser = DEF_FC_MAX_FREQ_USER;          // Максимальная скорость инвертора РУЧНОЙ РЕЖИМ (см компрессор) в 0.01
	_data.stepFreq = DEF_FC_STEP_FREQ;                 // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока (см компрессор) в 0.01
	_data.stepFreqBoiler = DEF_FC_STEP_FREQ_BOILER;    // Шаг уменьшения инвертора при достижении максимальной температуры, мощности и тока ГВС в 0.01
	_data.dtTemp = DEF_FC_DT_TEMP;                     // Превышение температуры от уставок (подача) при которой срабатыват защита (уменьшается частота) в сотых градуса
	_data.dtTempBoiler = DEF_FC_DT_TEMP_BOILER;        // Превышение температуры от уставок (подача) при которой срабатыват защита ГВС в сотых градуса
	_data.PidMaxStep = 500;
	_data.ReturnOilMinFreq = FC_RETOIL_FREQ;
	_data.ReturnOilFreq = 3000; // %
	_data.ReturnOilPeriod = 1800000 / FC_TIME_READ;
	_data.ReturnOilPerDivHz = 48000 / FC_TIME_READ;
	_data.ReturnOilTime = FC_RETOIL_TIME;
	_data.MaxPower = FC_MAX_POWER;
	_data.MaxPowerBoiler = FC_MAX_POWER_BOILER;

	flags = 0x00;                                	   // флаги  0 - наличие FC
	_data.setup_flags = 0;
#ifndef FC_ANALOG_CONTROL
	if(!Modbus.get_present()) // modbus отсутствует
	{
		SETBIT0(flags, fFC); // Инвертор не работает
		journal.jprintf("%s, modbus not found, block.\n", name);
		err = ERR_NO_MODBUS;
		return err;
	} else if(DEVICEFC == true) SETBIT1(flags, fFC); // наличие частотника в текушей конфигурации
#else
	pin = PIN_DEVICE_FC;                			  // Ножка куда прицеплено FC
	analogWriteResolution(FC_ANALOG_RESOLUTION);      // разрешение ЦАП, бит;
	_data.level0 = 0;                                 // Отсчеты ЦАП соответсвующие 0   мощности
	_data.level100 = (1<<FC_ANALOG_RESOLUTION) - 1;   // Отсчеты ЦАП соответсвующие 100 мощности
	SETBIT1(flags, fFC); // наличие частотника в текушей конфигурации
	note = (char*)"OK";
#endif

	if(get_present()) journal.jprintf("Invertor %s: present config\n", name);
	else {
		journal.jprintf("Invertor %s: none config\n", name);
		return err;
	} // выходим если нет инвертора

#ifndef FC_ANALOG_CONTROL // НЕ Аналоговое управление
	CheckLinkStatus(); // проверка связи с инвертором
	if(err != OK) return err;// связи нет выходим
	journal.jprintf("Test link Modbus %s: OK\n", name);// Тест пройден

	uint8_t i = 3;
	while((state & FC_S_RUN))// Если частотник работает то остановить его
	{
		if(i-- == 0) break;
		stop_FC();
		journal.jprintf("Wait stop %s...\n", name);
		_delay(3000);
		CheckLinkStatus(); //  Получить состояние частотника
	}
	if(i == 0) { // Не останавливается
		err = ERR_MODBUS_STATE;
		set_Error(err, name);
	}
	if(err == OK) {
		// 10.Установить стартовую частоту
		set_target(_data.startFreq, true, _data.minFreqUser, _data.maxFreqUser);// режим не знаем по этому границы развигаем
	}
	set_nominal_power();
#endif // #ifndef FC_ANALOG_CONTROL
	return err;
}

// Вычисление номинальной мощности двигателя компрессора = U*sqrt(3)*I*cos, W
void devVaconFC::set_nominal_power(void)
{
#ifdef FC_POWER_IN_PERCENT
#ifndef FC_ANALOG_CONTROL
	//nominal_power = (uint32_t) (400) * (700) / 100 * (75) / 100; // W
	typeof(nominal_power) n = nominal_power;
	uint32_t pwr;
	pwr = read_0x03_16(FC_MOTOR_NVOLT) * 173;
	if(err) return;
	pwr *= read_0x03_16(FC_MOTOR_NA);
	if(err) return;
	pwr = pwr / 100 * read_0x03_16(FC_MOTOR_NCOS) / 100 / 100;
	if(err) return;
	nominal_power = pwr;
#ifdef FC_CORRECT_NOMINAL_POWER
	nominal_power += FC_CORRECT_NOMINAL_POWER;
#endif
	if(n != nominal_power) journal.jprintf(" FC Nominal power: %d W\n", nominal_power);
#endif
#endif
}

// Возвращает состояние, или ERR_LINK_FC, если нет связи по Modbus
int16_t devVaconFC::CheckLinkStatus(void)
{
#ifndef FC_ANALOG_CONTROL // НЕ Аналоговое управление
    if(testMode == NORMAL || testMode == HARD_TEST){
		for (uint8_t i = 0; i < FC_NUM_READ; i++) // Чтение состояния инвертора, при ошибке генерация общей ошибки ТН и останов
		{
			err = Modbus.readHoldingRegisters16(FC_MODBUS_ADR, FC_STATUS - 1, (uint16_t *)&state); // Послать запрос, Нумерация регистров с НУЛЯ!!!!
			if(check_blockFC()) break; // проверить необходимость блокировки
			_delay(FC_DELAY_REPEAT);
		}
		if(err != OK) state = ERR_LINK_FC;
    } else {
    	SETBIT0(flags, fErrFC);
    	state = FC_S_RDY;
    	err = OK;
    }
    return state;
#else
    return OK;
#endif
}

// Прочитать (внутренние переменные обновляются) состояние Инвертора, возвращает или ОК или ошибку
// Вызывается из задачи чтения датчиков период FC_TIME_READ
int8_t devVaconFC::get_readState()
{
	if(testMode != NORMAL && testMode != HARD_TEST) {
		err = OK;
		state = FC_S_RDY;
		power = 600;
		FC_curr_freq = 4000 + (int32_t)FC_target * 30 / 100;
	} else {
#ifndef FC_ANALOG_CONTROL // Не аналоговое управление
		if(!get_present()) {
			state = ERR_LINK_FC;
			power = FC_curr_freq = 0;
#ifdef FC_MAX_CURRENT
			current = 0;
#endif
			return err; // выходим если нет инвертора или нет связи
		}
		// Чтение состояния инвертора, при ошибке генерация общей ошибки ТН и останов
#ifdef FC_VLT
		state = read_0x03_32(FC_STATUS); // прочитать состояние
#else
		state = read_0x03_16(FC_STATUS); // прочитать состояние
#endif
		if(err) // Ошибка
		{
			state = ERR_LINK_FC; // признак потери связи с инвертором
			power = FC_curr_freq = 0;
#ifdef FC_MAX_CURRENT
			current = 0;
#endif
			if(!GETBIT(flags, fOnOff)) return err; // Если не работаем, то и ошибку не генерим
#if defined(SPOWER)
			if(HP.NO_Power) return err;
			HP.sInput[SPOWER].Read(true);
			if(HP.sInput[SPOWER].is_alarm()) {
				HP.sendCommand(pWAIT);
				HP.NO_Power = 2;
				return err;
			}
#endif
#if defined(SGENERATOR)
			HP.sInput[SGENERATOR].Read(true);
			if(HP.sInput[SGENERATOR].get_Input() == HP.sInput[SGENERATOR].get_alarmInput()) {
				HP.sendCommand(pWAIT);
				SETBIT1(HP.flags, fHP_BackupNoPwrWAIT);
				return err;
			}
#endif
			SETBIT1(flags, fErrFC); // Блок инвертора
			set_Error(err, name); // генерация ошибки
			return err; // Возврат
		}
		if(GETBIT(flags, fOnOff)) { // ТН включил компрессор, проверяем состояние инвертора
			if(state & FC_S_FLT) { // Действующий отказ
#ifdef FC_VLT
				uint32_t reg = read_0x03_32(FC_ERROR);
				journal.jprintf("%s, Fault: %X - ", name);
				get_fault_str(NULL, FC_Alarm_str, reg);
#else
				uint16_t reg = read_0x03_16(FC_ERROR);
				journal.jprintf("%s, Fault: %s(%d) - ", name, reg ? get_fault_str(reg) : cError, reg ? reg : err);
#endif
				err = ERR_FC_FAULT;
				if(HP.NO_Power) return err;
				if(GETBIT(HP.Option.flags, fBackupPower)) {
//					HP.sendCommand(pWAIT);
//					SETBIT1(HP.flags, fHP_BackupNoPwrWAIT);
//					return err;
					// Нужно перезапустить
					if(HP.is_compressor_on()) {
						if(get_present()) stop_FC(); else HP.dRelay[RCOMP].set_OFF();
						HP.set_stopCompressor();
					}
					HP.sendCommand(pREPEAT_FAST);
					return err;
				}
			} else if(get_startTime() && rtcSAM3X8.unixtime() - get_startTime() > FC_ACCEL_TIME / 100 && ((state & (FC_S_RDY | FC_S_RUN | FC_S_DIR)) != (FC_S_RDY | FC_S_RUN))) {
				err = ERR_MODBUS_STATE;
			}
			if(err) {
				set_Error(err, name); // генерация ошибки
				return err;
			}
		} else if(state & FC_S_RUN) { // Привод работает, а не должен - останавливаем через модбас
			if(rtcSAM3X8.unixtime() - HP.get_stopCompressor() > FC_DEACCEL_TIME / 100) {
				journal.jprintf_time("Compressor running - stop via Modbus!\n");
				err = write_0x06_16(FC_CONTROL, FC_C_STOP); // подать команду ход/стоп через модбас
				if(err) return err;
			}
		}
		// Состояние прочитали и оно правильное все остальное читаем
		{
			FC_curr_freq = read_0x03_16(FC_FREQ) * FC_FREQ_MUL; 	// прочитать текущую частоту
			if(err == OK) {
#ifdef FC_POWER_IN_PERCENT
				power = (uint32_t)nominal_power * read_0x03_16(FC_POWER) / 1000; 	// прочитать мощность
#else
				power = read_0x03_16(FC_POWER); 	// прочитать мощность
#endif
#ifdef FC_MAX_CURRENT
				if(err == OK) current = read_0x03_16(FC_CURRENT);
#endif
				if(err == OK) {
					FC_Temp = read_0x03_16(FC_TEMP);
					if(err == OK && _data.FC_MaxTemp && FC_Temp > _data.FC_MaxTemp) {
						set_Error(err = ERR_MODBUS_VACON_TEMP, name); // генерация ошибки
						return err;
					}
					if(_data.FC_TargetTemp) {
#ifdef FC_VLT
						if(FC_Temp > _data.FC_TargetTemp) write_0x06_16(FC_CONTROL, FC_C_COOLER_FAN | (state & FC_S_RUN ? FC_C_RUN : 0)); // подать команду
						else if(FC_Temp < _data.FC_TargetTemp - FC_COOLER_FUN_HYSTERESIS) write_0x06_16(FC_CONTROL, (state & FC_S_RUN ? FC_C_RUN : 0)); // подать команду
#else
						if(FC_Temp > _data.FC_TargetTemp) write_0x06_16(FC_CONTROL, FC_C_COOLER_FAN); // подать команду
						else if(FC_Temp < _data.FC_TargetTemp - FC_COOLER_FUN_HYSTERESIS) write_0x06_16(FC_CONTROL, 0); // подать команду
#endif
					}
				}
				if(GETBIT(_data.setup_flags,fLogWork) && GETBIT(flags, fOnOff)) {
					journal.jprintf_time("FC: %Xh,%.2dHz,%.3d\n", state, FC_curr_freq, get_power());
				}
			}
		}
		if(GETBIT(flags, fOnOff) && err == OK) {
			if(Adjust_EEV_delta) {
#ifdef EEV_DEF
				int16_t n = HP.dEEV.get_EEV() + Adjust_EEV_delta;
				if(n < HP.dEEV.get_minEEV()) n = HP.dEEV.get_minEEV(); else if(n > HP.dEEV.get_maxEEV()) n = HP.dEEV.get_maxEEV();
				if(GETBIT(HP.dEEV.get_flags(), fEEV_DirectAlgorithm)) {
					HP.dEEV.pidw.max = 1 + (abs(n - HP.dEEV.get_EEV()) > 5 ? 1 : 0); // пропустить итераций
				}
				HP.dEEV.set_EEV(n);
#endif
				Adjust_EEV_delta = 0;
			}
#ifdef FC_RETOIL_FREQ
			if(GETBIT(_data.setup_flags, fFC_RetOil)) {
				if(!GETBIT(flags, fFC_RetOilSt)) {
					if(FC_curr_freq < _data.ReturnOilMinFreq && (FC_curr_freq < _data.maxFreqGen || !GETBIT(HP.Option.flags, fBackupPower))) {
						if(++ReturnOilTimer >= _data.ReturnOilPeriod - (_data.ReturnOilMinFreq - FC_curr_freq) * _data.ReturnOilPerDivHz / 100) {
							flags |= 1 << fFC_RetOilSt;
#ifdef FC_VLT
							err = write_0x06_16((uint16_t) FC_SET_SPEED, map(_data.ReturnOilFreq, 0, 10000, 0, 16384));
#else
							err = write_0x06_16((uint16_t) FC_SET_SPEED, _data.ReturnOilFreq);
#endif
							Adjust_EEV(_data.ReturnOilFreq - FC_target);
							ReturnOilTimer = 0;
						}
					} else ReturnOilTimer = 0;
				} else {
					if(++ReturnOilTimer >= _data.ReturnOilTime) {
						Adjust_EEV(FC_target - _data.ReturnOilFreq);
						flags &= ~(1 << fFC_RetOilSt);
#ifdef FC_VLT
						err = write_0x06_16((uint16_t) FC_SET_SPEED, map(FC_target, 0, 10000, 0, 16384));
#else
						err = write_0x06_16((uint16_t) FC_SET_SPEED, FC_target);
#endif
						ReturnOilTimer = 0;
					}
				}
			}
#endif
		}
#else // Аналоговое управление
		err = OK;
		power = 0;
	#ifdef FC_MAX_CURRENT
		current = 0;
	#endif
#endif
	}
	return err;
}

void devVaconFC::Adjust_EEV(int16_t freq_delta)
{
	if(GETBIT(flags, fOnOff)) {
		int16_t k = GETBIT(flags, fFC_RetOilSt) ? _data.ReturnOil_AdjustEEV_k : _data.AdjustEEV_k;
		Adjust_EEV_delta = (freq_delta * k + 5000) / 10000L; // +Округление
	}
}

// Установить целевую скорость в %
// show - показывать сообщение сообщение или нет, два оставшихся параметра границы
int8_t devVaconFC::set_target(int16_t x, boolean show, int16_t _min, int16_t _max)
{
#ifdef DEMO
    if((x >= _min) && (x <= _max)) // Проверка диапазона разрешенных частот
    {
        FC_target = x;
        if(show) journal.jprintf(" Set %s: %.2d\n", name, FC_target);
        return err;
    } // установка частоты OK  - вывод сообщения если надо
    else {
    	err = ERR_FC_ERROR;
        journal.jprintf("%s: Wrong frequency %.2d\n", name, x);
        return WARNING_VALUE;
    }
#else // Боевой вариант
    if((!get_present()) || (GETBIT(flags, fErrFC))) return err; // выходим если нет инвертора или он заблокирован по ошибке
    if(x < _min) x = _min; // Проверка диапазона разрешенных частот
    else if(x > _max) x = _max;
    if(GETBIT(HP.Option.flags, fBackupPower) && x > _data.maxFreqGen) x = _data.maxFreqGen;

	err = OK;
#ifdef FC_RETOIL_FREQ
	if(GETBIT(flags, fFC_RetOilSt)) {
		FC_target = x;
		return err;
	}
#endif
#ifndef FC_ANALOG_CONTROL // Не аналоговое управление
	// Запись в регистры инвертора установленной частоты
	if(testMode == NORMAL || testMode == HARD_TEST){
#ifdef FC_VLT
		err = write_0x06_16((uint16_t)FC_SET_SPEED, map(x, 0, 10000, 0, 16384));
#else
		err = write_0x06_16((uint16_t)FC_SET_SPEED, x);
#endif
	}
	if(err == OK) {
		Adjust_EEV(x - FC_target);
		FC_target = x;
		if(show) journal.jprintf(" Set %s[%s]: %.2d%%\n", name, (char *)codeRet[HP.get_ret()], FC_target);
	} else if(err != ERR_NO_POWER_WHILE_WORK) {  // генерация ошибки
		SETBIT1(flags, fErrFC);
		set_Error(err, name);
	}
#else // Аналоговое управление
	Adjust_EEV(x - FC_target);
	FC_target = x;
	dac = (int32_t)FC_target * (_data.level100 - _data.level0) / (100*100) + _data.level0;
	switch (testMode) // РЕАЛЬНЫЕ Действия в зависимости от режима
	{
	case NORMAL:
#ifdef FC_ANALOG_OFF_SET_0
		if(!GETBIT(flags, fOnOff)) return err;
#endif
	case HARD_TEST:
		analogWrite(pin, dac); //  все включаем
		break;
	default:
		break; //  Ничего не включаем
	}
	if(show) {
		journal.jprintf(" Set %s: %.2d%%\n", name, FC_target); // установка частоты OK  - вывод сообщения если надо
    }
#endif
	return err;
#endif // DEMO
}

// Установить запрет на использование инвертора если лимит ошибок исчерпан
// false - норм, true - ошибка
bool devVaconFC::check_blockFC()
{
#ifndef FC_ANALOG_CONTROL // Не аналоговое управление
    if(err != OK) {
        if(xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || ++number_err >= FC_NUM_READ) { // если не запущена free rtos то блокируем с первого раза
#ifdef USE_UPS
			if(HP.NO_Power) return true;
	#if defined(SPOWER)
			HP.sInput[SPOWER].Read(true);
			if(HP.sInput[SPOWER].is_alarm()) {
				HP.HandleNoPower();
				return true;
			}
	#endif
#endif
            SETBIT1(flags, fErrFC); // Установить флаг
            note = (char*)noteFC_NO;
            set_Error(err, (char*)name); // Подъем ошибки на верх и останов ТН
            return true;
		}
    } else {
    	SETBIT0(flags, fErrFC);
        number_err = 0;
        note = (char*)noteFC_OK; // Описание инвертора есть
    }
#endif
    return false;
}

// Команда ход на инвертор (целевая скорость НЕ ВЫСТАВЛЯЕТСЯ)
// Может быть подана команда через реле и через модбас в зависимости от ключей компиляции
int8_t devVaconFC::start_FC()
{
	flags &= ~(1<<fFC_RetOilSt);
	ReturnOilTimer = 0;
    if(testMode != NORMAL && testMode != HARD_TEST) {
        err = OK;
    	goto xStarted;
    }
	if(!get_present() || GETBIT(flags, fErrFC)) return err = ERR_MODBUS_BLOCK; // выходим если нет инвертора или он заблокирован по ошибке
    err = OK;
#ifndef FC_ANALOG_CONTROL // Не аналоговое управление
 #ifdef DEMO
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
    HP.dRelay[RCOMP].set_ON(); // ПЛОХО через глобальную переменную
  #endif // FC_USE_RCOMP
    if(err == OK) {
        SETBIT1(flags, fOnOff);
        startCompressor = rtcSAM3X8.unixtime();
        journal.jprintf(" %s ON\n", name);
    }
    else {
        SETBIT1(flags, fErrFC);
        set_Error(err, name);
    } // генерация ошибки
 #else // DEMO
    // set_target(FC_START_FC,true);  // Запись в регистр инвертора стартовой частоты  НЕ всегда скорость стартовая - супербойлер
	if((state & FC_S_FLT)) { // Действующий отказ
#ifdef FC_VLT
		uint32_t reg = read_0x03_32(FC_ERROR);
		journal.jprintf("%s, Fault: %X - ", name);
		get_fault_str(NULL, FC_Alarm_str, reg);
#else
		uint16_t reg = read_0x03_16(FC_ERROR);
		journal.jprintf("%s, Fault: %s(%d) - ", name, reg ? get_fault_str(reg) : cError, reg ? reg : err);
#endif
		if(GETBIT(_data.setup_flags, fAutoResetFault)) { // Автосброс не критичного сбоя инвертора
			if(!err) { // код считан успешно
#ifdef FC_VLT
				if((reg & ~FC_NonCriticalFaults) == 0)
#else
				uint8_t i = 0;
				for(; i < sizeof(FC_NonCriticalFaults); i++) if(FC_NonCriticalFaults[i] == reg) break;
				if(i < sizeof(FC_NonCriticalFaults))
#endif
				{
					if(reset_errorFC()) {
						if((state & FC_S_FLT)) {
							if(reset_errorFC() && (state & FC_S_FLT)) err = ERR_FC_FAULT; else journal.jprintf("Reseted(2).\n");
						} else journal.jprintf("Reseted.\n");
					}
					if(err) journal.jprintf("reset failed!\n");
				} else {
					journal.jprintf("Critical!\n");
					err = ERR_FC_FAULT;
				}
			}
		} else {
			journal.jprintf("skip start!\n");
			err = ERR_FC_FAULT;
		}
	}
	if(err == OK) {
	    set_nominal_power();
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
		HP.dRelay[RCOMP].set_ON();
  #else
		uint16_t ctrl = read_0x03_16(FC_CONTROL);
		if(!err) err = write_0x06_16(FC_CONTROL, (ctrl & FC_C_COOLER_FAN) | FC_C_RUN); // Команда Ход
  #endif
	}
 #endif // DEMO
    if(err == OK) {
xStarted:
    	SETBIT1(flags, fOnOff);
        startCompressor = rtcSAM3X8.unixtime();
        journal.jprintf(" %s[%s] ON\n", name, (char *)codeRet[HP.get_ret()]);
    } else {
        SETBIT1(flags, fErrFC);
        set_Error(err, name);  // генерация ошибки
    }
#else //  FC_ANALOG_CONTROL
 #ifdef DEMO
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
    HP.dRelay[RCOMP].set_ON(); // ПЛОХО через глобальную переменную
  #endif // FC_USE_RCOMP
xStarted:
    SETBIT1(flags, fOnOff);
    startCompressor = rtcSAM3X8.unixtime();
    journal.jprintf(" %s ON\n", name);
 #else // DEMO
    // Боевая часть
    if((testMode == NORMAL) || (testMode == HARD_TEST)) //   Режим работа и хард тест, все включаем,
    {
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
        HP.dRelay[RCOMP].set_ON(); // ПЛОХО через глобальную переменную
  #else
        err = ERR_FC_CONF_ANALOG;
        SETBIT1(flags, fErrFC);
        set_Error(err, name); // Ошибка конфигурации
  #endif
  #ifdef FC_ANALOG_OFF_SET_0
        dac = (int32_t)FC_target * (_data.level100 - _data.level0) / (100*100) + _data.level0;
        analogWrite(pin, dac);
  #endif
    }
xStarted:
    SETBIT1(flags, fOnOff);
    startCompressor = rtcSAM3X8.unixtime();
    journal.jprintf(" %s ON\n", name);
#endif //DEMO
#endif //FC_ANALOG_CONTROL
    Adjust_EEV_delta = 0;
    return err;
}

// Команда стоп на инвертор Обратно код ошибки
int8_t devVaconFC::stop_FC()
{
#ifndef FC_ANALOG_CONTROL // Не аналоговое управление
 #ifdef DEMO
    err = OK;
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
    HP.dRelay[RCOMP].set_OFF(); // ПЛОХО через глобальную переменную
  #endif // FC_USE_RCOMP
    if(err == OK) {
        SETBIT0(flags, fOnOff);
        startCompressor = 0;
        journal.jprintf(" %s OFF\n", name);
    }
    else {
        SETBIT1(flags, fErrFC);
        set_Error(err, name);
    } // генерация ошибки
 #else // DEMO
    if((testMode == NORMAL) || (testMode == HARD_TEST)) // Режим работа и хард тест, все включаем,
    {
    	if(!get_present()) {
            SETBIT0(flags, fOnOff);
            startCompressor = 0;
            return err; // выходим если нет инвертора или нет связи
    	}
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
        HP.dRelay[RCOMP].set_OFF();
  #endif
    	if(state == ERR_LINK_FC) {
            SETBIT0(flags, fOnOff);
            startCompressor = 0;
            return err; // выходим если нет инвертора или нет связи
    	}
  #ifndef FC_USE_RCOMP
		uint16_t ctrl = read_0x03_16(FC_CONTROL);
		if(!err) err = write_0x06_16(FC_CONTROL, (ctrl & FC_C_COOLER_FAN) | FC_C_STOP); // подать команду ход/стоп через модбас
  #else
        err = OK;
  #endif
    }
    if(err == OK || err == ERR_NO_POWER_WHILE_WORK) {
        journal.jprintf(" %s[%s] OFF\n", name, (char *)codeRet[HP.get_ret()]);
        SETBIT0(flags, fOnOff);
        startCompressor = 0;
    } else {
        SETBIT1(flags, fErrFC);
        set_Error(err, name);
    } // генерация ошибки
 #endif // DEMO
#else // FC_ANALOG_CONTROL
 #ifdef DEMO
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
    HP.dRelay[RCOMP].set_OFF(); // ПЛОХО через глобальную переменную
  #endif // FC_USE_RCOMP
    SETBIT0(flags, fOnOff);
    startCompressor = 0;
    journal.jprintf(" %s OFF\n", name);
 #else // DEMO
    if((testMode == NORMAL) || (testMode == HARD_TEST)) // Режим работа и хард тест, все включаем,
    {
  #ifdef FC_USE_RCOMP // Использовать отдельный провод для команды ход/стоп
        HP.dRelay[RCOMP].set_OFF();
  #else // подать команду ход/стоп через модбас
        err = ERR_FC_CONF_ANALOG;
        SETBIT1(flags, fErrFC);
        set_Error(err, name); // Ошибка конфигурации
  #endif
  #ifdef FC_ANALOG_OFF_SET_0
        analogWrite(pin, FC_target = dac = 0);
  #endif
    }
    SETBIT0(flags, fOnOff);
    startCompressor = 0;
    journal.jprintf(" %s OFF\n", name);
 #endif // DEMO
#endif // FC_ANALOG_CONTROL
    Adjust_EEV_delta = 0;
	return err;
}

// Получить текущий ток, сотые
uint16_t devVaconFC::get_current()
{
#ifdef FC_MAX_CURRENT
	  return current;
#else
	#ifndef FC_ANALOG_CONTROL
	  return (testMode == NORMAL || testMode == HARD_TEST) ? read_0x03_16(FC_CURRENT) : 0;
	#else
	  return 0;
	#endif
#endif
}

// Получить параметр инвертора в виде строки, результат ДОБАВЛЯЕТСЯ в ret
void devVaconFC::get_paramFC(char *var,char *ret)
{

	ret += m_strlen(ret);
    if(strcmp(var,fc_ON_OFF)==0)                { if (GETBIT(flags,fOnOff))  strcat(ret,(char*)cOne);else  strcat(ret,(char*)cZero); } else
    if(strcmp(var,fc_INFO)==0)                  {
#ifndef FC_ANALOG_CONTROL
    	                                        get_infoFC(ret);
#else
                                                strcat(ret, "|Данные не доступны, управление через ") ;
                                                if((g_APinDescription[pin].ulPinAttribute & PIN_ATTR_ANALOG) == PIN_ATTR_ANALOG) strcat(ret, "аналоговый"); else strcat(ret, "ШИМ");
                                                strcat(ret, " выход D");
                                                _itoa(PIN_DEVICE_FC, ret);
                                                strcat(ret, "|;");
#endif
    	                                        } else
    if(strcmp(var,fc_NAME)==0)                  {  strcat(ret,name);             } else
    if(strcmp(var,fc_NOTE)==0)                  {  strcat(ret,note);             } else
    if(strcmp(var,fc_PRESENT)==0)               { if (GETBIT(flags,fFC))  strcat(ret,(char*)cOne);else  strcat(ret,(char*)cZero); } else
    if(strcmp(var,fc_STATE)==0)                 {  _itoa(state,ret);   } else
    if(strcmp(var,fc_FC)==0)                    {  _dtoa(ret, FC_target, 2); strcat(ret, "%"); } else
    if(strcmp(var,fc_INFO1)==0)                 {  _dtoa(ret, FC_target, 2); strcat(ret, "%"); } else
    if(strcmp(var,fc_cFC)==0)                   {  _dtoa(ret, FC_curr_freq, 2); strcat(ret, " Гц"); } else // Текущая частота!
    if(strcmp(var,fc_cPOWER)==0)                {  _itoa(get_power(), ret); } else
    if(strcmp(var,fc_cCURRENT)==0)              {  _dtoa(ret, get_current(), 2); } else
    if(strcmp(var,fc_AUTO_RESET_FAULT)==0)      {  strcat(ret,(char*)(GETBIT(_data.setup_flags,fAutoResetFault) ? cOne : cZero)); } else
    if(strcmp(var,fc_LogWork)==0)      			{  strcat(ret,(char*)(GETBIT(_data.setup_flags,fLogWork) ? cOne : cZero)); } else
    if(strcmp(var,fc_fFC_RetOil)==0)   			{  strcat(ret,(char*)(GETBIT(_data.setup_flags,fFC_RetOil) ? cOne : cZero)); } else
    if(strcmp(var,fc_ReturnOilPeriod)==0)       {  _itoa(_data.ReturnOilPeriod * (FC_TIME_READ/1000), ret); } else
    if(strcmp(var,fc_ReturnOilPerDivHz)==0)     {  _itoa(_data.ReturnOilPerDivHz * (FC_TIME_READ/1000), ret); } else
    if(strcmp(var,fc_ReturnOilMinFreq)==0)      {  _dtoa(ret, _data.ReturnOilMinFreq, 2); } else
    if(strcmp(var,fc_ReturnOilFreq)==0)         {  _dtoa(ret, _data.ReturnOilFreq, 2); } else
    if(strcmp(var,fc_ReturnOilTime)==0)         {  _itoa(_data.ReturnOilTime * (FC_TIME_READ/1000), ret); } else
    if(strcmp(var,fc_ANALOG)==0)                { // Флаг аналогового управления
#ifdef FC_ANALOG_CONTROL
		                                         strcat(ret,(char*)cOne);
#else
		                                         strcat(ret,(char*)cZero);
#endif
                                                } else
#ifdef FC_ANALOG_CONTROL
    if(strcmp(var,fc_PIN)==0)                   {  _itoa(pin,ret);     	    } else
    if(strcmp(var,fc_DAC)==0)                   {  _itoa(dac,ret);          } else
    if(strcmp(var,fc_LEVEL0)==0)                {  _itoa(_data.level0,ret);       } else
    if(strcmp(var,fc_LEVEL100)==0)              {  _itoa(_data.level100,ret);     } else
#endif
#ifdef DEFROST
   	if(strcmp(var,fc_defrostFreq)==0)           {  _dtoa(ret, _data.defrostFreq, 2); } else // %
#endif
    if(strcmp(var,fc_BLOCK)==0)                 { if (GETBIT(flags,fErrFC))  strcat(ret,(char*)cYes); } else
    if(strcmp(var,fc_ERROR)==0)                 {  _itoa(err,ret);          } else
    if(strcmp(var,fc_UPTIME)==0)                {  _itoa(_data.Uptime,ret); } else   // вывод в секундах
    if(strcmp(var,fc_PID_STOP)==0)              {  _itoa(_data.PidStop,ret);          } else
    if(strcmp(var,fc_DT_COMP_TEMP)==0)          {  _dtoa(ret, _data.dtCompTemp, 2); } else // градусы

	if(strcmp(var,fc_PID_FREQ_STEP)==0)         {  _dtoa(ret, _data.PidFreqStep,2); } else // %
	if(strcmp(var,fc_START_FREQ)==0)            {  _dtoa(ret, _data.startFreq,2); } else // %
	if(strcmp(var,fc_START_FREQ_BOILER)==0)     {  _dtoa(ret, _data.startFreqBoiler,2); } else // %
	if(strcmp(var,fc_MIN_FREQ)==0)              {  _dtoa(ret, _data.minFreq,2); } else // %
	if(strcmp(var,fc_MIN_FREQ_COOL)==0)         {  _dtoa(ret, _data.minFreqCool,2); } else // %
	if(strcmp(var,fc_MIN_FREQ_BOILER)==0)       {  _dtoa(ret, _data.minFreqBoiler,2); } else // %
	if(strcmp(var,fc_MIN_FREQ_USER)==0)         {  _dtoa(ret, _data.minFreqUser,2); } else // %
	if(strcmp(var,fc_MAX_FREQ)==0)              {  _dtoa(ret, _data.maxFreq,2); } else // %
	if(strcmp(var,fc_MAX_FREQ_COOL)==0)         {  _dtoa(ret, _data.maxFreqCool,2); } else // %
	if(strcmp(var,fc_MAX_FREQ_BOILER)==0)       {  _dtoa(ret, _data.maxFreqBoiler,2); } else // %
	if(strcmp(var,fc_MAX_FREQ_USER)==0)         {  _dtoa(ret, _data.maxFreqUser,2); } else // %
	if(strcmp(var,fc_MAX_FREQ_GEN)==0)          {  _dtoa(ret, _data.maxFreqGen, 2); } else // %
	if(strcmp(var,fc_STEP_FREQ)==0)             {  _dtoa(ret, _data.stepFreq,2); } else // %
	if(strcmp(var,fc_STEP_FREQ_BOILER)==0)      {  _dtoa(ret, _data.stepFreqBoiler,2); } else // %
    if(strcmp(var,fc_DT_TEMP)==0)               {  _dtoa(ret, _data.dtTemp,2); } else // градусы
    if(strcmp(var,fc_DT_TEMP_BOILER)==0)        {  _dtoa(ret, _data.dtTempBoiler,2); } else // градусы
    if(strcmp(var,fc_MB_ERR)==0)        		{  _itoa(numErr, ret); } else
   	if(strcmp(var, fc_FC_TIME_READ)==0)   		{  _itoa(FC_TIME_READ, ret); } else
   	if(strcmp(var, fc_PidMaxStep)==0)   		{  _dtoa(ret, _data.PidMaxStep, 2); } else
    if(strcmp(var, fc_AdjustEEV_k)==0)			{  _dtoa(ret, _data.AdjustEEV_k, 2); } else
    if(strcmp(var, fc_ReturnOil_AdjustEEV_k)==0){  _dtoa(ret, _data.ReturnOil_AdjustEEV_k, 2); } else
   	if(strcmp(var, fc_MaxPower)==0)  			{  _itoa(_data.MaxPower, ret); } else
   	if(strcmp(var, fc_MaxPowerBoiler)==0)		{  _itoa(_data.MaxPowerBoiler, ret); } else
   	if(strcmp(var, fc_FC_MaxTemp)==0)  			{  _itoa(_data.FC_MaxTemp, ret); } else
   	if(strcmp(var, fc_FC_TargetTemp)==0) 		{  _itoa(_data.FC_TargetTemp, ret); } else
   	if(strcmp(var, fc_FC_C_COOLER_FAN_STR)==0)	{  strcat(ret, FC_C_COOLER_FAN_STR); } else

    strcat(ret,(char*)cInvalid);
}

// Установить параметр инвертора из строки
boolean devVaconFC::set_paramFC(char *var, float f)
{
	int16_t x = f;
    if(strcmp(var,fc_ON_OFF)==0)                { if(x == 0) stop_FC(); else start_FC(); return true; } else
    if(strcmp(var,fc_AUTO_RESET_FAULT)==0)      { _data.setup_flags = (_data.setup_flags & ~(1<<fAutoResetFault)) | ((x!=0)<<fAutoResetFault); return true;  } else
    if(strcmp(var,fc_LogWork)==0)               { _data.setup_flags = (_data.setup_flags & ~(1<<fLogWork)) | ((x!=0)<<fLogWork); return true;  } else
    if(strcmp(var,fc_fFC_RetOil)==0)            { _data.setup_flags = (_data.setup_flags & ~(1<<fFC_RetOil)) | ((x!=0)<<fFC_RetOil); return true;  } else
#ifdef FC_ANALOG_CONTROL
    if(strcmp(var,fc_LEVEL0)==0)                { if ((x>=0)&&(x<=4096)) { _data.level0=x; return true;} else return false;      } else
    if(strcmp(var,fc_LEVEL100)==0)              { if ((x>=0)&&(x<=4096)) { _data.level100=x; return true;} else return false;    } else
#endif
    if(strcmp(var,fc_BLOCK)==0)                 { SemaphoreGive(xModbusSemaphore); // отдать семафор ВСЕГДА  
                                                	if(x==0) { if(reset_FC()) note=(char*)noteFC_OK; }
                                                	else     { SETBIT1(flags,fErrFC); note=(char*)noteFC_NO; }
                                                	return true;
                                                } else  // только чтение
    if(strcmp(var,fc_UPTIME)==0)                { if((x>=1)&&(x<650)){_data.Uptime=x;return true; } else return false; } else   // хранение в сек
    if(strcmp(var,fc_ReturnOilPeriod)==0)       { _data.ReturnOilPeriod = (int16_t) x / (FC_TIME_READ/1000); return true; } else
    if(strcmp(var,fc_ReturnOilPerDivHz)==0)     { _data.ReturnOilPerDivHz = (int16_t) x / (FC_TIME_READ/1000); return true; } else
    if(strcmp(var,fc_ReturnOilTime)==0)         { _data.ReturnOilTime = (int16_t) x / (FC_TIME_READ/1000); return true; } else
    if(strcmp(var,fc_PID_STOP)==0)              { if((x>=0)&&(x<=100)){_data.PidStop=x;return true; } else return false;  } else
    if(strcmp(var,fc_MaxPower)==0)  		    { _data.MaxPower = x; return true; } else
    if(strcmp(var,fc_MaxPowerBoiler)==0)	    { _data.MaxPowerBoiler = x; return true; } else
    if(strcmp(var,fc_FC_MaxTemp)==0)  		    { _data.FC_MaxTemp = x; return true; } else
    if(strcmp(var,fc_FC_TargetTemp)==0)  	    { _data.FC_TargetTemp = x; return true; } else
   
	x = rd(f, 100);
    	if(strcmp(var,fc_DT_COMP_TEMP)==0)          { if(x>=0 && x<2500){_data.dtCompTemp=x;return true; } else return false; } else // градусы
		if(strcmp(var,fc_FC)==0)                    { if(x>=_data.minFreqUser && x<=_data.maxFreqUser){set_target(x,true, _data.minFreqUser, _data.maxFreqUser); return true; } } else
		if(strcmp(var,fc_DT_TEMP)==0)               { if(x>=0 && x<1000){_data.dtTemp=x;return true; } } else // градусы
		if(strcmp(var,fc_DT_TEMP_BOILER)==0)        { if(x>=0 && x<1000){_data.dtTempBoiler=x;return true; } } else // градусы
		if(strcmp(var,fc_START_FREQ)==0)            { if(x>=0 && x<=20000){_data.startFreq=x;return true; } } else // %
		if(strcmp(var,fc_START_FREQ_BOILER)==0)     { if(x>=0 && x<=20000){_data.startFreqBoiler=x;return true; } } else // %
		if(strcmp(var,fc_MIN_FREQ)==0)              { if(x>=0){_data.minFreq=x;return true; } } else // %
		if(strcmp(var,fc_MIN_FREQ_COOL)==0)         { if(x>=0){_data.minFreqCool=x;return true; } } else // %
		if(strcmp(var,fc_MIN_FREQ_BOILER)==0)       { if(x>=0){_data.minFreqBoiler=x;return true; } } else // %
		if(strcmp(var,fc_MIN_FREQ_USER)==0)         { if(x>=0){_data.minFreqUser=x;return true; } } else // %
		if(strcmp(var,fc_MAX_FREQ)==0)              { if(x>=0){_data.maxFreq=x;return true; } } else // %
		if(strcmp(var,fc_MAX_FREQ_COOL)==0)         { if(x>=0){_data.maxFreqCool=x;return true; } } else // %
		if(strcmp(var,fc_MAX_FREQ_BOILER)==0)       { if(x>=0){_data.maxFreqBoiler=x;return true; } } else // %
		if(strcmp(var,fc_MAX_FREQ_USER)==0)         { if(x>=0){_data.maxFreqUser=x;return true; } } else // %
		if(strcmp(var,fc_MAX_FREQ_GEN)==0)          { if(x>=0){_data.maxFreqGen=x;return true; } } else // %
		if(strcmp(var,fc_PID_FREQ_STEP)==0)         { if(x>=0 && x<=_data.PidMaxStep){ _data.PidFreqStep=x; return true; } } else // %
		if(strcmp(var,fc_STEP_FREQ)==0)             { if(x>=0 && x<10000){_data.stepFreq=x;return true; } } else // %
		if(strcmp(var,fc_PidMaxStep)==0)            { if(x>=0 && x<10000){_data.PidMaxStep=x; return true; } } else // %
		if(strcmp(var,fc_ReturnOilMinFreq)==0)      { _data.ReturnOilMinFreq = x; return true; } else
		if(strcmp(var,fc_ReturnOilFreq)==0)         { _data.ReturnOilFreq = x; return true; } else
		if(strcmp(var,fc_AdjustEEV_k)==0)           { _data.AdjustEEV_k = x; return true; } else
		if(strcmp(var,fc_ReturnOil_AdjustEEV_k)==0) { _data.ReturnOil_AdjustEEV_k = x; return true; } else
#ifdef DEFROST
		if(strcmp(var,fc_defrostFreq)==0) 			{ _data.defrostFreq = x; return true; } else
#endif
		if(strcmp(var,fc_STEP_FREQ_BOILER)==0)      { if(x>=0 && x<10000){_data.stepFreqBoiler=x;return true; } } // %
 
    return false;
}

// Вывести в buffer строковый статус.
void devVaconFC::get_infoFC_status(char *buffer, uint16_t st)
{
#ifdef FC_VLT
	if((st & FC_S_CONTROL_RDY) == 0) strcat(buffer, FC_S_CONTROL_RDY_str0);
	if(st & FC_S_RDY) 		strcat(buffer, FC_S_RDY_str);
	if(st & FC_S_START) 	strcat(buffer, FC_S_START_str);
	if(st & FC_S_RUN) 	{
		strcat(buffer, FC_S_RUN_str);
		if((st & FC_S_AREF)==0)	strcat(buffer, FC_S_AREF_str0);
	}
	if(st & FC_S_TRIP) 		strcat(buffer, FC_S_TRIP_str);
	if(st & FC_S_TRIPLOCK) 	strcat(buffer, FC_S_TRIPLOCK_str);
	if(!(st & FC_S_BUS)) 	strcat(buffer, FC_S_BUS_str0);
	if(st & FC_S_FREQ_LIM) 	strcat(buffer, FC_S_FREQ_LIM_str);
	if(st & FC_S_OVERTEMP) 	strcat(buffer, FC_S_OVERTEMP_str);
	if(st & FC_S_VOLTAGE) 	strcat(buffer, FC_S_VOLTAGE_str);
	if(st & FC_S_CURRENT) 	strcat(buffer, FC_S_CURRENT_str);
	if(st & FC_S_HEAT) 		strcat(buffer, FC_S_HEAT_str);
	if(st & FC_S_FLT) 		strcat(buffer, FC_S_FLT_str);
	if(st & FC_S_W) 		strcat(buffer, FC_S_W_str);
#else //FC_VLT
	if(st & FC_S_RDY) 	strcat(buffer, FC_S_RDY_str);
	if(st & FC_S_RUN) 	{
		strcat(buffer, FC_S_RUN_str);
		if((st & FC_S_AREF)==0)	strcat(buffer, FC_S_AREF_str0);
	}
	if(st & FC_S_DIR) 	strcat(buffer, FC_S_DIR_str);
	if(st & FC_S_FLT) 	strcat(buffer, FC_S_FLT_str);
	if(st & FC_S_W) 	strcat(buffer, FC_S_W_str);
	if(st & FC_S_Z) 	strcat(buffer, FC_S_Z_str);
#endif
	int i = m_strlen(buffer);
	if(i) *(buffer + i - 1) = '\0'; // skip last ','
}

// Получить информацию о частотнике
void devVaconFC::get_infoFC(char* buf)
{
	buf += m_strlen(buf);
	if(testMode == NORMAL || testMode == HARD_TEST) {
#ifndef FC_ANALOG_CONTROL // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
		if(state == ERR_LINK_FC) {   // Нет связи
			strcat(buf, "|Данные не доступны - нет связи|;");
		} else {
#ifdef FC_VLT
			strcat(buf, "16-00|Состояние инвертора: ");
			get_infoFC_status(buf + m_strlen(buf), state);
			buf += m_snprintf(buf += m_strlen(buf), 256, "|0x%X;", state);
			if(err == OK) {
				buf += m_snprintf(buf, 256, "16-01|Фактическая скорость|%d%%;16-10|Выходная мощность (кВт)|%d;", read_0x03_16(FC_SPEED), get_power());
				buf += m_snprintf(buf, 256, "16-17|Обороты (об/м)|%d;", (int16_t)read_0x03_16(FC_RPM));
				buf += m_snprintf(buf, 256, "16-22|Крутящий момент|%d%%;", (int16_t)read_0x03_16(FC_TORQUE));
				buf += m_snprintf(buf, 256, "16-12|Выходное напряжение (В)|%.1d;", (int16_t)read_0x03_16(FC_VOLTAGE));
				buf += m_snprintf(buf, 256, "16-30|Напряжение шины постоянного тока (В)|%d;", (int16_t)read_0x03_16(FC_VOLTAGE_DC));
				buf += m_snprintf(buf, 256, "16-34|Температура радиатора (°С)|%d;", read_tempFC());
				buf += m_snprintf(buf, 256, "15-00|Время включения инвертора (часов)|%d;", read_0x03_32(FC_POWER_HOURS));
				buf += m_snprintf(buf, 256, "15-01|Время работы компрессора (часов)|%d;", read_0x03_32(FC_RUN_HOURS));
				buf += m_snprintf(buf, 256, "15-02|Итого выход кВтч|%d;", read_0x03_16(FC_CNT_POWER));

				uint32_t reg = read_0x03_32(FC_ERROR);
				if(err == OK) {
					strcat(buf, "16-90|Ошибки (");
					get_fault_str(buf, FC_Alarm_str, reg);
					buf += m_snprintf(buf += strlen(buf), 256, ")|0x%X;", reg);
					strcat(buf, "16-92|Предупреждения (");
					reg = read_0x03_32(FC_WARNING);
					get_fault_str(buf, FC_Warning_str, reg);
					buf += m_snprintf(buf += strlen(buf), 256, ")|0x%X;", reg);
					strcat(buf, "16-94|Статус (");
					reg = read_0x03_32(FC_EX_STATUS);
					get_fault_str(buf, FC_ExtStatus_str, reg);
					buf += m_snprintf(buf += strlen(buf), 256, ")|0x%X;", reg);
#ifdef FC_EX_STATUS2
					strcat(buf, "16-94|Статус2 (");
					reg = read_0x03_32(FC_EX_STATUS2);
					get_fault_str(buf, FC_ExtStatus2_str, reg);
					buf += m_snprintf(buf += strlen(buf), 256, ")|0x%X;", reg);
#endif
				} else {
					m_snprintf(buf, 256, "-|Ошибка Modbus|%d;", err);
				}
			}
#else //FC_VLT
			uint32_t i;
			strcat(buf, "2101|Состояние инвертора: ");
			get_infoFC_status(buf + m_strlen(buf), state);
			buf += m_snprintf(buf += m_strlen(buf), 256, "|0x%X;", state);
			if(err == OK) {
				buf += m_snprintf(buf, 256, "2103|Фактическая скорость|%.2d%%;2108 (V1.1)|Выходная мощность: %.1d%% (кВт)|%.3d;", read_0x03_16(FC_SPEED), power, get_power());
				buf += m_snprintf(buf, 256, "2105 (V1.3)|Обороты (об/м)|%d;", (int16_t)read_0x03_16(FC_RPM));
				buf += m_snprintf(buf, 256, "2107 (V1.5)|Крутящий момент|%.1d%%;", (int16_t)read_0x03_16(FC_TORQUE));
				i = read_0x03_32(FC_VOLTAGE); // +FC_VOLTATE_DC (low word)
				buf += m_snprintf(buf, 256, "2109 (V1.7)|Выходное напряжение (В)|%.1d;2110 (V1.8)|Напряжение шины постоянного тока (В)|%d;", i >> 16, i & 0xFFFF);
				buf += m_snprintf(buf, 256, "0008 (V1.9)|Температура радиатора (°С)|%d;", read_tempFC());
				i = read_0x03_32(FC_POWER_DAYS); // +FC_POWER_HOURS (low word)
				buf += m_snprintf(buf, 256, "0828 (V3.3)|Время включения инвертора (дней:часов)|%d:%d;", i >> 16, i & 0xFFFF);
				i = read_0x03_32(FC_RUN_DAYS); // +FC_RUN_HOURS (low word)
				buf += m_snprintf(buf, 256, "0840 (V3.5)|Время работы компрессора (дней:часов)|%d:%d;", i >> 16, i & 0xFFFF);
				buf += m_snprintf(buf, 256, "0842 (V3.6)|Счетчик аварийных отключений|%d;", read_0x03_16(FC_NUM_FAULTS));

				i = read_0x03_16(FC_ERROR);
				if(err == OK) {
					m_snprintf(buf, 256, "2111|Активная ошибка|%s (%d);", get_fault_str(i), i);
				} else {
					m_snprintf(buf, 256, "-|Ошибка Modbus|%d;", err);
				}
			}
#endif
		}
#endif
	} else {
		m_snprintf(buf, 256, "|Режим тестирования!|;");
	}
}

// Сброс сбоя инвертора
boolean devVaconFC::reset_errorFC()
{
#ifndef FC_ANALOG_CONTROL // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
	if((state & FC_S_FLT)) { // сброс отказа
		if((err = write_0x06_16(FC_CONTROL, 0)) == OK) {
			_delay(100);
			err = write_0x06_16(FC_CONTROL, FC_C_RST);
			_delay(100);
		}
		if(err) journal.jprintf("%s: Error reset fault!\n", name);
	}
    read_stateFC();
#endif
    return err == OK;
}

// Сброс состояния связи инвертора через модбас
boolean devVaconFC::reset_FC()
{
	number_err = 0;
#ifndef FC_ANALOG_CONTROL
    CheckLinkStatus();
    if(!err && state == ERR_LINK_FC) err = ERR_RESET_FC;
    else
#endif
    {
    	if(testMode == NORMAL || testMode == HARD_TEST) reset_errorFC();
    }
    return err == OK;
}

// Текущее состояние инвертора
inline int16_t devVaconFC::read_stateFC()
{
#ifndef FC_ANALOG_CONTROL // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
    return state = read_0x03_16(FC_STATUS);
#else
    return 0;
#endif
}

// Tемпература радиатора, C
int16_t devVaconFC::read_tempFC()
{
	return FC_Temp;
//#ifndef FC_ANALOG_CONTROL // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
//    return read_0x03_16(FC_TEMP);
//#else
//    return 0;
//#endif
}

// Функции общения по модбас инвертора Чтение регистров
#ifndef FC_ANALOG_CONTROL // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
// Функция 0х03 прочитать 2 байта, возвращает значение, ошибка обновляется
// Реализовано FC_NUM_READ попыток чтения/записи в инвертор
int16_t devVaconFC::read_0x03_16(uint16_t cmd)
{
    uint8_t i;
    int16_t result = 0;
    if(!get_present()) return 0; // выходим если нет инвертора
    for (i = 0; i < FC_NUM_READ; i++) // делаем FC_NUM_READ попыток чтения Чтение состояния инвертора, при ошибке генерация общей ошибки ТН и останов
    {
        err = Modbus.readHoldingRegisters16(FC_MODBUS_ADR, cmd - 1, (uint16_t *)&result); // Послать запрос, Нумерация регистров с НУЛЯ!!!!
        if(err == OK) {
        	check_blockFC();
        	break; // Прочитали удачно
        }
#ifdef SPOWER
        HP.sInput[SPOWER].Read(true);
        if(HP.sInput[SPOWER].is_alarm()) {
        	err = ERR_NO_POWER_WHILE_WORK;
        	break;
        }
#endif
        if(GETBIT(HP.Option.flags, fBackupPower)) {
        	err = ERR_NO_POWER_WHILE_WORK;
        	break;
        }
        if(state == ERR_LINK_FC) {
        	result = ERR_LINK_FC;
        	break;
        }
        numErr++; // число ошибок чтение по модбасу
        if(number_err == FC_NUM_READ-1 || GETBIT(HP.Option.flags, fModbusLogErrors)) journal.jprintf_time(cErrorRS485, name, __FUNCTION__, cmd, err); 	// Сообщение об ошибке
        if(check_blockFC()) break; // проверить необходимость блокировки
        _delay(FC_DELAY_REPEAT);
    }
    return result;
}

// Функция 0х03 прочитать 2 байта, возвращает значение, ошибка обновляется
// Реализовано FC_NUM_READ попыток чтения/записи в инвертор
uint32_t devVaconFC::read_0x03_32(uint16_t cmd)
{
    uint8_t i;
    uint32_t result = 0;
    if(!get_present() || state == ERR_LINK_FC) return 0; // выходим если нет инвертора или он заблокирован по ошибке
    for (i = 0; i < FC_NUM_READ; i++) // делаем FC_NUM_READ попыток чтения Чтение состояния инвертора, при ошибке генерация общей ошибки ТН и останов
    {
        err = Modbus.readHoldingRegisters32(FC_MODBUS_ADR, cmd - 1, (uint32_t *)&result); // Послать запрос, Нумерация регистров с НУЛЯ!!!!
        if(err == OK) {
        	check_blockFC();
        	break; // Прочитали удачно
        }
#ifdef SPOWER
        HP.sInput[SPOWER].Read(true);
        if(HP.sInput[SPOWER].is_alarm()) {
        	err = ERR_NO_POWER_WHILE_WORK;
        	break;
        }
#endif
        if(GETBIT(HP.Option.flags, fBackupPower)) {
        	err = ERR_NO_POWER_WHILE_WORK;
        	break;
        }
        if(state == ERR_LINK_FC) {
        	result = ERR_LINK_FC;
        	break;
        }
        numErr++; // число ошибок чтение по модбасу
        if(number_err == FC_NUM_READ-1 || GETBIT(HP.Option.flags, fModbusLogErrors)) journal.jprintf_time(cErrorRS485, name, __FUNCTION__, cmd, err); 	// Сообщение об ошибке
        if(check_blockFC()) break; // проверить необходимость блокировки
        _delay(FC_DELAY_REPEAT);
    }
    return result;
}

// Запись данных (2 байта) в регистр cmd возвращает код ошибки
// Реализовано FC_NUM_READ попыток чтения/записи в инвертор
int8_t devVaconFC::write_0x06_16(uint16_t cmd, uint16_t data)
{
    uint8_t i;
    if(!get_present() || state == ERR_LINK_FC) return err; // выходим если нет инвертора или он заблокирован по ошибке
    for (i = 0; i < FC_NUM_READ; i++) // делаем FC_NUM_READ попыток записи
    {
        err = Modbus.writeHoldingRegisters16(FC_MODBUS_ADR, cmd - 1, data); // послать запрос, Нумерация регистров с НУЛЯ!!!!
        if(err == OK) {
        	check_blockFC();
        	break; // Записали удачно
        }
#ifdef SPOWER
        HP.sInput[SPOWER].Read(true);
        if(HP.sInput[SPOWER].is_alarm()) {
        	err = ERR_NO_POWER_WHILE_WORK;
        	break;
        }
#endif
        if(GETBIT(HP.Option.flags, fBackupPower)) {
        	err = ERR_NO_POWER_WHILE_WORK;
        	break;
        }
        if(state == ERR_LINK_FC) {
        	break;
        }
        numErr++; // число ошибок чтение по модбасу
        if(number_err == FC_NUM_READ-1 || GETBIT(HP.Option.flags, fModbusLogErrors)) journal.jprintf_time(cErrorRS485, name, __FUNCTION__, cmd, err); 	// Сообщение об ошибке
        if(check_blockFC()) break; // проверить необходимость блокировки
        _delay(FC_DELAY_REPEAT);
    }
    return err;
}
#endif // FC_ANALOG_CONTROL    // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ

#ifdef FC_VLT
// Возвращает строкове представление ошибки или предупреждения
void devVaconFC::get_fault_str(char *ret, const char *arr[], uint32_t code)
{
	uint8_t i = 0;
	while(code && arr[i] != NULL) {
		if(code & 1) {
			if(ret == NULL) {
				journal.jprintf("%s,", arr[i]);
			} else {
				strcat(ret, arr[i]);
				strcat(ret, ",");
			}
		}
		i++;
		code >>= 1;
	}
}
#else //FC_VLT
// Возвращает текст ошибки по FT коду
const char* devVaconFC::get_fault_str(uint8_t fault)
{
    uint8_t i = 0;
    for (; i < sizeof(FC_Faults_code); i++) {
        if(FC_Faults_code[i] == fault) break;
    }
    return FC_Faults_str[i];
}
#endif
