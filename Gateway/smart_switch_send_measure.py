#!/usr/bin/python

# Modules required to me installed: 
#   sudo pip install requests
#   sudo pip install requests[security]

import time
from time import gmtime, strftime

import os
import sys, getopt
import json
import requests
import hashlib
import hmac
import base64
from decimal import *
from time import gmtime, strftime
from datetime import datetime
from datetime import timedelta

import logging
logging.basicConfig(level=logging.ERROR, filename=os.path.dirname(os.path.abspath(__file__)) + '/last_error.log')

def main(argv):

   # Validate for Input parameters
   try:
      opts, args = getopt.getopt(argv,"d:v:m:",["deviceid=","measurevalue=", "measuretype="])
   except getopt.GetoptError:
      print 'smart_switch_send_measure.py -d <deviceid> -v <measurevalue> -m <measuretype>'
      sys.exit(2)
   for opt, arg in opts:
      if opt == '-h':
         print 'Example for reporting call for device 4849 about 17 users connected: smart_switch_send_measure.py -d 4849 -v 17 -m 1045'
         sys.exit()
      elif opt in ("-d", "--deviceid"):
         deviceId = arg
      elif opt in ("-v", "--measurevalue"):
         measured_value = arg
      elif opt in ("-m", "--measuretype"):
         measure_type = arg

   print 'Start Parameters:'
   print '       Device Id:', deviceId
   print '    Measure Type:', measure_type 
   print '  Measured Value:', measured_value 
   now_ = (datetime.now()-timedelta(hours=3)).strftime("%Y-%m-%dT%H:%M:%S")
   print '    Current Time:', now_

   href = 'https://smartoffice.accenture.lv/api/events/process'
   token = '4ZWRC-LPAGM-HLE6J-98CBK'
   authentication = "1:" + token

   headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': now_, 'Authentication': authentication}
   measurements = []    

   measure = {}
   measure["EventType"] = measure_type
   try:
       measure["EventValue"] = int(measured_value)
   except:
       measure["EventValue"] = 0
        
   measure["EventTime"] = now_
   measurements.append(measure)

   print measurements

   payload = {'events': measurements, "deviceId": deviceId}
   print(json.dumps(payload))
    
   #response = session.post(href, headers=headers, data=json.dumps(payload))
   response = requests.post(href, headers=headers, data=json.dumps(payload), verify=True)
   print 'C: Send to DeviceId=' + deviceId + ' value=' + str(measure["EventValue"]) + ' Result Code=' + str(response.status_code)

if __name__ == "__main__":
   try:
      main(sys.argv[1:])
   except:
      logging.exception(datetime.now().strftime("%Y-%m-%dT%H:%M:%S"))
