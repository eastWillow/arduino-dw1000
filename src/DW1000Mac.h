/*
 * Copyright (c) 2015 by Leopold Sayous <leosayous@gmail.com> and Thomas Trojer <thomas@trojer.net>
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
 * @file DW1000Mac.h
 * Arduino global library (header file) working with the DW1000 library
 * for the Decawave DW1000 UWB transceiver IC. This class has the purpose
 * to generate the mac layer
 *
 * @todo everything, this class is only a prototype
 */

// FF FF as dest Address is BCAST

// ### Blink Frame Structure ### //
// ## 0  |  1   |  2-9   | 10-11
// ## FC | Seq# | SrcAdd | AddChk
// # Length = 12 Bytes
// Src Address is 8 bytes (reversed full)
// Address Check is 2 bytes (reversed short)
// generateBlinkFrame(byte frame[], byte sourceAddress[], byte sourceShortAddress[]);

// ### Short Mac Frame Structure ### //
// ## 0  |  1   |  2   |   3-4  |  5-6   |  7-8
// ## FC | FC_2 | Seq# | PAN ID | DstAdd | SrcAdd
// # Length = 9 Bytes
// Src and Dest Addresses are 2 bytes (reversed short)
// Pan ID is set to 0xDECA (Hardcoded) TODO fix this
// generateShortMACFrame(byte frame[], byte sourceShortAddress[], byte destinationShortAddress[]);

// ### Long Mac Frame Structure ### //
// ## 0  |  1   |  2   |   3-4  |  5-12   |  13-14
// ## FC | FC_2 | Seq# | PAN ID | DstAdd  | SrcAdd
// # Length = 15 Bytes
// Dest Addresses is 8 bytes (reversed full)
// Src Addresses is 2 bytes (reversed short)
// Pan ID is set to 0xDECA (Hardcoded) TODO fix this
// generateLongMACFrame(byte frame[], byte sourceShortAddress[], byte destinationAddress[]);

// Frame Control Flags
#define FC_1 0x41
#define FC_1_BLINK 0xC5
#define FC_2 0x8C
#define FC_2_SHORT 0x88

#define FC_1_DATA_WO_ACK 0x41
#define FC_1_DATA_W_ACK 0x61
#define FC_1_ACK_WO_ACK 0x42
#define FC_1_ACK_W_ACK 0x62
#define FC_1_MAC_WO_ACK 0x43
#define FC_1_MAC_W_ACK 0x63
#define FC_1_TYPE4_WO_ACK 0x44
#define FC_1_TYPE4_W_ACK 0x64
#define FC_1_TYPE5_WO_ACK 0x45
#define FC_1_TYPE5_W_ACK 0x65
#define FC_1_TYPE6_WO_ACK 0x46
#define FC_1_TYPE6_W_ACK 0x66
#define FC_1_TYPE7_WO_ACK 0x47
#define FC_1_TYPE7_W_ACK 0x67

#define PAN_ID_1 0xCA
#define PAN_ID_2 0xDE

#define SHORT_MAC_LEN 9
#define LONG_MAC_LEN 15

#ifndef _DW1000MAC_H_INCLUDED
#define _DW1000MAC_H_INCLUDED

#include <Arduino.h>
#include "DW1000Device.h"

class DW1000Device;

class DW1000Mac
{
  public:
	//Constructor and destructor
	DW1000Mac(DW1000Device *parent);
	DW1000Mac();
	~DW1000Mac();

	//setters
	void setDestinationAddress(byte *destinationAddress);
	void setDestinationAddressShort(byte *shortDestinationAddress);
	void setSourceAddress(byte *sourceAddress);
	void setSourceAddressShort(byte *shortSourceAddress);

	//for poll message we use just 2 bytes address
	//total=12 bytes
	void generateBlinkFrame(byte frame[], byte sourceAddress[], byte sourceShortAddress[]);

	//the short fram usually for Resp, Final, or Report
	//2 bytes for Desination Address and 2 bytes for Source Address
	//total=9 bytes
	void generateShortMACFrame(byte frame[], byte sourceShortAddress[], byte destinationShortAddress[]);
	void generateShortMACFrame(byte frame[], byte sourceShortAddress[], byte destinationShortAddress[], byte network[]);
	void generateShortMACFrame(byte fc, byte frame[], byte sourceShortAddress[], byte destinationShortAddress[], byte network[]);
	//the long frame for Ranging init
	//8 bytes for Destination Address and 2 bytes for Source Address
	//total of
	void generateLongMACFrame(byte frame[], byte sourceShortAddress[], byte destinationAddress[]);

	//in order to decode the frame and save source Address!
	void decodeBlinkFrame(byte frame[], byte address[], byte shortAddress[]);
	void decodeShortMACFrame(byte frame[], byte address[]);
	void decodeShortMACFrame(byte frame[], byte destAddr[], byte srcAddr[]);
	void decodeShortMACFrame(byte* fc, byte frame[], byte sourceShortAddress[], byte destinationShortAddress[], byte network[]);
	void decodeShortMACFrame(byte frame[], byte sourceShortAddress[], byte destinationShortAddress[], byte network[]);
	void decodeLongMACFrame(byte frame[], byte address[]);
	void decodeLongMACFrame(byte frame[], byte destAddr[], byte srcAddr[]);

	void incrementSeqNumber();

  private:
	uint8_t _seqNumber = 0;
	void reverseArray(byte to[], byte from[], int16_t size);
};

#endif
