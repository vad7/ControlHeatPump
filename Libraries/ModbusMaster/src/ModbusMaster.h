/*
Доработка библиотеки для "Народного контроллера теплового насоса"
pav2000  firstlast2007@gmail.com
Добавлены изменения для работы с инвертором Omron MX2
- поддерживается функция проверки связи (код функции 0х08)
для проверки функции используйте   LinkTestOmronMX2Only(code)
где code - проверочный код (любое число uint16_t),
в случае успеха первый элемент буфера будет содержать этот код
- сделана обработка ошибок инвертора (в коде функции добавляется 0х80)
при этом возвращается состяние ku8MBErrorOmronMX2,
первый элемент буфера при этом содержит код ошибки
*
* Доработки - vad7@yahoo.com
*/

/**
@file
Arduino library for communicating with Modbus slaves over RS232/485 (via RTU protocol).
*/
/*
  ModbusMaster.h - Arduino library for communicating with Modbus slaves
  over RS232/485 (via RTU protocol).

  Library:: ModbusMaster

  Copyright:: 2009-2016 Doc Walker

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/
  
#ifndef ModbusMaster_h
#define ModbusMaster_h

//#define MODBUSMASTER_DEBUG             // Отладка - посылка приема и передачи в Serial
#define MODBUS_FREERTOS                  // Настроить либу на многозадачность

#include "Arduino.h"                     // include types & constants of Wiring core API
#include "util/crc16.h"                  // functions to calculate Modbus Application Data Unit CRC
#include "util/word.h"                   // functions to manipulate words

#ifdef MODBUS_FREERTOS
#include "FreeRTOS_ARM.h"                // поддержка многозадачности
#endif

// Коды функций Modbus
// Modbus function codes for bit access
#define ku8MBReadCoils                  0x01 ///< Modbus function 0x01 Read Coils
#define ku8MBReadDiscreteInputs         0x02 ///< Modbus function 0x02 Read Discrete Inputs
#define ku8MBWriteSingleCoil            0x05 ///< Modbus function 0x05 Write Single Coil
#define ku8MBWriteMultipleCoils         0x0F ///< Modbus function 0x0F Write Multiple Coils

// Modbus function codes for 16 bit access
#define ku8MBReadHoldingRegisters       0x03 ///< Modbus function 0x03 Read Holding Registers
#define ku8MBReadInputRegisters         0x04 ///< Modbus function 0x04 Read Input Registers
#define ku8MBWriteSingleRegister        0x06 ///< Modbus function 0x06 Write Single Register
#define ku8MBWriteMultipleRegisters     0x10 ///< Modbus function 0x10 Write Multiple Registers
#define ku8MBMaskWriteRegister          0x16 ///< Modbus function 0x16 Mask Write Register
#define ku8MBReadWriteMultipleRegisters 0x17 ///< Modbus function 0x17 Read Write Multiple Registers
#define ku8MBLinkTestOmronMX2Only       0x08 ///< Modbus function 0x08 Тест связи с инвертром Omron MX2 функция только для него
// 8 bit
#define ku8MBCustomRequest				0x09 // Custom request, prepare send buffer - send(uint8_t) //vad7

/**
Arduino class library for communicating with Modbus slaves over 
RS232/485 (via RTU protocol).
*/
class ModbusMaster
{
  public:
    ModbusMaster();
   
    void begin(uint8_t, Stream &serial);
    inline uint8_t set_slave(uint8_t slave) {return _u8MBSlave=slave;} // Установить slave
    void idle(void (*)());
    void preTransmission(void (*)());
    void postTransmission(void (*)());

    // Modbus exception codes
    /**
    Modbus protocol illegal function exception.
    
    The function code received in the query is not an allowable action for
    the server (or slave). This may be because the function code is only
    applicable to newer devices, and was not implemented in the unit
    selected. It could also indicate that the server (or slave) is in the
    wrong state to process a request of this type, for example because it is
    unconfigured and is being asked to return register values.
    
    @ingroup constant
    */
    static const uint8_t ku8MBIllegalFunction            = 0x01;

    /**
    Modbus protocol illegal data address exception.
    
    The data address received in the query is not an allowable address for 
    the server (or slave). More specifically, the combination of reference 
    number and transfer length is invalid. For a controller with 100 
    registers, the ADU addresses the first register as 0, and the last one 
    as 99. If a request is submitted with a starting register address of 96 
    and a quantity of registers of 4, then this request will successfully 
    operate (address-wise at least) on registers 96, 97, 98, 99. If a 
    request is submitted with a starting register address of 96 and a 
    quantity of registers of 5, then this request will fail with Exception 
    Code 0x02 "Illegal Data Address" since it attempts to operate on 
    registers 96, 97, 98, 99 and 100, and there is no register with address 
    100. 
    
    @ingroup constant
    */
    static const uint8_t ku8MBIllegalDataAddress         = 0x02;
    
    /**
    Modbus protocol illegal data value exception.
    
    A value contained in the query data field is not an allowable value for 
    server (or slave). This indicates a fault in the structure of the 
    remainder of a complex request, such as that the implied length is 
    incorrect. It specifically does NOT mean that a data item submitted for 
    storage in a register has a value outside the expectation of the 
    application program, since the MODBUS protocol is unaware of the 
    significance of any particular value of any particular register.
    
    @ingroup constant
    */
    static const uint8_t ku8MBIllegalDataValue           = 0x03;
    
    /**
    Modbus protocol slave device failure exception.
    
    An unrecoverable error occurred while the server (or slave) was
    attempting to perform the requested action.
    
    @ingroup constant
    */
    static const uint8_t ku8MBSlaveDeviceFailure         = 0x04;

    // Class-defined success/exception codes
    /**
    ModbusMaster success.
    
    Modbus transaction was successful; the following checks were valid:
      - slave ID
      - function code
      - response code
      - data
      - CRC
      
    @ingroup constant
    */
    static const uint8_t ku8MBSuccess                    = 0x00;
    
    /**
    ModbusMaster invalid response slave ID exception.
    
    The slave ID in the response does not match that of the request.
    
    @ingroup constant
    */
    static const uint8_t ku8MBInvalidSlaveID             = 0xE0;
    
    /**
    ModbusMaster invalid response function exception.
    
    The function code in the response does not match that of the request.
    
    @ingroup constant
    */
    static const uint8_t ku8MBInvalidFunction            = 0xE1;
    
    /**
    ModbusMaster response timed out exception.
    
    The entire response was not received within the timeout period, 
    ModbusMaster::ku8MBResponseTimeout. 
    
    @ingroup constant
    */
    static const uint8_t ku8MBResponseTimedOut           = 0xE2;
    
    /**
    ModbusMaster invalid response CRC exception.
    
    The CRC in the response does not match the one calculated.
    
    @ingroup constant
    */
    static const uint8_t ku8MBInvalidCRC                 = 0xE3;


    // Обнаружена спицефическая ошибка Omron MX2, счетчика PZEM-004T
    // В случае обнаружения ошибки в запросе (кроме ошибки связи)
    // преобразователь частоты возвращает в ответе сообщение об исключении и не выполняет никаких действий.
    // Ошибку можно найти по коду функции в ответе. Код функции для ответа с
    // сообщением об ошибке определяется как сумма кода функции запроса и числа 80h.
    // Для кодирования используется  ku8MBErrorOmronMX2+Код_исключения (третий байт пакета)
    static const uint8_t ku8MBExtendedError              = 0x08;
    
    uint16_t getResponseBuffer(uint8_t);
    void     clearResponseBuffer();
    uint8_t  setTransmitBuffer(uint8_t, uint16_t);
    void     clearTransmitBuffer();
    
    void beginTransmission(uint16_t);
    //uint8_t requestFrom(uint16_t, uint16_t);
    void sendBit(bool);
    void send(uint8_t);
    void send(uint16_t);
    void send(uint32_t);
    uint8_t available(void);
    uint16_t receive(void);
    
    uint8_t  readCoils(uint16_t, uint16_t);
    uint8_t  readDiscreteInputs(uint16_t, uint16_t);
    uint8_t  readHoldingRegisters(uint16_t, uint16_t);
    uint8_t  readInputRegisters(uint16_t, uint8_t);
    uint8_t  writeSingleCoil(uint16_t, uint8_t);
    uint8_t  writeSingleRegister(uint16_t, uint16_t);
    uint8_t  writeMultipleCoils(uint16_t, uint16_t);
    uint8_t  writeMultipleCoils();
    uint8_t  writeMultipleRegisters(uint16_t, uint16_t);
    uint8_t  writeMultipleRegisters();
    uint8_t  maskWriteRegister(uint16_t, uint16_t, uint16_t);
    uint8_t  readWriteMultipleRegisters(uint16_t, uint16_t, uint16_t, uint16_t);
    uint8_t  readWriteMultipleRegisters(uint16_t, uint16_t);
    uint8_t  LinkTestOmronMX2Only(uint16_t);

    // master function that conducts Modbus transactions
    uint8_t ModbusMasterTransaction(uint8_t);
	uint8_t ModbusMinTimeBetweenTransaction; // ms
    // Modbus timeout [milliseconds] Depend on serial speed
	uint8_t ModbusResponseTimeout; // < Modbus timeout, every byte [milliseconds]
    
  private:
    Stream* _serial;                                             ///< reference to serial port object
    uint8_t  _u8MBSlave;                                         ///< Modbus slave (1..255) initialized in begin()
    static const uint8_t ku8MaxBufferSize                = 64;   ///< size of response/transmit buffers    
    uint16_t _u16ReadAddress;                                    ///< slave register from which to read
    uint16_t _u16ReadQty;                                        ///< quantity of words to read
    uint16_t _u16ResponseBuffer[ku8MaxBufferSize];               ///< buffer to store Modbus slave response; read via GetResponseBuffer()
    uint16_t _u16WriteAddress;                                   ///< slave register to which to write
    uint16_t _u16WriteQty;                                       ///< quantity of words to write
    uint16_t _u16TransmitBuffer[ku8MaxBufferSize];               ///< buffer containing data to transmit to Modbus slave; set via SetTransmitBuffer()
    uint16_t* txBuffer; // from Wire.h -- need to clean this up Rx
    uint8_t _u8TransmitBufferIndex;
    uint16_t u16TransmitBufferLength;
    uint16_t* rxBuffer; // from Wire.h -- need to clean this up Rx
    uint8_t _u8ResponseBufferIndex;
    uint8_t _u8ResponseBufferLength;
    uint32_t last_transaction_time;

    // idle callback function; gets called during idle time between TX and RX
    void (*_idle)();
    // preTransmission callback function; gets called before writing a Modbus message
    void (*_preTransmission)();
    // postTransmission callback function; gets called after a Modbus message has been sent
    void (*_postTransmission)();
};
#endif

/**
@example examples/Basic/Basic.pde
@example examples/PhoenixContact_nanoLC/PhoenixContact_nanoLC.pde
@example examples/RS485_HalfDuplex/RS485_HalfDuplex.ino
*/
