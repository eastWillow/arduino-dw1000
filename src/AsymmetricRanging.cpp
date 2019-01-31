/*
 * Copyright (c) 2015 by Thomas Trojer <thomas@trojer.net> and Leopold Sayous <leosayous@gmail.com>
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
 * @file AsymmetricRanging.cpp
 * Arduino global library (source file) working with the DW1000 library
 * for the Decawave DW1000 UWB transceiver IC.
 *
 * @TODO
 * - remove or debugmode for Serial.print
 * - move strings to flash to reduce ram usage
 * - do not safe duplicate of pin settings
 * - maybe other object structure
 * - use enums instead of preprocessor constants
 */


#include "AsymmetricRanging.h"
#include "DW1000Device.h"

AsymmetricRangingClass AsymmetricRanging;


//other devices we are going to communicate with which are on our network:
DW1000Device AsymmetricRangingClass::_networkDevices[MAX_DEVICES];
byte         AsymmetricRangingClass::_currentAddress[8];
byte         AsymmetricRangingClass::_currentShortAddress[2];
byte         AsymmetricRangingClass::_lastSentToShortAddress[2];
volatile uint8_t AsymmetricRangingClass::_networkDevicesNumber = 0; // TODO short, 8bit?
int16_t      AsymmetricRangingClass::_lastDistantDevice    = 0; // TODO short, 8bit?
DW1000Mac    AsymmetricRangingClass::_globalMac;

//module type (anchor or tag)
int16_t      AsymmetricRangingClass::_type; // TODO enum??

// message flow state
volatile byte    AsymmetricRangingClass::_expectedMsgId;

// range filter
volatile boolean AsymmetricRangingClass::_useRangeFilter = false;
uint16_t AsymmetricRangingClass::_rangeFilterValue = 15;

// message sent/received state
volatile boolean AsymmetricRangingClass::_sentAck     = false;
volatile boolean AsymmetricRangingClass::_receivedAck = false;

// protocol error state
boolean          AsymmetricRangingClass::_protocolFailed = false;

// timestamps to remember
int32_t            AsymmetricRangingClass::timer           = 0;
int16_t            AsymmetricRangingClass::counterForBlink = 0; // TODO 8 bit?


// data buffer
byte          AsymmetricRangingClass::data[LEN_DATA];
// reset line to the chip
uint8_t   AsymmetricRangingClass::_RST;
uint8_t   AsymmetricRangingClass::_SS;
// watchdog and reset period
uint32_t  AsymmetricRangingClass::_lastActivity;
uint32_t  AsymmetricRangingClass::_resetPeriod;
// reply times (same on both sides for symm. ranging)
uint16_t  AsymmetricRangingClass::_replyDelayTimeUS;
//timer delay
uint16_t  AsymmetricRangingClass::_timerDelay;
// ranging counter (per second)
uint16_t  AsymmetricRangingClass::_successRangingCount = 0;
uint32_t  AsymmetricRangingClass::_rangingCountPeriod  = 0;
//Here our handlers
void (* AsymmetricRangingClass::_handleNewRange)(void) = 0;
void (* AsymmetricRangingClass::_handleBlinkDevice)(DW1000Device*) = 0;
void (* AsymmetricRangingClass::_handleNewDevice)(DW1000Device*) = 0;
void (* AsymmetricRangingClass::_handleInactiveDevice)(DW1000Device*) = 0;

void (* AsymmetricRangingClass::_handleUnknownMessage)(byte(*)[90],uint16_t) = 0;
void (* AsymmetricRangingClass::_handleSafeTransmit)(DW1000Device*) = 0;

/* ###########################################################################
 * #### Init and end #######################################################
 * ######################################################################### */

void AsymmetricRangingClass::initCommunication(uint8_t myRST, uint8_t mySS, uint8_t myIRQ) {
	// reset line to the chip
	_RST              = myRST;
	_SS               = mySS;
	_resetPeriod      = DEFAULT_RESET_PERIOD;
	// reply times (same on both sides for symm. ranging)
	_replyDelayTimeUS = DEFAULT_REPLY_DELAY_TIME;
	//we set our timer delay
	_timerDelay       = DEFAULT_TIMER_DELAY;


	DW1000.begin(myIRQ, myRST);
	DW1000.select(mySS);
}


void AsymmetricRangingClass::configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]) {
	// general configuration
	DW1000.newConfiguration();
	DW1000.setDefaults();
	DW1000.setDeviceAddress(deviceAddress);
	DW1000.setNetworkId(networkId);
	DW1000.enableMode(mode);
	DW1000.commitConfiguration();

}

void AsymmetricRangingClass::generalStart() {
	// attach callback for (successfully) sent and received messages
	DW1000.attachSentHandler(handleSent);
	DW1000.attachReceivedHandler(handleReceived);
	// anchor starts in receiving mode, awaiting a ranging poll message


	if(DEBUG) {
		// DEBUG monitoring
		Serial.println(F("DW1000-arduino"));
		// initialize the driver


		Serial.println(F("configuration.."));
		// DEBUG chip info and registers pretty printed
		char msg[90];
		DW1000.getPrintableDeviceIdentifier(msg);
		Serial.print(F("Device ID: "));
		Serial.println(msg);
		DW1000.getPrintableExtendedUniqueIdentifier(msg);
		Serial.print(F("Unique ID: "));
		Serial.print(msg);
		char string[6];
		sprintf(string, "%02X:%02X", _currentShortAddress[0], _currentShortAddress[1]);
		Serial.print(F(" short: "));
		Serial.println(string);

		DW1000.getPrintableNetworkIdAndShortAddress(msg);
		Serial.print(F("Network ID & Device Address: "));
		Serial.println(msg);
		DW1000.getPrintableDeviceMode(msg);
		Serial.print(F("Device mode: "));
		Serial.println(msg);
	}


	// anchor starts in receiving mode, awaiting a ranging poll message
	receiver();
	// for first time ranging frequency computation
	_rangingCountPeriod = millis();
}

void AsymmetricRangingClass::startNode(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print(F("device address: "));
	Serial.println(address);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}

	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	// TODO: Hardcoded PAN (0xDECA)
	AsymmetricRanging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);

	//general start:
	generalStart();

	//defined type as anchor
	_type = NODE;

	Serial.println(F("### ANCHOR ###"));
}

void AsymmetricRangingClass::startAsAnchor(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print(F("device address: "));
	Serial.println(address);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}

	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	AsymmetricRanging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);

	//general start:
	generalStart();

	//defined type as anchor
	_type = ANCHOR;

	Serial.println(F("### ANCHOR ###"));

}

void AsymmetricRangingClass::startAsTag(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print(F("device address: "));
	Serial.println(address);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}

	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	AsymmetricRanging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);

	generalStart();
	//defined type as tag
	_type = TAG;

	Serial.println(F("### TAG ###"));
}

boolean AsymmetricRangingClass::addNetworkDevices(DW1000Device* device, boolean shortAddress) {
	boolean   addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && !shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		else if(_networkDevices[i].isShortAddressEqual(device) && shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}

	}

	if(addDevice) {
		device->setRange(0);
		memcpy(&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}

	return false;
}

boolean AsymmetricRangingClass::addNetworkDevices(DW1000Device* device) {
	boolean addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && _networkDevices[i].isShortAddressEqual(device)) {
			//the device already exists
			addDevice = false;
			return false;
		}

	}

	if(addDevice) {
		if(_type == ANCHOR) //for now let's start with 1 TAG
		{
			_networkDevicesNumber = 0;
		}
		memcpy(&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}

	return false;
}

void AsymmetricRangingClass::removeNetworkDevices(int16_t index) {
	//if we have just 1 element
	if(_networkDevicesNumber == 1) {
		_networkDevicesNumber = 0;
	}
	else if(index == _networkDevicesNumber-1) //if we delete the last element
	{
		_networkDevicesNumber--;
	}
	else {
		//we translate all the element wich are after the one we want to delete.
		for(int16_t i = index; i < _networkDevicesNumber-1; i++) { // TODO 8bit?
			memcpy(&_networkDevices[i], &_networkDevices[i+1], sizeof(DW1000Device));
			_networkDevices[i].setIndex(i);
		}
		_networkDevicesNumber--;
	}
}

/* ###########################################################################
 * #### Setters and Getters ##################################################
 * ######################################################################### */

//setters
void AsymmetricRangingClass::setReplyTime(uint16_t replyDelayTimeUs) { _replyDelayTimeUS = replyDelayTimeUs; }

void AsymmetricRangingClass::setResetPeriod(uint32_t resetPeriod) { _resetPeriod = resetPeriod; }


DW1000Device* AsymmetricRangingClass::searchDistantDevice(byte shortAddress[]) {
	//we compare the 2 bytes address with the others
	for(uint16_t i = 0; i < _networkDevicesNumber; i++) { // TODO 8bit?
		if(memcmp(shortAddress, _networkDevices[i].getByteShortAddress(), 2) == 0) {
			//we have found our device !
			return &_networkDevices[i];
		}
	}

	return nullptr;
}

DW1000Device* AsymmetricRangingClass::getDistantDevice() {
	//we get the device which correspond to the message which was sent (need to be filtered by MAC address)

	return &_networkDevices[_lastDistantDevice];

}


/* ###########################################################################
 * #### Public methods #######################################################
 * ######################################################################### */

void AsymmetricRangingClass::checkForReset() {
	uint32_t curMillis = millis();
	if(!_sentAck && !_receivedAck) {
		// check if inactive
		if(curMillis-_lastActivity > _resetPeriod) {
			resetInactive();
		}
		return; // TODO cc
	}
}

void AsymmetricRangingClass::checkForInactiveDevices() {
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isInactive()) {
			if(_handleInactiveDevice != 0) {
				(*_handleInactiveDevice)(&_networkDevices[i]);
			}
			//we need to delete the device from the array:
			removeNetworkDevices(i);

		}
	}
}

// TODO check return type
int16_t AsymmetricRangingClass::detectMessageType(byte datas[]) {
	// If the first byte denotes a blink frame, then return BLINK
	if(datas[0] == FC_1_BLINK) {
		return BLINK;
	}

	// If the first byte denotes a MAC frame and the second byte denotes a
	// full length MAC address, then return the first byte of the messsage (which
	// starts right after the MAC frame ends)
	else if(datas[0] == FC_1 && datas[1] == FC_2) {
		//we have a long MAC frame message (ranging init)
		return datas[LONG_MAC_LEN];
	}

	// If the first byte denotes a MAC frame and the second byte denotes a
	// short length MAC address, then return the first byte of the messsage (which
	// starts right after the MAC frame ends)
	else if(datas[0] == FC_1 && datas[1] == FC_2_SHORT) {
		//we have a short mac frame message (poll, range, range report, etc..)
		return datas[SHORT_MAC_LEN];
	}
}

void AsymmetricRangingClass::loop() {
	//we check if needed to reset !
	checkForReset();
	uint32_t time = millis(); // TODO other name - too close to "timer"
	if(time-timer > _timerDelay) {
		timer = time;
		timerTick();
	}

	// IF we have sent a message since the last loop
	// The transmitted data is stored in the data variable
	if(_sentAck) {
		// Flag that we have delt with this transmission
		_sentAck = false;

		// Log the sent message
		if (DEBUG) {
			Serial.print(F("t-"));
			DW1000.printPrettyHex(data, LEN_DATA);
		}

		// Detect the message type. Will be BLINK, or one the contents of the first
		// byte after the MAC frame finishes (POLL_ACK, POLL, RANGE, RANGE_INIT,
		// RANGE_REPORT, RANGE_FAILED)
		int messageType = detectMessageType(data);

		// If the message is not a POLL_ACK, not a POLL, and not a RANGE, then
		// return. This filters out BLINK, RANGE_INIT, RANGE_REPORT, RANGE_FAILED,
		// and other unknown message types
		if(messageType != POLL_ACK && messageType != POLL && messageType != RANGE)
			return;

		// A msg was sent. We launch the ranging protocol when a message was sent

		// If this device is an ANCHOR, see what we just sent. If it was a POLL_ACK,
		// then note the timestamp of our outgoing message
		if(_type == ANCHOR) {
			if(messageType == POLL_ACK) {
				DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);

				if (myDistantDevice) {
					DW1000.getTransmitTimestamp(myDistantDevice->timePollAckSent);
				}
			}
		}

		// If this device is a TAG, then see what we sent.
		else if(_type == TAG) {

			// If it was a POLL, then
		  // store when we sent our initial poll to this distant device. If the poll
		  // was a broadcase, then store the timestamp for all distant devices.
			// This stores to DW1000Device -> timePollSent
			if(messageType == POLL) {
				DW1000Time timePollSent;
				DW1000.getTransmitTimestamp(timePollSent);
				//if the last device we send the POLL is broadcast:
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					//we save the value for all the devices !
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timePollSent = timePollSent;
					}
				}
				else {
					//we search the device associated with the last send address
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					//we save the value just for one device
					if (myDistantDevice) {
						myDistantDevice->timePollSent = timePollSent;
					}
				}
			}

			// If it was a RANGE, then
		  // store when we sent our initial poll to this distant device. If the poll
		  // was a broadcase, then store the timestamp for all distant devices.
			// This stores to DW1000Device -> timeRangeSent
			else if(messageType == RANGE) {
				DW1000Time timeRangeSent;
				DW1000.getTransmitTimestamp(timeRangeSent);
				//if the last device we send the POLL is broadcast:
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					//we save the value for all the devices !
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timeRangeSent = timeRangeSent;
					}
				}
				else {
					//we search the device associated with the last send address
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					//we save the value just for one device
					if (myDistantDevice) {
						myDistantDevice->timeRangeSent = timeRangeSent;
					}
				}

			}
		}
	}

	// If we recieved a message since the last loop
	if(_receivedAck) {
		// Flag this received massage as hendled
		_receivedAck = false;

		// Read data in from the DW1000 module. This is done here instead of in the
		// handle function so that our transmitted data is not overwritten (For the
		// case where we sent and recieved since the last loop)
		DW1000.getData(data, LEN_DATA);

		// DEBUGGING
		if (DEBUG) {
			Serial.print(F("r-"));
			DW1000.printPrettyHex(data, LEN_DATA);
		}

		// Detect the message type. Will be BLINK, or one the contents of the first
		// byte after the MAC frame finishes (POLL_ACK, POLL, RANGE, RANGE_INIT,
		// RANGE_REPORT, RANGE_FAILED)
		int messageType = detectMessageType(data);

		// If we have just recieved a BLINK message and this is an ANCHOR, decode
		// the blink frame and create a new DW1000Device. Add this device to the
		// network, then call the _handleBlinkDevice callback function. Finally
		// transmit a ranging init message. This is a distinct program path
		// -- Responds with a RANGE_INIT Message
		if(messageType == BLINK && _type == ANCHOR) {
			byte address[8];
			byte shortAddress[2];
			_globalMac.decodeBlinkFrame(data, address, shortAddress);
			//we crate a new device with th tag
			DW1000Device myTag(address, shortAddress);

			if(addNetworkDevices(&myTag)) {
				if(_handleBlinkDevice != 0) {
					(*_handleBlinkDevice)(&myTag);
				}
				//we reply by the transmit ranging init message
				// Long Mac Frame + RANGING_INIT as the messageType
				transmitRangingInit(&myTag);
				noteActivity();
			}

			// If we just recieved a blink, then we are expecting a POLL next
			_expectedMsgId = POLL;
		}

		// If we have just reicieved a RANGE_INIT and we are a TAG device, then
		// decode the long mac frame. Then create a new device in the network and
		// call the _handleNewDevice callback. This is a distinct program path
		else if(messageType == RANGING_INIT && _type == TAG) {

			byte address[2];
			_globalMac.decodeLongMACFrame(data, address);
			//we crate a new device with the anchor
			DW1000Device myAnchor(address, true);

			if(addNetworkDevices(&myAnchor, true)) {
				if(_handleNewDevice != 0) {
					(*_handleNewDevice)(&myAnchor);
				}
			}

			noteActivity();
		}

		// Otherwise, all incoming messages should be short MAC frames.
		else {
			//we have a short mac layer frame !
			byte address[2];
			_globalMac.decodeShortMACFrame(data, address);

			// Get the device associated with this address from our database
			DW1000Device* myDistantDevice = searchDistantDevice(address);

			// Check if we have a valid device, given our incoming address. If it
			// is not valud, then return
			// TODO Maybe dont just return
			if((_networkDevicesNumber == 0) || (myDistantDevice == nullptr)) {
				Serial.print(F("Ukn Addr:"));
				DW1000.printPrettyHex(address, 2);
				return;
			}

			// Now that we have a valid device picked out, proceed to check what type
			// of message we got.

			//// If we are an ANCHOR
			if(_type == ANCHOR) {
				// Check to ensure that the message type we got matches the expected
				// message id. If it does not, then mark the message as a failure
				if(messageType != _expectedMsgId) {
					// unexpected message, start over again (except if already POLL)
					_protocolFailed = true;
				}

				// If we have a POLL Message, scan it to see if we are included in the
				// list of polling devices. If the POLL was a broadcast, then it is
				// possible that we are not in the list. If we locate ourself in the
				// message, then record the time at which the poll was recieved and
				// reply with a POLL ACK. This is a distinct program path
				// -- Responds with a POLL_ACK Message
				if(messageType == POLL) {
					// We receive a POLL which is a broacast message
					// we need to grab info about it, specificlly the number of devices
					// that are included in the poll
					int16_t numberDevices = 0;
					memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);

					// See how many devices were requested in the poll
					for(uint16_t i = 0; i < numberDevices; i++) {
						// We need to test if this value is for us:
						byte shortAddress[2];
						memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*4, 2);

						// If this entry in the POLL Request is, in fact, for us, then we
						//
						if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) {
							// We grab our reply time from the list of reply times
							uint16_t replyTime;
							memcpy(&replyTime, data+SHORT_MAC_LEN+2+i*4+2, 2);
							// We configure our replyTime;
							_replyDelayTimeUS = replyTime;

							// On POLL we (re-)start, so no protocol failure
							_protocolFailed = false;

							// Get the timestamp at which we recieved this POLL message and
							// store it
							DW1000.getReceiveTimestamp(myDistantDevice->timePollReceived);
							// We note activity for our device:
							myDistantDevice->noteActivity();
							// We indicate our next receive message for our ranging protocol
							_expectedMsgId = RANGE;

							// Transmit a POLL ACK
							transmitPollAck(myDistantDevice);
							noteActivity();

							return;
						}
					}
				}

				// If we have a RANGE Message,
				// This is a distinct program path
				// -- Responds with a POLL_ACK Message
				else if(messageType == RANGE) {
					// We receive a RANGE which is a broacast message
					// We need to grab info about it, specifically
					uint8_t numberDevices = 0;
					memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);


					for(uint8_t i = 0; i < numberDevices; i++) {
						//we need to test if this value is for us:
						//we grab the mac address of each devices:
						byte shortAddress[2];
						memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*17, 2);

						//we test if the short address is our address
						if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) {
							//we grab the replytime wich is for us
							DW1000.getReceiveTimestamp(myDistantDevice->timeRangeReceived);
							noteActivity();
							_expectedMsgId = POLL;

							if(!_protocolFailed) {

								myDistantDevice->timePollSent.setTimestamp(data+SHORT_MAC_LEN+4+17*i);
								myDistantDevice->timePollAckReceived.setTimestamp(data+SHORT_MAC_LEN+9+17*i);
								myDistantDevice->timeRangeSent.setTimestamp(data+SHORT_MAC_LEN+14+17*i);

								// (re-)compute range as two-way ranging is done
								DW1000Time myTOF;
								computeRangeAsymmetric(myDistantDevice, &myTOF); // CHOSEN RANGING ALGORITHM

								float distance = myTOF.getAsMeters();

								if (_useRangeFilter) {
									//Skip first range
									if (myDistantDevice->getRange() != 0.0f) {
										distance = filterValue(distance, myDistantDevice->getRange(), _rangeFilterValue);
									}
								}

								myDistantDevice->setRXPower(DW1000.getReceivePower());
								myDistantDevice->setRange(distance);

								myDistantDevice->setFPPower(DW1000.getFirstPathPower());
								myDistantDevice->setQuality(DW1000.getReceiveQuality());

								//we send the range to TAG
								transmitRangeReport(myDistantDevice);

								//we have finished our range computation. We send the corresponding handler
								_lastDistantDevice = myDistantDevice->getIndex();
								if(_handleNewRange != 0) {
									(*_handleNewRange)();
								}

							}
							else {
								transmitRangeFailed(myDistantDevice);
							}


							return;
						}

					}


				}
			}

			//// IF we are a TAG device
			else if(_type == TAG) {
				// get message and parse
				if(messageType != _expectedMsgId) {
					// unexpected message, start over again
					//not needed ?
					return;
					_expectedMsgId = POLL_ACK;
					return;
				}
				if(messageType == POLL_ACK) {
					DW1000.getReceiveTimestamp(myDistantDevice->timePollAckReceived);
					//we note activity for our device:
					myDistantDevice->noteActivity();

					//in the case the message come from our last device:
					if(myDistantDevice->getIndex() == _networkDevicesNumber-1) {
						_expectedMsgId = RANGE_REPORT;
						//and transmit the next message (range) of the ranging protocole (in broadcast)
						transmitRange(nullptr);
					}
				}
				else if(messageType == RANGE_REPORT) {

					float curRange;
					memcpy(&curRange, data+1+SHORT_MAC_LEN, 4);
					float curRXPower;
					memcpy(&curRXPower, data+5+SHORT_MAC_LEN, 4);

					if (_useRangeFilter) {
						//Skip first range
						if (myDistantDevice->getRange() != 0.0f) {
							curRange = filterValue(curRange, myDistantDevice->getRange(), _rangeFilterValue);
						}
					}

					//we have a new range to save !
					myDistantDevice->setRange(curRange);
					myDistantDevice->setRXPower(curRXPower);


					//We can call our handler !
					//we have finished our range computation. We send the corresponding handler
					_lastDistantDevice = myDistantDevice->getIndex();
					if(_handleNewRange != 0) {
						(*_handleNewRange)();
					}
				}
				else if(messageType == RANGE_FAILED) {
					//not needed as we have a timer;
					return;
					_expectedMsgId = POLL_ACK;
				}
			} else {
				if (DEBUG)
					Serial.print(F("Unknown Message Type: "));
					Serial.println(messageType);
			}

			// The incoming message is not a TAG or an ANCHOR
			// else {
			// 	if(_handleUnknownMessage != 0) {
			// 		(*_handleUnknownMessage)(&data, DW1000.getDataLength());
			// 	}
			// }
		}

	}
}

void AsymmetricRangingClass::useRangeFilter(boolean enabled) {
	_useRangeFilter = enabled;
}

void AsymmetricRangingClass::setRangeFilterValue(uint16_t newValue) {
	if (newValue < 2) {
		_rangeFilterValue = 2;
	}else{
		_rangeFilterValue = newValue;
	}
}


/* ###########################################################################
 * #### Private methods and Handlers for transmit & Receive reply ############
 * ######################################################################### */


void AsymmetricRangingClass::handleSent() {
	// status change on sent success
	_sentAck = true;
}

void AsymmetricRangingClass::handleReceived() {
	// status change on received success
	_receivedAck = true;
}


void AsymmetricRangingClass::noteActivity() {
	// update activity timestamp, so that we do not reach "resetPeriod"
	_lastActivity = millis();
}

void AsymmetricRangingClass::resetInactive() {
	//if inactive
	if(_type == ANCHOR) {
		_expectedMsgId = POLL;
		receiver();
	}
	noteActivity();
}

void AsymmetricRangingClass::timerTick() {
	if(_networkDevicesNumber > 0 && counterForBlink != 0) {
		if(_type == TAG) {
			_expectedMsgId = POLL_ACK;
			//send a prodcast poll
			transmitPoll(nullptr);
		}
	}
	else if(counterForBlink == 0) {
		if(_type == TAG) {
			transmitBlink();
		}
		//check for inactive devices if we are a TAG or ANCHOR
		checkForInactiveDevices();
	}
	counterForBlink++;
	if(counterForBlink > 20) {
		counterForBlink = 0;
	}
}


void AsymmetricRangingClass::copyShortAddress(byte address1[], byte address2[]) {
	*address1     = *address2;
	*(address1+1) = *(address2+1);
}

/* ###########################################################################
 * #### Methods for ranging protocole   ######################################
 * ######################################################################### */

void AsymmetricRangingClass::transmitInit() {
	DW1000.newTransmit();
	DW1000.setDefaults();
}


void AsymmetricRangingClass::transmit(byte datas[]) {
	DW1000.setData(datas, LEN_DATA);
	DW1000.startTransmit();
}


void AsymmetricRangingClass::transmit(byte datas[], DW1000Time time) {
	DW1000.setDelay(time);
	DW1000.setData(data, LEN_DATA);
	DW1000.startTransmit();
}

void AsymmetricRangingClass::transmitBlink() {
	transmitInit();
	_globalMac.generateBlinkFrame(data, _currentAddress, _currentShortAddress);
	transmit(data);
}

void AsymmetricRangingClass::transmitRangingInit(DW1000Device* myDistantDevice) {
	// Executes the following code:
	// DW1000.newTransmit();
	// DW1000.setDefaults();
	transmitInit();

	//we generate the mac frame for a ranging init message
	_globalMac.generateLongMACFrame(data, _currentShortAddress, myDistantDevice->getByteAddress());
	//we define the function code
	data[LONG_MAC_LEN] = RANGING_INIT;

	// Copy the distance devices' short address to the _lastSentToShortAddress
	// field.
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());

	transmit(data);
}


// Transmit Poll Message Format:
// | Short Mac Frame Len |  1   |  2   |[ 3-4  |   5-6  ] X #dev
// |   Short Mac Frame   | POLL | #dev |[ addr | rplyTm ] X #dev
void AsymmetricRangingClass::transmitPoll(DW1000Device* myDistantDevice) {

	transmitInit();

	// DEBUGGING
	uint8_t len = 0;

	// If we have no distant device, then we want to transmit with a broadcast
	// instead.
	if(myDistantDevice == nullptr) {
		// Set our timer delay based on the number of devices in the network (so
		// that we do not overwhelm the network)
		_timerDelay = DEFAULT_TIMER_DELAY+(uint16_t)(_networkDevicesNumber*3*DEFAULT_REPLY_DELAY_TIME/1000);

		// Generate a short MAC frame witha broadcast as the destination address
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);

		// Set the message type
		data[SHORT_MAC_LEN]   = POLL;
		// Set the number of polling devices
		data[SHORT_MAC_LEN+1] = _networkDevicesNumber;

		// For each network device,
		for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
			// Calculate this device's reply time
			_networkDevices[i].setReplyTime((2*i+1)*DEFAULT_REPLY_DELAY_TIME);

			// Write the short address of this device to our output data
			memcpy(data+SHORT_MAC_LEN+2+4*i, _networkDevices[i].getByteShortAddress(), 2);

			// Write the reply time to the output data
			uint16_t replyTime = _networkDevices[i].getReplyTime();
			memcpy(data+SHORT_MAC_LEN+2+2+4*i, &replyTime, 2);

		}

		// DEBUGGING
		len = SHORT_MAC_LEN+1+2+4*_networkDevicesNumber;

		copyShortAddress(_lastSentToShortAddress, shortBroadcast);

	}

	// If we have defined a distant device, then send that device a specific
	// POLL
	else {
		// We redefine our default_timer_delay for just 1 device;
		_timerDelay = DEFAULT_TIMER_DELAY;

		// Generate a short mac frame with the distant devices' address
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());

		// Write the message type
		data[SHORT_MAC_LEN]   = POLL;

		// Write the number of polling devices
		data[SHORT_MAC_LEN+1] = 1;

		// Write the reply time
		uint16_t replyTime = myDistantDevice->getReplyTime();
		memcpy(data+SHORT_MAC_LEN+2, &replyTime, sizeof(uint16_t)); // todo is code correct?

		// DEBUGGING
		len = SHORT_MAC_LEN+1;

		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	}

	// DEBUG
	Serial.print(F("P1("));
	Serial.print(len);
	Serial.print(F(")-"));
	DW1000.printPrettyHex(data, len); // SHORT_MAC_LEN+2+2+4*_networkDevicesNumber

	// Transmit
	transmit(data);
}


void AsymmetricRangingClass::transmitPollAck(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = POLL_ACK;
	// delay the same amount as ranging tag
	DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());

	Serial.print(F("PAK ("));
	Serial.print(myDistantDevice->getByteShortAddress()[0],HEX);
	Serial.print(myDistantDevice->getByteShortAddress()[1],HEX);
	Serial.println(F(")"));

	transmit(data, deltaTime);
}

// Transmit Range Message Format:
// | Short Mac Frame Len |   1   |  2   |[ 3-4  |   5-6  ] X #dev
// |   Short Mac Frame   | RANGE | #dev |[ addr | rplyTm ] X #dev
void AsymmetricRangingClass::transmitRange(DW1000Device* myDistantDevice) {
	//transmit range need to accept broadcast for multiple anchor
	transmitInit();

	// uint8_t len = 0;

	// If the distant device was not defined, then this is a broadcast message.
	if(myDistantDevice == nullptr) {

		// We need to set our timerDelay:
		_timerDelay = DEFAULT_TIMER_DELAY+(uint16_t)(_networkDevicesNumber*3*DEFAULT_REPLY_DELAY_TIME/1000);

		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		data[SHORT_MAC_LEN]   = RANGE;
		//we enter the number of devices
		data[SHORT_MAC_LEN+1] = _networkDevicesNumber;

		// delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime     = DW1000Time(DEFAULT_REPLY_DELAY_TIME, DW1000Time::MICROSECONDS);
		DW1000Time timeRangeSent = DW1000.setDelay(deltaTime);

		for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
			//we write the short address of our device:
			memcpy(data+SHORT_MAC_LEN+2+17*i, _networkDevices[i].getByteShortAddress(), 2);


			//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
			_networkDevices[i].timeRangeSent = timeRangeSent;
			_networkDevices[i].timePollSent.getTimestamp(data+SHORT_MAC_LEN+4+17*i);
			_networkDevices[i].timePollAckReceived.getTimestamp(data+SHORT_MAC_LEN+9+17*i);
			_networkDevices[i].timeRangeSent.getTimestamp(data+SHORT_MAC_LEN+14+17*i);

		}

		copyShortAddress(_lastSentToShortAddress, shortBroadcast);

		// len = data+SHORT_MAC_LEN+14+17*_networkDevicesNumber - 1;

	}
	else {
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
		data[SHORT_MAC_LEN] = RANGE;
		// delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
		//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
		myDistantDevice->timeRangeSent = DW1000.setDelay(deltaTime);
		myDistantDevice->timePollSent.getTimestamp(data+1+SHORT_MAC_LEN);
		myDistantDevice->timePollAckReceived.getTimestamp(data+6+SHORT_MAC_LEN);
		myDistantDevice->timeRangeSent.getTimestamp(data+11+SHORT_MAC_LEN);
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());

		// len = 15;
	}

	// DEBUG
	Serial.print(F("TR ("));
	Serial.print(myDistantDevice->getByteShortAddress()[0],HEX);
	Serial.print(myDistantDevice->getByteShortAddress()[1],HEX);
	Serial.println(F(")"));

	transmit(data);
}


void AsymmetricRangingClass::transmitRangeReport(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_REPORT;
	// write final ranging result
	float curRange   = myDistantDevice->getRange();
	float curRXPower = myDistantDevice->getRXPower();
	//We add the Range and then the RXPower
	memcpy(data+1+SHORT_MAC_LEN, &curRange, 4);
	memcpy(data+5+SHORT_MAC_LEN, &curRXPower, 4);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());

	Serial.print(F("TRR ("));
	Serial.print(myDistantDevice->getByteShortAddress()[0],HEX);
	Serial.print(myDistantDevice->getByteShortAddress()[1],HEX);
	Serial.println(F(")"));

	transmit(data, DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS));
}

void AsymmetricRangingClass::transmitRangeFailed(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_FAILED;

	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());

	Serial.print(F("TRF ("));
	Serial.print(myDistantDevice->getByteShortAddress()[0],HEX);
	Serial.print(myDistantDevice->getByteShortAddress()[1],HEX);
	Serial.println(F(")"));

	transmit(data);
}

void AsymmetricRangingClass::receiver() {
	DW1000.newReceive();
	DW1000.setDefaults();
	// so we don't need to restart the receiver manually
	DW1000.receivePermanently(true);
	DW1000.startReceive();
}


/* ###########################################################################
 * #### Methods for range computation and corrections  #######################
 * ######################################################################### */


void AsymmetricRangingClass::computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF) {
	// asymmetric two-way ranging (more computation intense, less error prone)
	DW1000Time round1 = (myDistantDevice->timePollAckReceived-myDistantDevice->timePollSent).wrap();
	DW1000Time reply1 = (myDistantDevice->timePollAckSent-myDistantDevice->timePollReceived).wrap();
	DW1000Time round2 = (myDistantDevice->timeRangeReceived-myDistantDevice->timePollAckSent).wrap();
	DW1000Time reply2 = (myDistantDevice->timeRangeSent-myDistantDevice->timePollAckReceived).wrap();

	myTOF->setTimestamp((round1*round2-reply1*reply2)/(round1+round2+reply1+reply2));
	/*
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("timePollSent ");myDistantDevice->timePollSent.print();
	Serial.print("round1 "); Serial.println((long)round1.getTimestamp());

	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("timePollReceived ");myDistantDevice->timePollReceived.print();
	Serial.print("reply1 "); Serial.println((long)reply1.getTimestamp());

	Serial.print("timeRangeReceived ");myDistantDevice->timeRangeReceived.print();
	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("round2 "); Serial.println((long)round2.getTimestamp());

	Serial.print("timeRangeSent ");myDistantDevice->timeRangeSent.print();
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("reply2 "); Serial.println((long)reply2.getTimestamp());
	 */
}


/* FOR DEBUGGING*/
void AsymmetricRangingClass::visualizeDatas(byte datas[]) {
	char string[60];
	sprintf(string, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
					datas[0], datas[1], datas[2], datas[3], datas[4], datas[5], datas[6], datas[7], datas[8], datas[9], datas[10], datas[11], datas[12], datas[13], datas[14], datas[15]);
	Serial.println(string);
}



/* ###########################################################################
 * #### Utils  ###############################################################
 * ######################################################################### */

float AsymmetricRangingClass::filterValue(float value, float previousValue, uint16_t numberOfElements) {

	float k = 2.0f / ((float)numberOfElements + 1.0f);
	return (value * k) + previousValue * (1.0f - k);
}
