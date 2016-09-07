#!/usr/bin/python
# Validaet only reading from queue

import time

import os
import sys, getopt
import json
import hashlib
import hmac
import base64
from decimal import *
from time import gmtime, strftime
from datetime import datetime

from azure.servicebus import ServiceBusService, Message, Queue
from requests_futures.sessions import FuturesSession
import logging
logging.basicConfig(level=logging.ERROR, filename=os.path.dirname(os.path.abspath(__file__)) + '/last_error.log')


def main(argv):
   print '##################################################################'
   print '#           Test Azure Service bus queue connection              #'
   print '##################################################################'

   configFileName = os.path.dirname(os.path.abspath(__file__)) + '/config.json'

   print 'Start Parameters:'
   print '  configFileName:', configFileName 

   json_data=open(configFileName)
   config_data = json.load(json_data)
   json_data.close()

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
	   
   # List to hold all commands that was send no ACK received
   localCommandSendAckWaitList = []
   while True:
      cloudCommand = bus_service.receive_queue_message(queue_name, peek_lock=False)

      if cloudCommand.body is not None:
          stringCommand = str(cloudCommand.body)
          print 'C: Received "' + stringCommand + '" => ',

          #Tranlate External/Cloud ID to local network ID 
          temp = stringCommand.split("-")
          #print 'stringCommand.split = ', temp
          localNetworkDeviceID = config_data["Devices"].keys()[config_data["Devices"].values().index(temp[0])]
          print 'for DeviceId=' + localNetworkDeviceID

      time.sleep(10)

if __name__ == "__main__":
   try:
      main(sys.argv[1:])
   except:
      logging.exception(datetime.now().strftime("%Y-%m-%dT%H:%M:%S"))
