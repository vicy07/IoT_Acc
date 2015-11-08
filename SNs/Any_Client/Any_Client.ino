#include <BH1750.h>
#include <Wire.h>
#include <SPI.h>
#include "RF24.h"
#include "printf.h"
#include <dht.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>

//Validation for Sensors changes each 20 sec
#define DELAY 2000
//Control Values = 90 times
#define RESET_Interval 500
#define DHT_PIN 2
#define DHT_HEATING_PIN 2
#define PIR_PIN 4
#define RESET_PIN 3
#define SWITCH_CONTROL 5
#define INDICATION_PIN 8

#define DEBUG 0

const char timed_out[] PROGMEM = "Time out error!\r\n";
const char checksum_err[] PROGMEM = "Checksum error!\r\n";

int BH1750address = 0x23;
BH1750 LightSensor;

int NodeID;

// Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
RF24 radio(9,10);
//7,10
//
// Topology
//

// Radio pipe addresses for the 2 nodes to communicate.
                        //Writing Pipe   //Command Listening & Registration Pipe
const uint64_t pipes[2] PROGMEM = { 0xF0F0F0F0D2LL, 0xF0F0F0F0E1LL };

//
// state management
//
// Set up state.  This sketch uses the same software for all the nodes
// in this system.  Doing so greatly simplifies testing.  
//

// The various states supported by this sketch
typedef enum { state_ping_out = 1, state_pong_back = 2,  state_registration = 3 , state_initial = 4, receive_data = 5} state_e;

// The debug-friendly names of those states
const char* state_friendly_name[] = { "invalid", "Ping out", "Pong back", "Registration", "Initial State", "Receiving Data State"};

// The state of the current running sketch
//state_e state = state_ping_out;
state_e state = state_initial;
dht DHT;


int m_temp;
int m_humd;
uint16_t m_lux;
int m_pir;
int m_DHT_sensor_type;
int iterations_count;

void setup(void)
{ 
  Serial.begin(9600);
  LightSensor.begin(BH1750_CONTINUOUS_HIGH_RES_MODE);
  
  printf_begin();
  Serial.print(F("\n\rRF24/examples/GettingStarted/\n\r"));
  Serial.print(F("STATE: "));
  printf("%s\n\r",state_friendly_name[state]);
  Serial.print(F("*** PRESS 'T' to begin transmitting to the other node\n\r"));

  radio.begin();
  radio.setPayloadSize(32);
  radio.setChannel(0x4c);  
  radio.setPALevel(RF24_PA_MAX);
  

  if (radio.setDataRate(RF24_250KBPS))
    Serial.print(F("RF24_250KBPS has been set...\r\n"));
  else
    Serial.print(F("Failed with RF24_250KBPS setting...\r\n"));
  radio.setAutoAck(true);
  radio.setRetries(15, 15);
    
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1,pipes[1]);
  radio.startListening();
  radio.printDetails();

  NodeID=(int)EEPROM_readlong(0);
  printf("DeviceId=%03i - Read from Memory\r\n", NodeID);
  
  pinMode(PIR_PIN, INPUT);  
  digitalWrite(PIR_PIN, HIGH);
  
  pinMode(SWITCH_CONTROL, OUTPUT);
  digitalWrite(SWITCH_CONTROL, LOW);  
  
//  pinMode(RESET_PIN, INPUT);
//  EEPROM_writelong(0,21);
//  pinMode(RESET_PIN, INPUT_PULLUP);
//  digitalWrite(RESET_PIN, LOW);
  pinMode(INDICATION_PIN, OUTPUT);

  randomSeed(analogRead(0));
  
  resetMeasurement();
}

#define MAX_RETRY_COUNT 5
#define RETRY_TIMEOUT 500

void loop(void)
{
  bool ok;
  char a[35];
  char abc[33];
  
  if ( state == state_initial )
  {
    radio.stopListening();
    if(NodeID)
    {
      Serial.print(F("Trying with NodeID restored from EEPROM\r\n"));
      state = state_ping_out;
    }
    else
    {
      state = state_registration;
    }
  }

  if ( state == state_registration )
  {
      rest();

      int handshakeID;
      handshakeID = (int)random(998);
        
      Serial.print(F("\n\r"));
      Serial.print(F("-----------------------------------\n\r"));
      
      sprintf(a, "?");  
      sprintf(a + strlen(a), "r_v01_%03i", handshakeID);
      Serial.print(F("Registration: Request "));
      printf("%s:", a);
      
      bool mineIDConfirmation = false;
      while (!mineIDConfirmation)
      {
        int retrycount = 0;
        
        rest();
        delay(RETRY_TIMEOUT);
        
        if (radio.write(a, sizeof(a)))
          Serial.print(F("ok."));
        else
        {
          Serial.print(F("."));
          continue;
        }

        Serial.print(F("\n\r"));

        radio.startListening();
      
        Serial.print(F("Starting registartion with ID:"));
        printf("%i\r\n", handshakeID);
        
        while (!radio.available())
        {        
          delay(RETRY_TIMEOUT);
          if(++retrycount > MAX_RETRY_COUNT)
            break;
          Serial.print(F("Registration: NOTHING received "));
          printf("(%i out of %i) \n\r", retrycount, MAX_RETRY_COUNT);
        }
      
        if(retrycount > MAX_RETRY_COUNT)
        {
          Serial.print(F("Let's go for another registration request round\r\n"));
          continue;
        }
        
        // Fetch the payload, and see if this was the last one.
        
        radio.read( abc, sizeof(abc) );
        Serial.print(F("Received: "));
        printf("%s\r\n", abc);
        
        // Dump the payloads until we've got everything
        unsigned long deviceIdReceived = atoi(abc);
  
        // Spew it
        if ((int)handshakeID==(int)deviceIdReceived)
        {
           /////NB assumption that we will use 4 bytes for data in eprom
           EEPROM_writelong(0,deviceIdReceived);
           NodeID=deviceIdReceived;
           
           Serial.print(F("Registration: Mine Confirmation received "));
           printf("%lu. \n\r",deviceIdReceived);
           
           mineIDConfirmation = true;           
           
           state = state_ping_out;
           break;
        }
        else
        {
           Serial.print(F("Registration: Someone's confirmation received "));
           printf("%lu. \n\r",deviceIdReceived);
           state = state_initial;
        }
      }
      // First, stop listening so we can talk
      radio.stopListening();          
  }

  if (state == state_ping_out)
  {
    Serial.print(F("Starting Data Send Procedure.\r\n"));
    // First, stop listening so we can talk.
    rest();

    dealWithPIRData(a, sizeof(a));            
    rest();

    dealWithLuxData(a, sizeof(a));
    rest();
 
    if(checkDHT(DHT_PIN) == DHTLIB_OK)
    {      
      dealWithHumData(a, sizeof(a));
      rest();

      dealWithTempData(a, sizeof(a));
      rest();      
    }

    // Now, continue listening
    state = receive_data;
    
  }//if (state == state_ping_out)
  

  rest();  
  if (state == receive_data)
  {
    int retrycount = 0;
    radio.startListening();
  
    int msg[32];
    Serial.print(F("Starting Receiving.\r\n"));
    while (!radio.available())
    {        
      delay(RETRY_TIMEOUT);
      if(++retrycount > MAX_RETRY_COUNT)
        break;
      Serial.print(F("Received: NOTHING "));
      printf("(%i out of %i) \n\r", retrycount, MAX_RETRY_COUNT);
    }
  
    if(retrycount > MAX_RETRY_COUNT)
    {
      Serial.print(F("Let's go for another data transmission cycle.\r\n"));
      state = state_ping_out;
    }
    else
    {
      // Expect to read command
      radio.read( msg, sizeof(abc) );
      printf("Received: %s ", msg);

      //GET First 3 as SN ID
      char target_device[3]; 
      memcpy(target_device, msg + 0 /* Offset */, 3 /* Length */);
      target_device[3] = 0; /* Add terminator */
      int target_device_ID;
      sscanf(target_device, "%d", &target_device_ID);
      
      if (target_device_ID == NodeID)
      {
         printf("- Mine (SN=%i), start to process\r\n", target_device_ID); 

         printf("msg now: %s:", msg);
         char target_command[32-4]; 
         memcpy(target_command, msg + 2 /* Offset */, 32-4 /* Length */);
         target_command[32-4] = 0; /* Add terminator */
       
         actuatorCommand(target_command);

         // First, stop listening so we can talk
         radio.stopListening();
         Serial.print(F("Sending Confirmation response: "));
         // Send the final one back.
         sprintf(a, "%03i", NodeID);
         sprintf(a + strlen(a), "_ack_%s;",msg);
         printf("%s:", a);
         sendMessage(a, sizeof(a));
  
         // Now, resume listening so we catch the next packets.
         radio.startListening();
      }
      else
      {
         printf("- Not mine, but for SN=%i\r\n", target_device_ID); 
      }

      state = state_ping_out;         
    }
   
  }
    
  delay(DELAY);
  
  iterations_count++;
  if (iterations_count>=RESET_Interval)
  {
     resetMeasurement();
  }
  
}

int checkDHT(int pin)
{
  int chk;

  ///If not clear sensor type validate it    
  if (m_DHT_sensor_type==-1)
  {
    chk = DHT.read22(pin);
    if (chk == DHTLIB_OK) m_DHT_sensor_type=1;
    
    chk = DHT.read11(pin);
    if (chk == DHTLIB_OK) m_DHT_sensor_type=0;
  }
  
  ///If Type is clear read it properly
  switch(m_DHT_sensor_type)
  {
    case 1: 
       chk = DHT.read22(pin);
       break;
    default:
       chk = DHT.read11(pin);
       break;
  };

  ///If Type is clear and reading failed report error
  switch (chk)
  {
    case DHTLIB_OK:  
      Serial.print(F("DHT Sensor is OK.\r\n")); 
      break;
    case DHTLIB_ERROR_CHECKSUM: 
      printf("%s", checksum_err); 
      break;
    case DHTLIB_ERROR_TIMEOUT: 
      printf("%s", timed_out); 
      break;
    default: 
      Serial.print(F("Unknown error!\r\n")); 
      break;
  }
  return chk;
}

int dealWithHumData(char* a, unsigned int aLen)
{
  int value = (int)DHT.humidity;
  sprintf(a, "%03i", NodeID);
  sprintf(a + strlen(a), "_h_%02i;", value);
  Serial.print(F("Now sending \"HumData\": "));
  printf("%s:", a);
    
  if (m_humd != value)  
  {
      sendMessage(a, aLen);
      
      m_humd = value;
  }    
  else
  {
     Serial.print(F("No changes.\r\n"));
  }
  
  return value;    
}

int dealWithTempData(char* a, unsigned int aLen)
{
  int value = (int)DHT.temperature;
  sprintf(a, "%03i", NodeID);
  sprintf(a + strlen(a), "_t_%02i;", value);
  Serial.print(F("Now sending \"TempData\": "));
  printf("%s:", a);
    
  if (m_temp != value)  
  {
      sendMessage(a, aLen);
      
      m_temp = value;      
  }
  else
  {
     Serial.print(F("No changes.\r\n"));
  }
  
  return value;
}

uint16_t dealWithLuxData(char* a, unsigned int aLen)
{
  uint16_t value = LightSensor.readLightLevel();
  sprintf(a, "%03i", NodeID);
  sprintf(a + strlen(a), "_l_%i;", value);
  Serial.print(F("Now sending "));
  printf("%s:", a);
    
  if (m_lux != value)  
  {
    if ((value>=0)&&(value<=5000))
    {
        sendMessage(a, aLen);
    }
    else
    {
        Serial.print(F("Not Connected.\n\r")); 
    }
    
    m_lux = value;      
  }
  else
  {
     Serial.print(F("No changes.\r\n"));
  }
  
  return value;
}

bool dealWithPIRData(char* a, unsigned int aLen)
{
  int value = digitalRead(PIR_PIN);
  sprintf(a, "%03i", NodeID);
  sprintf(a + strlen(a), "_p_%01i;", value);
  Serial.print(F("Now sending "));
  printf("%s:", a);

  digitalWrite(INDICATION_PIN, value); 

#if (DEBUG!=1)  
  if (m_pir != value)  
#endif
  {
    sendMessage(a, aLen);
      
    m_pir = value;            
  }
#if (DEBUG!=1) 
  else
  {
     Serial.print(F("No changes.\r\n"));
  }
#endif

  return value;
}

void rest()
{
  radio.startListening();
  radio.stopListening();
}

int sendMessage(const void *buf, uint8_t aLen)
{
    while (!radio.write(buf, aLen))
    {
        Serial.print(F("."));
        rest();
        delay(DELAY);
    }
    Serial.print(F("ok.\r\n"));
}

void resetMeasurement()
{
  m_temp = -1;
  m_humd = -1;
  m_lux = -1;
  m_pir = -1;
  iterations_count = 0;
  m_DHT_sensor_type = -1;
  
  Serial.print(F("Refresh values\n\r"));
}

  // read double word from EEPROM, give starting address
unsigned long EEPROM_readlong(int address) 
{
  //use word read function for reading upper part
  unsigned long dword = EEPROM_readint(address);
  //shift read word up
  dword = dword << 16;
  // read lower word from EEPROM and OR it into double word
  dword = dword | EEPROM_readint(address+2);
  return dword;
}

//write word to EEPROM
void EEPROM_writeint(int address, int value) {
  EEPROM.write(address,highByte(value));
  EEPROM.write(address+1 ,lowByte(value));
}
  
  //write long integer into EEPROM
void EEPROM_writelong(int address, unsigned long value) {
  //truncate upper part and write lower part into EEPROM
  EEPROM_writeint(address+2, word(value));
  //shift upper part down
  value = value >> 16;
  //truncate and write
  EEPROM_writeint(address, word(value));
}

unsigned int EEPROM_readint(int address) {
  unsigned int word = word(EEPROM.read(address), EEPROM.read(address+1));
  return word;
}


int commandSize(char *ptr)
{
    //variable used to access the subsequent array elements.
    int offset = 0;
    //variable that counts the number of elements in your array
    int count = 0;

    //While loop that tests whether the end of the array has been reached
    while (*(ptr + offset) != '\0')
    {
        //increment the count variable
        ++count;
        //advance to the next element of the array
        ++offset;
    }
    //return the size of the array
    return count;
}

void actuatorCommand(char* command)
{
   printf("   Execute Command: %s\r\n", command); 
 
   if (strcmp(command, "SwitchOff") == 0)
   {
      digitalWrite(SWITCH_CONTROL, LOW);
      printf("    c_Switch Off\r\n"); 
   }
   
   if (strcmp(command, "SwitchOn") == 0)
   {
      digitalWrite(SWITCH_CONTROL, HIGH);
      printf("    c_Switch On\r\n");       
   }
   
   if (strcmp(command, "Reinitialize") == 0)
   {
      resetMeasurement();
      printf("    c_Reset\r\n");       
   }

}
