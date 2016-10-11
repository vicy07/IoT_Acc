#include <SPI.h>
#include "RF24.h"
#include "printf.h"
#include <dht.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>

#define DEBUG 0

//Validation for Sensors changes each 20 sec
#define DELAY 500
#define REFRESH_Interval 500

#define RESET_PIN 7
#define SWITCH_CONTROL 2
#define INDICATION_PIN 4

int NodeID;

const char timed_out[] PROGMEM = "Time out error!\r\n";
const char checksum_err[] PROGMEM = "Checksum error!\r\n";

// Define sensors connection to pins
// TODO: Move it to EPROM to make code fully independent
// v0.3
   int sensors_DHT_connection_bit = 256 * B0000000 + B0000010;
   int sensors_PIR_connection_bit = 256 * B0000000 + B0001000;
int sensors_SWITCH_connection_bit = 256 * B0000000 + B0010000;
   int sensors_LED_connection_bit = 256 * B0000001 + B0000000;

// v0.9
//   int sensors_DHT_connection_bit = 256 * B0000000 + B0100000;
//   int sensors_PIR_connection_bit = 256 * B0000000 + B0000100;
//int sensors_SWITCH_connection_bit = 256 * B0000000 + B0000010;
//   int sensors_LED_connection_bit = 256 * B0000000 + B0001000;


// Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
RF24 radio(9,10);

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
int m_DHT_sensor_type = -1;  //0 - 11, 1 - 21, 2 - 22

int m_temp;
int m_heating_temp;
int m_humd;
uint16_t m_lux;
int m_pir;
int m_switch;
int iterations_count;

void setup(void)
{ 
  Serial.begin(115200);
  
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

//  EEPROM_writelong(0,99);
  NodeID=(int)EEPROM_readlong(0);
  printf("DeviceId=%03i - Read from Memory\r\n", NodeID);
  
  //All Sensors and INPUT PINS define
  for (int n=1; n<=16; ++n)
  {
    if ((sensors_DHT_connection_bit|sensors_PIR_connection_bit) & (1<<(n-1))) 
    { 
      pinMode(n, INPUT);
      digitalWrite(n, LOW);      
    }
  }

  //All Sensors and OUTPUT PINS define
  for (int n=1; n<=16; ++n)
  {
    if ((sensors_LED_connection_bit|sensors_SWITCH_connection_bit) & (1<<(n-1))) 
    { 
      pinMode(n, OUTPUT);
      digitalWrite(n, LOW);      
    }
  }
  
  randomSeed(analogRead(0));
  
  printf("DHT LIBRARY VERSION: %s \r\n", DHT_LIB_VERSION);
  for (int n=1; n<=16; ++n)
  {
    if ((sensors_DHT_connection_bit) & (1<<(n-1))) 
    {   
       int chk = DHT.read11(n);
       if (chk == DHTLIB_OK)
       {
          m_DHT_sensor_type = 0;
       }
       
       chk = DHT.read21(n);
       if (chk == DHTLIB_OK)
       {
         m_DHT_sensor_type = 1;
       }
       
       chk = DHT.read22(n);
       if (chk == DHTLIB_OK)
       {
         m_DHT_sensor_type = 2;
       }
       
       printf("DHT Sensor Type: %01i connected to pin %02i \r\n", m_DHT_sensor_type, n);
    }
  }
  
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
  
  if (digitalRead(RESET_PIN) == HIGH)
  {
      int count=0;
      printf("Reset countdown: ", count);
      while (digitalRead(RESET_PIN) == HIGH) 
      {
         count+=1; 
         if (count % 2)
         {
           digitalWrite(INDICATION_PIN, HIGH); 
         }
         else
         {
           digitalWrite(INDICATION_PIN, LOW); 
         }

         if (count >= 10)
         {
           digitalWrite(INDICATION_PIN, LOW); 
         }  
         delay(DELAY);
         printf(".");
      }

      printf(":Final Count: %02i\n\r", count);      
      if (count >= 10)
      {
         printf("-----------------------------------------------------\n\r");
         printf("              Reinitialization initiated             \n\r");      
         printf("-----------------------------------------------------\n\r");
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
      
      sprintf(a, "???");  
      sprintf(a + strlen(a), "_v02_%03i;", handshakeID);
      Serial.print(F("Registration: Request "));
      printf("%s:", a);
      sendMessage(a, sizeof(a));
      
      bool mineIDConfirmation = false;
      while (!mineIDConfirmation)
      {
        int retrycount = 0;
        radio.startListening();
      
        Serial.print(F("Waiting registartion ack with ID:"));
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
        
        
        char handshakeConfirmation_raw[3]; 
        memcpy(handshakeConfirmation_raw, abc + 8 /* Offset */, 3 /* Length */);
        handshakeConfirmation_raw[3] = 0; /* Add terminator */
        printf("RECEIVED CONFIRMATION:%s\r\n", handshakeConfirmation_raw);
        int handshakeConfirmation;
        sscanf(handshakeConfirmation_raw, "%d", &handshakeConfirmation);        

        char deviceIdReceived_raw[3]; 
        memcpy(deviceIdReceived_raw, abc + 12 /* Offset */, 3 /* Length */);
        deviceIdReceived_raw[3] = 0; /* Add terminator */
        printf("RECEIVED Local ID:%s\r\n", deviceIdReceived_raw);
        int deviceIdReceived;
        sscanf(deviceIdReceived_raw, "%d", &deviceIdReceived);        

        // Spew it
        if ((int)handshakeID==(int)handshakeConfirmation)
        {
           /////NB assumption that we will use 4 bytes for data in eprom
           EEPROM_writelong(0,(int)deviceIdReceived);
           NodeID=(int)deviceIdReceived;
           
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

    dealWithDHTData(a, sizeof(a));
    rest();
   
    // Now, continue listening
    state = receive_data;
    
  }  
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
       
         m_switch = actuatorCommand(target_command);

         // First, stop listening so we can talk
         radio.stopListening();
         Serial.print(F("Sending Confirmation response: "));
         // Send the final one back.
         sprintf(a, "%03i", NodeID);
         sprintf(a + strlen(a), "_ack_%s;",msg);
         printf("%s:", a);
         sendMessage(a, sizeof(a));

         // Send switch state.
         sprintf(a, "%03i", NodeID);
         sprintf(a + strlen(a), "_s_%04i;",m_switch);
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
  if (iterations_count>=REFRESH_Interval)
  {
     resetMeasurement();
  }
  
}

void dealWithDHTData(char* a, unsigned int aLen)
{
  for (int n=1; n<=16; ++n)
  {
    if ((sensors_DHT_connection_bit) & (1<<(n-1))) 
    { 
        int chk;

        chk = DHT.read11(n);
        
        //could not calculate DHT sensor type
//        switch (m_DHT_sensor_type)
//        {
//            case 0: 
//              chk = DHT.read11(n);
//              break;
//            case 1: 
//              chk = DHT.read21(n);
//              break;
//            case 2: 
//              chk = DHT.read22(n);
//              break;              
//            default: 
//              Serial.print(F("Unknown DHT Sensor type!\r\n")); 
//              break;
//        }
            
        if (chk == DHTLIB_OK) 
        {
          int value = (int)DHT.humidity;
          sprintf(a, "%03i", NodeID);
          sprintf(a + strlen(a), "_h_%02i", value);
          sprintf(a + strlen(a), "_%01i;", n);
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

          value = (int)DHT.temperature;
          sprintf(a, "%03i", NodeID);
          sprintf(a + strlen(a), "_t_%02i", value);
          sprintf(a + strlen(a), "_%01i;", n);          
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
        }
        else
        {
            ///If Type is clear and reading failed report error
            switch (chk)
            {
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
        }
    }
  }
}



void dealWithPIRData(char* a, unsigned int aLen)
{
  
  for (int n=1; n<=16; ++n)
  {
    if ((sensors_PIR_connection_bit) & (1<<(n-1))) 
    { 
       int value = digitalRead(n);
       sprintf(a, "%03i", NodeID);
       sprintf(a + strlen(a), "_p_%01i", value);
       sprintf(a + strlen(a), "_%01i;", n);       
       Serial.print(F("Now sending "));
       printf("%s:", a);

       digitalWrite(INDICATION_PIN, value); 

       if (m_pir != value)  
       {
           sendMessage(a, aLen);
           m_pir = value;            
       }
       else
       {
           Serial.print(F("No changes.\r\n"));
       }
    }
  }
  
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
  m_heating_temp = -1;
  iterations_count = 0;
  m_switch = -1;
  
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

int actuatorCommand(char* command)
{
   printf("   Execute Command: %s\r\n", command); 
 
   if (strcmp(command, "SwitchOff") == 0)
   {
      digitalWrite(SWITCH_CONTROL, LOW);
      printf("    c_Switch Off\r\n"); 
      return 0;
   }
   
   if (strcmp(command, "SwitchOn") == 0)
   {
      digitalWrite(SWITCH_CONTROL, HIGH);
      printf("    c_Switch On\r\n");       
      return 1;
   }
   
   if (strcmp(command, "Reinitialize") == 0)
   {
      resetMeasurement();
      printf("    c_Reset\r\n");
      return 1023;
   }

}
