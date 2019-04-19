/*
 * Copyright (c) 2015 by Thomas Trojer <thomas@trojer.net> and Leopold Sayous <leosayous@gmail.com>
 * Copyright (c) 2019 by Yu-Ti Kuo <bobgash2@gmail.com> and Leopold Sayous <leosayous@gmail.com>
 * Decawave DW1000 library for arduino.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file DW1000Device_UD.cpp
 * Arduino global library (source file) working with the DW1000 library
 * for the Decawave DW1000 UWB transceiver IC.
 * 
 * @todo complete this class
 */

#include "DW1000Device_UD.h"
#include "DW1000.h"

//Constructor and destructor
DW1000Device_UD::DW1000Device_UD(byte network[])
{
	randomShortAddress();
	setNetwork(network);
}

DW1000Device_UD::DW1000Device_UD(uint16_t network)
{
	_shortAddress[0] = 0x00;
	_shortAddress[1] = 0x00;
	byte _network[2];
	DW1000.writeValueToBytes(_network, network, 2);
	setNetwork(_network);
}

DW1000Device_UD::DW1000Device_UD(uint16_t network, byte shortAddress[])
{
	setShortAddress(shortAddress);
	byte _network[2];
	DW1000.writeValueToBytes(_network, network, 2);
	setNetwork(_network);
}

DW1000Device_UD::DW1000Device_UD(byte deviceAddress[], byte shortAddress[], byte network[])
{
	//we have a 8 bytes address
	setAddress(deviceAddress);
	//we set the 2 bytes address
	setShortAddress(shortAddress);
	//we set the 2 bytes network
	setNetwork(network);
}

DW1000Device_UD::~DW1000Device_UD()
{
}

//setters:
void DW1000Device_UD::setReplyTime(uint16_t replyDelayTimeUs) { _replyDelayTimeUS = replyDelayTimeUs; }

void DW1000Device_UD::setAddress(char deviceAddress[]) { DW1000.convertToByte(deviceAddress, _ownAddress); }

void DW1000Device_UD::setAddress(byte *deviceAddress)
{
	memcpy(_ownAddress, deviceAddress, 8);
}

void DW1000Device_UD::setShortAddress(byte shortAddress[])
{
	memcpy(_shortAddress, shortAddress, 2);
}

void DW1000Device_UD::setNetwork(byte network[])
{
	memcpy(_network, network, 2);
}

void DW1000Device_UD::setRange(float range) { _range = round(range * 100); }

void DW1000Device_UD::setRXPower(float RXPower) { _RXPower = round(RXPower * 100); }

void DW1000Device_UD::setFPPower(float FPPower) { _FPPower = round(FPPower * 100); }

void DW1000Device_UD::setQuality(float quality) { _quality = round(quality * 100); }

byte *DW1000Device_UD::getByteAddress()
{
	return _ownAddress;
}

byte *DW1000Device_UD::getByteShortAddress()
{
	return _shortAddress;
}

uint16_t DW1000Device_UD::getShortAddress()
{
	return _shortAddress[1] * 256 + _shortAddress[0];
}

byte *DW1000Device_UD::getByteNetowrk()
{
	return _network;
}

uint16_t DW1000Device_UD::getNetowrk()
{
	return _network[1] * 256 + _network[0];
}

boolean DW1000Device_UD::isAddressEqual(DW1000Device_UD *device)
{
	return memcmp(this->getByteAddress(), device->getByteAddress(), 8) == 0;
}

boolean DW1000Device_UD::isNetworkEqual(DW1000Device_UD *device)
{
	return memcmp(this->getByteNetowrk(), device->getByteNetowrk(), 8) == 0;
}

boolean DW1000Device_UD::isShortAddressEqual(DW1000Device_UD *device)
{
	return memcmp(this->getByteShortAddress(), device->getByteShortAddress(), 2) == 0;
}

float DW1000Device_UD::getRange() { return float(_range) / 100.0f; }

float DW1000Device_UD::getRXPower() { return float(_RXPower) / 100.0f; }

float DW1000Device_UD::getFPPower() { return float(_FPPower) / 100.0f; }

float DW1000Device_UD::getQuality() { return float(_quality) / 100.0f; }

void DW1000Device_UD::randomShortAddress()
{
	_shortAddress[0] = random(0, 256);
	_shortAddress[1] = random(0, 256);
}
