#!/usr/bin/python
# raspberry pi nrf24l01 hub
# more details at http://blog.riyas.org
# Credits to python port of nrf24l01, Joao Paulo Barrac & maniacbugs original c library

from nrf24 import NRF24
import time
from time import gmtime, strftime

import os
import sys, getopt
import json
import requests

import hashlib
import hmac
import base64
import serial
from decimal import *
from time import gmtime, strftime
from datetime import datetime
import socket
from uuid import getnode as get_mac

def ComputeHash(timeStamp, key):
    message = bytes(timeStamp).encode('utf-8')
    secret  = bytes(key).encode('utf-8')
    signature = base64.b64encode(hmac.new(message, secret, digestmod=hashlib.sha256).digest())
    print (signature)
    return signature

pipes = [[0xf0, 0xf0, 0xf0, 0xf0, 0xd2], [0xf0, 0xf0, 0xf0, 0xf0, 0xe1]]
configFileName = 'config.json'

json_data=open(configFileName)
config_data = json.load(json_data)
json_data.close()

nowPI = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")

href = config_data["Server"]["url"] + 'API/Device/GetServerDateTime'
token = ComputeHash(nowPI, config_data["Server"]["key"])
authentication = config_data["Server"]["id"] + ":" + token
headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Authentication': authentication}
print 'Server side URL:' + href
r = requests.get(href, headers=headers, verify=False)
if r.status_code == 200:
    nowPI = r.json()
    print ("Setting up time to: " + nowPI)
    os.popen('sudo -S date -s "' + nowPI + '"', 'w').write("123")
else:
    print 'Error in setting time. Server response code: %i' % r.status_code

href = config_data["Server"]["url"] + 'api/Device/DeviceConfigurationUpdate'
token = ComputeHash(nowPI, config_data["Server"]["key"])
authentication = config_data["Server"]["id"] + ":" + token
print(authentication)
headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': nowPI, 'Authentication': authentication}
    
deviceDetail = {}
deviceDetail["DeviceIdentifier"] = config_data["Server"]["Deviceid"]
deviceDetail["DeviceType"] = "Custom"
deviceDetail["DeviceConfigurations"] = [{'Key':'IPPrivate','Value':[(s.connect(('8.8.8.8', 80)), s.getsockname()[0], s.close()) for s in [socket.socket(socket.AF_INET, socket.SOCK_DGRAM)]][0][1]},
                                        {'Key':'IPPublic','Value': requests.get('http://icanhazip.com/').text},
                                        {'Key': 'Configuration', 'Value': json.dumps(config_data) },
                                        {'Key':'MAC','Value': ':'.join(("%012X" % get_mac())[i:i+2] for i in range(0, 12, 2))}
                                       ]

payload = {'Device': deviceDetail}
print 'Request Content: {0}'.format(json.dumps(payload))
r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)

if r.status_code == 200:
   print 'Response Content: {0}'.format(r.content)
   data = json.loads(r.text)    
   for entry in data['Device']['DeviceConfigurations']:
       if entry['Key'] == 'Configuration':     
          with open(configFileName, 'w') as outfile:
             json.dump(json.loads(entry['Value']), outfile)
   print 'Device configuration Successfully updated'
else:
   print 'Error in setting time. Server response code: {0} {1}'.format(r.status_code, r.content)





def setAlarmState(config_data, now_, temper, humi, luxi, deviceId, move=0):    
    href = config_data["Server"]["url"] + 'api/events/process'
    token = ComputeHash(now_, config_data["Server"]["key"])
    authentication = config_data["Server"]["id"] + ":" + token
    print(authentication)
    
    headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': now_, 'Authentication': authentication}
    measurements = []    
    if temper != "":
        temp = {}
        temp["EventType"] = 1
        temp["EventValue"] = int(temper)
        temp["EventTime"] = now_
        measurements.append(temp)

    if humi != "":
        hum = {}
        hum["EventType"] = 2
        hum["EventValue"] = int(humi)
        hum["EventTime"] = now_
        measurements.append(hum)

    if move != "":
        movement = {}
        movement["EventType"] = 7
        movement["EventValue"] = int(move)
        movement["EventTime"] = now_
        measurements.append(movement)

    if luxi != "":
        lux = {}
        lux["EventType"] = 6
        lux["EventValue"] = int(luxi)
        lux["EventTime"] = now_
        measurements.append(lux)

    #measurements = [{"EventType":7,"EventValue":temp,"EventTime":now_},{"EventType":6,"EventValue":hum,"EventTime":now_},{"EventType":1,"EventValue":movement,"EventTime":now_}]

    print measurements

    payload = {'events': measurements, "deviceId": deviceId}
    print(json.dumps(payload))
    r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)
    print (r)




radio = NRF24()
radio.begin(0, 0, 25, 18) #set gpio 25 as CE pin
radio.setRetries(15,15)
radio.setPayloadSize(32)
radio.setChannel(0x4c)
radio.setDataRate(NRF24.BR_250KBPS)
radio.setPALevel(NRF24.PA_MAX)
radio.setAutoAck(1)
radio.openWritingPipe(pipes[1])
radio.openReadingPipe(1, pipes[0])

radio.startListening()
radio.stopListening()

radio.printDetails()
radio.startListening()

while True:
    pipe = [0]
    while not radio.available(pipe, True):
        time.sleep(1)
    recv_buffer = []
    radio.read(recv_buffer)
    out = ''.join(chr(i) for i in recv_buffer)
 
    if out.find(';')>0:
       out = out.split(';')[0]
       print out

       temp =out.split("_")
       print (temp)
    
       nowPI = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")

       #Regular data message
       hum = ''
       if temp[1] == "h":
           hum = temp[2]
           print "Hum: ", hum

       temperature = ''        
       if temp[1] == "t":
           temperature = temp[2]
           print "Temp: ", temperature

       movement = ''        
       if temp[1] == "p":
           movement = temp[2]
           print "PIR: ", movement

       lux = ''          
       if temp[1] == "l":
           lux = temp[2]
           print "Lux: ", lux
    
       setAlarmState(config_data, nowPI, temperature, hum, lux, config_data["Devices"][temp[0]], movement)

       href = config_data["Server"]["url"] + 'api/events/process'
       token = ComputeHash(nowPI, config_data["Server"]["key"])
       authentication = config_data["Server"]["id"] + ":" + token
       print(authentication)
    
       headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': nowPI, 'Authentication': authentication}
       measurements = []    

       measure = {}
       measure["EventType"] = 32
       measure["EventValue"] = 1
       measure["EventTime"] = nowPI
       measurements.append(measure)
       
       print measurements

       payload = {'events': measurements, "deviceId": config_data["Server"]["Deviceid"]}
       print(json.dumps(payload))
       r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)
       print (r)
