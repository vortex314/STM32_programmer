/*
 * Stm32.cpp
 *
 *  Created on: Jul 2, 2016
 *      Author: lieven
 */

#include "Stm32.h"

#define PIN_RESET 4 // GPIO4 D2
#define PIN_BOOT0 5 // GPIO5 D1
#define ACK 0x79
#define NACK 0x1F
#define DELAY 10

enum {
	BL_GET = 0,
	BL_GET_VERSION = 1,
	BL_GET_ID = 2,
	BL_READ_MEMORY = 0x11,
	BL_GO = 0x21,
	BL_WRITE_MEMORY = 0x31,
	BL_ERASE_MEMORY = 0x43,
	BL_EXTENDED_ERASE_MEMORY = 0x44,
	BL_WRITE_PROTECT = 0x63,
	BL_WRITE_UNPROTECT = 0x73,
	BL_READOUT_PROTECT = 0x82,
	BL_READOUT_UNPROTECT = 0x92
} BootLoaderCommand;
#define XOR(xxx) (xxx ^ 0xFF)

bool Stm32::_alt_serial = false;
uint64_t Stm32::_timeout = 0;
bool Stm32::_boot0 = true;

Erc Stm32::setAltSerial(bool flag) {
	if (_alt_serial == flag)
		return E_OK;
	_alt_serial = flag;
	Serial.swap();
	return E_OK;
}
/*
 Erc Stm32::setBoot0(bool flag) {
 if (_boot0 == flag)
 return E_OK;
 digitalWrite(PIN_BOOT0, flag);	// 1 : bootloader mode, 0: flash run
 _boot0 = flag;
 return E_OK;
 }*/

void Stm32::init() {
	pinMode(PIN_RESET, OUTPUT);
	digitalWrite(PIN_RESET, 1);
	pinMode(PIN_BOOT0, OUTPUT);
	boot0Flash();
	_mode = M_SYSTEM;
//	setAltSerial(true);
}

Erc Stm32::boot0Flash() {
	digitalWrite(PIN_BOOT0, 0);
	return E_OK;
}

Erc Stm32::boot0System() {
	digitalWrite(PIN_BOOT0, 1);
	return E_OK;
}

Str str(1024);
void logBytes(const char* location, Bytes& bytes) {
	str.clear();
	bytes.toHex(str);
	LOGF(" %s : %s", location, str.c_str());
}
Bytes in(300);

Erc Stm32::waitAck(Bytes& out, Bytes& in, uint32_t count, uint32_t time) {
	Serial.write(out.data(), out.length());
	timeout(time);
	while (true) {
		if (timeout()) {
			logBytes("TIMEOUT", in);
			return ETIMEDOUT;
		};
		if (Serial.available()) {
			byte b;
			while (Serial.available()) {
				in.write(b = Serial.read());
			}
			if (b == ACK)
				break;
		}
	}
//	logBytes("ACK", in);
	return E_OK;
}

Erc Stm32::readVar(Bytes& in, uint32_t max, uint32_t time) {
	uint32_t count;
	timeout(time);
	while (true) {
		if (timeout()) {
			return ETIMEDOUT;
		};
		if (Serial.available()) {
			count = Serial.read() + 1;
//			in.write(count - 1);
			break;
		}
	}
	if (count > max)
		return EINVAL;
	while (count) {
		if (timeout()) {
			return ETIMEDOUT;
		};
		if (Serial.available()) {
			in.write(Serial.read());
			count--;
		}
	}
	return E_OK;
}

Erc Stm32::read(Bytes& in, uint32_t count, uint32_t time) {
	timeout(time);
	while (count) {
		if (timeout()) {
			return ETIMEDOUT;
		};
		if (Serial.available()) {
			in.write(Serial.read());
			count--;
		}
	}
	return E_OK;
}

void flush() {
	while (Serial.available()) {
		Serial.read();
	};
	in.clear();
}

byte xorBytes(byte* data, uint32_t count) {
	byte x = data[0];
	int i = 1;
	while (i < count) {
		x = x ^ data[i];
		i++;
	}
	return x;
}

byte slice(uint32_t word, int offset) {
	return (byte) ((word >> (offset * 8)) & 0xFF);
}

Erc Stm32::resetFlash() {
	boot0Flash();
	digitalWrite(PIN_RESET, 0);
	delay(10);
	digitalWrite(PIN_RESET, 1);
	delay(10);
	_mode = M_FLASH;
	return E_OK;
}

Erc Stm32::resetSystem() {
	boot0System();
	digitalWrite(PIN_RESET, 0);
	delay(10);
	digitalWrite(PIN_RESET, 1);
	delay(10);
	Serial.write(0x7F);	// send sync for bootloader
	_mode = M_SYSTEM;
	return E_OK;
}

Erc Stm32::getId(uint16_t& id) {
	byte GET_ID[] = { BL_GET_ID, XOR(BL_GET_ID) };
	Bytes out(GET_ID, 2);
	flush();
	Erc erc = E_OK;
	if ((erc = waitAck(out, in, 1, DELAY)) == E_OK) {
		in.clear();
		if ((erc = readVar(in, 4, DELAY)) == E_OK) {
			id = in.peek(0) * 256 + in.peek(1);
		}
	}
	return erc;
}

Erc Stm32::get(uint8_t& version, Bytes& cmds) {
	byte GET[] = { BL_GET, XOR(BL_GET) };
	Bytes buffer(30);
	Bytes out(GET, 2);
	flush();
	Erc erc = E_OK;
	if ((erc = waitAck(out, cmds, 1, DELAY)) == E_OK) {
		cmds.clear();
		if ((erc = readVar(buffer, 30, DELAY)) == E_OK) {
			buffer.offset(0);
			version = buffer.read();
			while (buffer.hasData())
				cmds.write(buffer.read());
		}
	}
	return erc;
}

Erc Stm32::getVersion(uint8_t& version) {
	byte GET_VERSION[] = { BL_GET_VERSION, XOR(BL_GET_VERSION) };
	Bytes out(GET_VERSION, 2);
	Bytes in(4);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	if ((erc = waitAck(out, in, 1, DELAY)) == E_OK) {
		in.clear();
		if ((erc = waitAck(noData, in, 4, DELAY)) == E_OK) {
			version = in.peek(0);
		}
	}
	return erc;
}

Erc Stm32::writeMemory(uint32_t address, Bytes& data) {
	byte GET[] = { BL_WRITE_MEMORY, XOR(BL_WRITE_MEMORY) };
	byte ADDRESS[] = { slice(address, 3), slice(address, 2), slice(address, 1),
			slice(address, 0), xorBytes(ADDRESS, 4) };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	if ((erc = waitAck(out.map(GET, 2), in, 1, DELAY)) == E_OK) {
		if ((erc = waitAck(out.map(ADDRESS, 5), in, 1, DELAY)) == E_OK) {
			Serial.write(data.length() - 1);
			Serial.write(data.data(), data.length());
			Serial.write(
					((byte) (data.length() - 1))
							^ xorBytes(data.data(), data.length()));
			if ((erc = waitAck(noData, in, 10, 200)) == E_OK) {

			}
		}
	}
	return erc;
}

Erc Stm32::readMemory(uint32_t address, uint32_t length, Bytes& data) {
	byte READ_MEMORY[] = { BL_READ_MEMORY, XOR(BL_READ_MEMORY) };
	byte ADDRESS[] = { slice(address, 3), slice(address, 2), slice(address, 1),
			slice(address, 0), xorBytes(ADDRESS, 4) };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(READ_MEMORY, 2), in, 1, DELAY);
	if (erc)
		return erc;
	ADDRESS[4] = xorBytes(ADDRESS, 4);
	erc = waitAck(out.map(ADDRESS, 5), in, 1, DELAY);
	if (erc)
		return erc;
	Serial.write(length - 1);
	Serial.write(XOR(length - 1));
	erc = waitAck(noData, in, 1, DELAY);
	if (erc)
		return erc;
	if ((erc = read(data, length, 200)) == E_OK) {
	}
	return erc;
}

Erc Stm32::go(uint32_t address) {
	byte GO[] = { BL_GO, XOR(BL_GO) };
	byte ADDRESS[] = { slice(address, 3), slice(address, 2), slice(address, 1),
			slice(address, 0), xorBytes(ADDRESS, 4) };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(GO, 2), in, 1, DELAY);
	if (erc)
		return erc;
	ADDRESS[4] = xorBytes(ADDRESS, 4);
	erc = waitAck(out.map(ADDRESS, 5), in, 1, DELAY);
	return erc;
}

Erc Stm32::eraseMemory(Bytes& pages) {
	byte ERASE_MEMORY[] = { BL_ERASE_MEMORY, XOR(BL_ERASE_MEMORY) };

	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(ERASE_MEMORY, 2), in, 1, DELAY);
	if (erc)
		return erc;
	Serial.write(pages.length() - 1);
	Serial.write(pages.data(), pages.length());
	Serial.write(
			((byte) (pages.length() - 1))
					^ xorBytes(pages.data(), pages.length()));
	erc = waitAck(noData, in, 10, 200);
	return erc;
}

Erc Stm32::eraseAll() {
	byte ERASE_MEMORY[] = { BL_ERASE_MEMORY, XOR(BL_ERASE_MEMORY) };
	byte ALL_PAGES[] = { 0xFF, 0x00 };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(ERASE_MEMORY, 2), in, 1, DELAY);
	if (erc)
		return erc;
	erc = waitAck(out.map(ALL_PAGES, 2), in, 1, 200);
	return erc;
}

Erc Stm32::extendedEraseMemory() {
	byte EXTENDED_ERASE_MEMORY[] = { BL_EXTENDED_ERASE_MEMORY, XOR(
			BL_EXTENDED_ERASE_MEMORY) };
	byte ALL_PAGES[] = { 0xFF, 0xFF, 0 };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(EXTENDED_ERASE_MEMORY, 2), in, 1, DELAY);
	if (erc)
		return erc;
	erc = waitAck(out.map(ALL_PAGES, 3), in, 1, 200);
	return erc;
}

Erc Stm32::writeProtect(Bytes& sectors) {
	byte WP[] = { BL_WRITE_PROTECT, XOR(BL_WRITE_PROTECT) };

	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(WP, 2), in, 1, DELAY);
	if (erc)
		return erc;
	Serial.write(sectors.length() - 1);
	Serial.write(sectors.data(), sectors.length());
	Serial.write(
			((byte) (sectors.length() - 1))
					^ xorBytes(sectors.data(), sectors.length()));
	erc = waitAck(noData, in, 10, 200);
	return erc;
}

Erc Stm32::writeUnprotect() {
	byte WUP[] = { BL_WRITE_UNPROTECT, XOR(BL_WRITE_UNPROTECT) };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(WUP, 2), in, 1, DELAY);
	if (erc)
		return erc;
	erc = waitAck(noData, in, 1, DELAY);
	return erc;
}

Erc Stm32::readoutProtect() {
	byte RDP[] = { BL_READOUT_PROTECT, XOR(BL_READOUT_PROTECT) };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(RDP, 2), in, 1, DELAY);
	if (erc)
		return erc;
	erc = waitAck(noData, in, 1, DELAY);
	return erc;
}

Erc Stm32::readoutUnprotect() {
	byte RDUP[] = { BL_READOUT_UNPROTECT, XOR(BL_READOUT_UNPROTECT) };
	Bytes out(0);
	Bytes noData(0);
	flush();
	Erc erc = E_OK;
	erc = waitAck(out.map(RDUP, 2), in, 1, DELAY);
	if (erc)
		return erc;
	erc = waitAck(noData, in, 1, DELAY);
	return erc;
}

bool Stm32::timeout() {
	return _timeout < millis();
}

void Stm32::timeout(uint32_t delta) {
	_timeout = millis() + delta;
}
extern void mqttPublish(const char* topic, String message);

Bytes uartBuffer(256);
uint32_t Stm32::_usartRxd = 0;
uint32_t lastDataReceived = 0;
uint32_t firstDataReceived = 0;
bool sendNow = false;

void Stm32::loop() {
	if (Serial.available() && (uartBuffer.length() == 0)) {
		firstDataReceived = millis();
	}
	while (Serial.available()) {
		byte b = Serial.read();
		_usartRxd++;
		if (b == '\n')
			sendNow = true;
		uartBuffer.write(b);
		lastDataReceived = millis();
	};
	if (uartBuffer.length() > 200)
		sendNow = true;
	if (((lastDataReceived - firstDataReceived) > 100))	// more then 100 msec delay
		sendNow = true;
	if (sendNow && uartBuffer.length()) {
		String msg;
		uartBuffer.offset(0);
		while (uartBuffer.hasData())
			msg.concat((char) uartBuffer.read());
		mqttPublish("usart/rxd", msg);
		uartBuffer.clear();
		sendNow = false;
	}
}

