/*
 * Copyright (c) 2016-2021 by Vadim Kulakov vad7@yahoo.com, vad711
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
#include "Constant.h"

#ifdef LCD2004

#include "LCD2004.h"

//////////////////////////////////////////////////////////////////////////
uint32_t LCD_setup = 0; // 0x8000MMII: 8 - Setup active, MМ - Menu item (0..<max LCD_SetupMenuItems-1>) , II - Selecting item (0...)

// Задача Пользовательский интерфейс (HP.xHandleKeysLCD) "KeysLCD"
void vKeysLCD( void * )
{
	pinMode(PIN_KEY_UP, INPUT_PULLUP);
	pinMode(PIN_KEY_DOWN, INPUT_PULLUP);
	pinMode(PIN_KEY_OK, INPUT_PULLUP);
	vTaskDelay(1000);
	static uint32_t DisplayTick = xTaskGetTickCount();
	static char buffer[ALIGN(LCD_COLS * 2)];
	static uint32_t setup_timeout = 0;
	for(;;)
	{
		if(LCD_setup & LCD_SetupFlag) if(--setup_timeout == 0) goto xSetupExit;
		if(!digitalReadDirect(PIN_KEY_OK)) {
			vTaskDelay(KEY_DEBOUNCE_TIME);
			if(LCD_setup & LCD_SetupFlag) {
				{
					uint32_t t = 2000 / KEY_DEBOUNCE_TIME;
					while(!digitalReadDirect(PIN_KEY_OK)) {
						if(--t == 0) {
							lcd.clear();
							while(!digitalReadDirect(PIN_KEY_OK)) vTaskDelay(KEY_DEBOUNCE_TIME);
							break;
						}
						vTaskDelay(KEY_DEBOUNCE_TIME);
					}
					if(t == 0) goto xSetupExit;
				}
				if((LCD_setup & 0xFFFF) == 0) { // menu item 1 selected - Exit
xSetupExit:
					lcd.command(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
					LCD_setup = 0;
					setup_timeout = 0;
					vTaskDelay(KEY_DEBOUNCE_TIME * 10);
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Relays) { // inside menu item selected - Relay
					HP.dRelay[LCD_setup & 0xFF].set_Relay(HP.dRelay[LCD_setup & 0xFF].get_Relay() ? fR_StatusAllOff : fR_StatusManual);
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Network) { // inside menu item selected
					HP.safeNetwork ^= 1;
					HP.sendCommand(pNETWORK);
					goto xSetupExit;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_UpdateFW) { // inside menu item selected
					if(HP.dEEV.EEV != -1) HP.dEEV.set_EEV(HP.dEEV.get_maxEEV());
					int i;
					if((i = HP.save_motoHour()) == OK)
						if((i = Stats.SaveStats(1)) == OK)
							i = Stats.SaveHistory(1);
					lcd.setCursor(0, 1);
					if(i == OK) {
						lcd.print("OK");
					} else {
						lcd.print("Error ");
						lcd.print(i);
					}
					vTaskDelay(3000);
					goto xSetupExit;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_OnOff) { // inside menu item selected
					if((HP.get_State() != pSTARTING_HP) || (HP.get_State() != pSTOPING_HP)) {
						if(LCD_setup & 0xFF) HP.sendCommand(pSTART); else HP.sendCommand(pSTOP);
					}
					goto xSetupExit;
				} else if((LCD_setup & 0xFF00) == 0) {	// select menu item
					LCD_setup = (LCD_setup << 8) | LCD_SetupFlag;
					if((LCD_setup & 0xFF00) == LCD_SetupMenu_Network) {
						lcd.command(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
					} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_UpdateFW) {
						if(HP.is_compressor_on()) LCD_setup >>= 8;
					} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Sensors) {
						lcd.command(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
					} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_OnOff) {
						if(HP.IsWorkingNow() && HP.get_State() != pSTOPING_HP) LCD_setup |= 1; else LCD_setup &= ~1;
						lcd.command(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
					}
				} else { // inside menu item selected
					goto xSetupExit;
				}
				DisplayTick = ~DisplayTick;
				setup_timeout = DISPLAY_SETUP_TIMEOUT / KEY_CHECK_PERIOD;
			} else if(HP.get_errcode() && !Error_Beep_confirmed) Error_Beep_confirmed = true; // Supress beeping
			else if((LCD_setup & 0xFFFF) == 0) { // Enter Setup
				LCD_setup = LCD_SetupFlag;
				lcd.command(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSORON | LCD_BLINKON);
				DisplayTick = ~DisplayTick;
				setup_timeout = DISPLAY_SETUP_TIMEOUT / KEY_CHECK_PERIOD;
			} else {
				LCD_setup ^= 0x0100;
				DisplayTick = ~DisplayTick;
			}
			while(!digitalReadDirect(PIN_KEY_OK)) vTaskDelay(KEY_DEBOUNCE_TIME);
			//journal.jprintfopt("[OK]\n");
		} else if(!digitalReadDirect(PIN_KEY_UP)) {
			vTaskDelay(KEY_DEBOUNCE_TIME);
			while(!digitalReadDirect(PIN_KEY_UP)) vTaskDelay(KEY_DEBOUNCE_TIME);
			if(LCD_setup & LCD_SetupFlag) {
				if((LCD_setup & 0xFF00) == 0) { // select menu item
					if((LCD_setup & 0xFF) < LCD_SetupMenuItems-1) LCD_setup++; else LCD_setup &= ~0xFF;
					DisplayTick = ~DisplayTick;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_UpdateFW) {
					goto xSetupExit;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Relays) {
					LCD_setup++;
					if((LCD_setup & 0xFF) >= (RNUMBER > LCD_SetupMenu_Relays_Max ? LCD_SetupMenu_Relays_Max : RNUMBER)) {
						LCD_setup &= ~0xFF;
					}
					DisplayTick = ~DisplayTick;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_OnOff) {
					LCD_setup ^= 1;
					DisplayTick = ~DisplayTick;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Network) {
					goto xSetupExit;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Sensors) DisplayTick = ~DisplayTick;
				setup_timeout = DISPLAY_SETUP_TIMEOUT / KEY_CHECK_PERIOD;
			} else if(LCD_setup & 0xFF00) {
				switch (LCD_setup & 0xFF) {
				case 1: // House
					HP.setTargetTemp(10);
					break;
				case 2: // Boiler
					HP.setTempTargetBoiler(100);
					break;
				}
				DisplayTick = ~DisplayTick;
			} else {
				if((LCD_setup & 0xFF) > 0) {
					LCD_setup--;
					DisplayTick = ~DisplayTick;
				}
			}
			//journal.jprintfopt("[UP]\n");
		} else if(!digitalReadDirect(PIN_KEY_DOWN)) {
			vTaskDelay(KEY_DEBOUNCE_TIME);
			while(!digitalReadDirect(PIN_KEY_DOWN)) vTaskDelay(KEY_DEBOUNCE_TIME);
			if(LCD_setup & LCD_SetupFlag) {
				if((LCD_setup & 0xFF00) == 0) {
					if(LCD_setup & 0xFF) LCD_setup--; else LCD_setup = (LCD_setup & ~0xFF) | (LCD_SetupMenuItems-1);
					DisplayTick = ~DisplayTick;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_UpdateFW) {
					goto xSetupExit;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Relays) {
					if((LCD_setup & 0xFF) != 0) LCD_setup--; else LCD_setup |= RNUMBER > LCD_SetupMenu_Relays_Max ? LCD_SetupMenu_Relays_Max-1 : RNUMBER-1;
					DisplayTick = ~DisplayTick;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_OnOff) {
					LCD_setup ^= 1;
					DisplayTick = ~DisplayTick;
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Network) {
					goto xSetupExit;
				}
				setup_timeout = DISPLAY_SETUP_TIMEOUT / KEY_CHECK_PERIOD;
			} else if(LCD_setup & 0xFF00) {
				switch (LCD_setup & 0xFF) {
				case 1: // House
					HP.setTargetTemp(-10);
					break;
				case 2: // Boiler
					HP.setTempTargetBoiler(-100);
					break;
				}
				DisplayTick = ~DisplayTick;
			} else {
				if((LCD_setup & 0xFF) < LCD_MainScreenMaxItem) {
					LCD_setup++;
					DisplayTick = ~DisplayTick;
				}
			}
			//journal.jprintfopt("[DWN]\n");
		}
		if(xTaskGetTickCount() - DisplayTick > DISPLAY_UPDATE) { // Update display
			char *buf = buffer;
			if(LCD_setup & LCD_SetupFlag) {
				if((LCD_setup & 0xFF00) == LCD_SetupMenu_Relays) { // Relays
					lcd.clear();
					for(uint8_t i = 0; i < (RNUMBER > LCD_SetupMenu_Relays_Max ? LCD_SetupMenu_Relays_Max : RNUMBER) ; i++) {
						lcd.setCursor(10 * (i % 2), i / 2);
						lcd.print(HP.dRelay[i].get_Relay() ? '*' : ' ');
						lcd.print(HP.dRelay[i].get_name());
					}
					lcd.setCursor(10 * ((LCD_setup & 0xFF) % 2), (LCD_setup & 0xFF) / 2);
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Sensors) { // Sensors
					lcd.clear();
					for(uint8_t i = 0; i < (INUMBER > 8 ? 8 : INUMBER) ; i++) {
						lcd.setCursor(10 * (i % 2), i / 2);
						lcd.print(HP.sInput[i].get_Input() ? '*' : ' ');
						lcd.print(HP.sInput[i].get_name());
					}
					lcd.setCursor(10 * ((LCD_setup & 0xFF) % 2), (LCD_setup & 0xFF) / 2);
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_OnOff) {
					lcd.clear();
					lcd.print(LCD_Str_HP);
					lcd.print(" - ");
					lcd.print(LCD_setup & 0xFF ? LCD_Str_On : LCD_Str_Off);
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_Network) {
					lcd.clear();
					lcd.print(LCD_Str_SafeNework);
					if(HP.safeNetwork) {
						lcd.print(' ');
						lcd.print(LCD_Str_Off);
					}
				} else if((LCD_setup & 0xFF00) == LCD_SetupMenu_UpdateFW) {
					lcd.clear();
					lcd.print(LCD_Str_PrepareUpdate);
				} else {
					lcd.clear();
					lcd.print((LCD_setup & 0xFF) + 1);
					lcd.print(". ");
					lcd.print(LCD_SetupMenu[LCD_setup & 0xFF]);
					lcd.setCursor(0, 1);
					lcd.print(LCD_Str_SetupInfo);
					lcd.setCursor(0, 3);
					NowDateToStr(buffer);
					buffer[10] = ' ';
					NowTimeToStr(buffer + 11);
					lcd.print(buffer);
					lcd.setCursor(0, 0);
				}
			} else {
				// Display:
				// 12345678901234567890
				// >[Hp2]: Heating
				//  House: 21.2→24.0°
				//  Boiler: 54.2→60.0°
				//  Freq: 50.2
				lcd.setCursor(0, 0);
				if(HP.get_errcode()) {
					buf += m_snprintf(buf, sizeof(buffer), " [E%d]: %s", HP.get_errcode(), HP.StateToStrEN());
				} else buf += m_snprintf(buf, sizeof(buffer), " [%s]: %s", codeRet[HP.get_ret()], HP.StateToStrEN());
				buffer_space_padding(buf, LCD_COLS - (buf - buffer));
				lcd.print(buffer);
				lcd.setCursor(0, 1);
				buf = buffer + m_snprintf(buffer, sizeof(buffer), " %s: %.1d\x7E", LCD_Str_House, HP.sTemp[TIN].get_Temp()/10);
				if(HP.get_modeHouse() == pOFF) strcat(buf, "-"); else HP.getTargetTempStr(buf);
				strcat(buf, "\xDF");
				buf += m_strlen(buf);
				buffer_space_padding(buf, LCD_COLS - (buf - buffer));
				lcd.print(buffer);
				lcd.setCursor(0, 2);
				buf = buffer + m_snprintf(buffer, sizeof(buffer), " %s: %.1d\x7E%.1d\xDF", LCD_Str_Boiler, HP.sTemp[TBOILER].get_Temp()/10, HP.Prof.Boiler.TempTarget/10);
				buffer_space_padding(buf, LCD_COLS - (buf - buffer));
				lcd.print(buffer);
				lcd.setCursor(0, 3);
				buf = buffer + m_snprintf(buffer, sizeof(buffer), " %s: ", LCD_Str_Freq);
				if(HP.dFC.get_present()) {
					if(HP.is_compressor_on()) _dtoa(buf, HP.dFC.get_frequency(), 2); else strcat(buf, LCD_Str_Off);
				} else strcat(buf, "-");
				buf += m_strlen(buf);
				buffer_space_padding(buf, LCD_COLS - (buf - buffer));
				lcd.print(buffer);
				lcd.setCursor(0, LCD_setup & 0x3);
				lcd.print(LCD_setup & 0xFF00 ? '*' : '>');
			}

			DisplayTick = xTaskGetTickCount();
		}
		vTaskDelay(KEY_CHECK_PERIOD);
	}
	vTaskSuspend(NULL);
}



#endif
