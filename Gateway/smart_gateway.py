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

import RPi.GPIO as GPIO

from azure.servicebus import ServiceBusService, Message, Queue
##############################
#   Compute secured Hash     #
##############################
def ComputeHash(timeStamp, key):
    message = bytes(timeStamp).encode('utf-8')
    secret  = bytes(key).encode('utf-8')
    signature = base64.b64encode(hmac.new(message, secret, digestmod=hashlib.sha256).digest())
    return signature


##############################
# Send Measurements to cloud #
##############################
def sendMeasure(config_data, now_, measure_type, measure_value, deviceId, debugMode=0):    

    measures = { "t": "1", "h": 2, "p": "7", "l":"6", "live":"32" }

    href = config_data["Server"]["url"] + 'api/events/process'
    token = ComputeHash(now_, config_data["Server"]["key"])
    authentication = config_data["Server"]["id"] + ":" + token

    if debugMode == 1: print(authentication)
    
    headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': now_, 'Authentication': authentication}
    measurements = []    

    measure = {}
    measure["EventType"] = measures[measure_type]
    measure["EventValue"] = int(measure_value)
    measure["EventTime"] = now_
    measurements.append(measure)

    if debugMode == 1: print measurements

    payload = {'events': measurements, "deviceId": deviceId}
    if debugMode == 1: print(json.dumps(payload))
#    r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)
    r = requests.post(href, headers=headers, data=json.dumps(payload))
    if debugMode == 1: print (r)
    else: print '-> ' + str(r.status_code)





def main(argv):
   print '##################################################################'
   print '#                         NRF24 gateway                          #'
   print '##################################################################'


   # Wait while internet appear
   import urllib2 
   loop_value = 1
   while (loop_value < 10):
      try:
           urllib2.urlopen("http://google.com")
      except:
           print( "Network: currently down." )
           time.sleep( 10 )
           loop_value = loop_value + 1
      else:
           print( "Network: Up and running." )
           loop_value = 10


   pipes = [[0xf0, 0xf0, 0xf0, 0xf0, 0xd2], [0xf0, 0xf0, 0xf0, 0xf0, 0xe1]]
   configFileName = os.path.dirname(os.path.abspath(__file__)) + '/config.json'
   debugMode = 0

   try:
      opts, args = getopt.getopt(argv,"hd:f:",["debug=","configFile="])
   except getopt.GetoptError:
      print 'smart_gateway.py -d <debugMode:0/1>'
      sys.exit(2)
   for opt, arg in opts:
      if opt == '-h':
         print 'Example for call it in debug mode: smart_gateway.py -d 1 -f config.json'
         sys.exit()
      elif opt in ("-d", "--debug"):
         debugMode = arg
      elif opt in ("-f", "--configFile"):
         configFileName = arg

   print 'Start Parameters:'
   print '       debugMode:', debugMode
   print '  configFileName:', configFileName 

   json_data=open(configFileName)
   config_data = json.load(json_data)
   json_data.close()

   print '      Server URL:', config_data["Server"]["url"]
   print '      Company ID:', config_data["Server"]["id"]
   print '      Gateway ID:', config_data["Server"]["Deviceid"]
   print '     Service Bus:', config_data["Servicebus"]["namespace"]

   queue_name = 'custom_' + config_data["Server"]["id"] + '_' + config_data["Server"]["Deviceid"]
   bus_service = ServiceBusService( service_namespace=config_data["Servicebus"]["namespace"], 
                                    shared_access_key_name=config_data["Servicebus"]["shared_access_key_name"], 
                                    shared_access_key_value=config_data["Servicebus"]["shared_access_key_value"])
   try:
      bus_service.receive_queue_message(queue_name, peek_lock=False)
      print '  Actuator queue: ' + queue_name
   except:
      queue_options = Queue()
      queue_options.max_size_in_megabytes = '1024'
      queue_options.default_message_time_to_live = 'PT15M'
      bus_service.create_queue(queue_name, queue_options)
      print '  Actuator queue: ' + queue_name + ' (Created)'



   print ''
   nowPI = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")

   href = config_data["Server"]["url"] + 'API/Device/GetServerDateTime'
   token = ComputeHash(nowPI, config_data["Server"]["key"])
   authentication = config_data["Server"]["id"] + ':' + token
   headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Authentication': authentication}
   #r = requests.get(href, headers=headers, verify=False)
   r = requests.get(href, headers=headers)
   if r.status_code == 200:
       nowPI = r.json()
       print ("Setting up time to: " + nowPI)
       os.popen('sudo -S date -s "' + nowPI + '"', 'w').write("123")
   else:
       print 'Error in setting time. Server response code: %i' % r.status_code

   href = config_data["Server"]["url"] + 'api/Device/DeviceConfigurationUpdate'
   token = ComputeHash(nowPI, config_data["Server"]["key"])
   authentication = config_data["Server"]["id"] + ":" + token

   if debugMode==1: print(authentication)
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
   if debugMode == 1: print 'Request Content: {0}'.format(json.dumps(payload))
   #r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)
   r = requests.post(href, headers=headers, data=json.dumps(payload))


   if r.status_code == 200:
      if debugMode == 1: print 'Configuration Response Content: {0}'.format(r.content)
      data = json.loads(r.text)    
      for entry in data['Device']['DeviceConfigurations']:
          if entry['Key'] == 'Configuration':     
             with open(configFileName, 'w') as outfile:
                json.dump(json.loads(entry['Value']), outfile)
      print 'Device configuration Successfully updated'
   else:
      print 'Error in setting time. Server response code: {0} {1}'.format(r.status_code, r.content)


   GPIO.setwarnings(False)

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

   print ''
   print 'NRF24 Module Configuration Details:'
   radio.printDetails()
   radio.startListening()

   print ''
   while True:
       pipe = [0]
       cloudCommand = ''
       while not radio.available(pipe, True):
           time.sleep(1)

       recv_buffer = []
       radio.read(recv_buffer)
       out = ''.join(chr(i) for i in recv_buffer)
 
       if out.find(';')>0:
          out = out.split(';')[0]
          print out,

          temp =out.split("_")
          if debugMode == 1: print (temp)
    
          if temp[0] in config_data["Devices"]:
             nowPI = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
             sendMeasure(config_data, nowPI, temp[1], temp[2], config_data["Devices"][temp[0]], debugMode)

             print config_data["Server"]["Deviceid"] + '_live_1',
             sendMeasure(config_data, nowPI, 'live', 1, config_data["Server"]["Deviceid"], debugMode)
          else:
             print '-> ignore'

       if queue_name <> '':
          cloudCommand = bus_service.receive_queue_message(queue_name, peek_lock=False)
          if cloudCommand:
                print '-> ' + str(cloudCommand.body)
          else:
                print '-> ignoring'

if __name__ == "__main__":
   main(sys.argv[1:])