//Arduino, WeMos D1R1 (ESP8266 Boards 2.5.2), upload 921600, 80MHz, COM:7,WeMos D1R1 Boards 2
//Creates SD file data. Data available via direct WiFi.  Tested with 64GB SD.
//Plots data using Tools --> Serial Plotter
//Stores reading on SD when values above / below set limits.
//WeMos Micro SD Shield uses HSPI 12-15 (not 5-8), 3V3, G:
//GPIO12(D6)=MISO (main in, secondary out); GPIO13(D7)=MOSI (main out, secondary in) 
//GPIO14(D5)=CLK (Clock); GPIO15(D8)=CS (Chip select)
//SD library-->8.3 filenames (ABCDEFGH.txt = abcdefgh.txt)
//RTC DS1307. I2C--> SCL(clk)=D1, SDA(data)=D2 
//XFW-HX711 Load Cell A2D.  +3,+5=>VCC; Gnd=>Gnd; DT=>GPIO0=D4 or GPIO16=D0; SCK=>GPIO2=D3.
//Load Cell: Red = E+, Blk = E-, Wht = A-, Grn = A+  
//20210806 - TSBrownie.  Non-commercial use.
#include <SD.h>               //SD card library
#include <SPI.h>              //Serial Peripheral Interface bus lib for COMM, SD com
#include <ESP8266WiFi.h>      //WiFi library
#include <ESP8266WebServer.h> //Web Server library
File dataFile;                //SD card file handle
const char* ssid = "TP-LINK_2.4GHz";  //Your Router/connect SSID
const char* password = "xxxxxxxxxx";  //Your Router/connect password

IPAddress local_ip(192,168,1,99);     //Device IP for local connect
IPAddress gateway(192,168,1,99);      //Device IP for local connect
IPAddress subnet(255,255,255,0);      //Subnet mask
ESP8266WebServer server(80);          //Port 80, standard internet

#include "Wire.h"             //I2C library
#include "HX711.h"            //HX711 Load Cell library
HX711 scale;                  //Link to HX711 lib function
#define DS1307 0x68           //I2C Addr of RTC1307 (Default=0x68 Hex)
String SDData;                //Build data to write to SD "disk"
String timeString;            //Build date time data
bool OneTimeFlag = true;      //For demo, execute 1 time (remove for logging)
bool NoPlot = false;          //True = plotting and print extra user data to monitor
String DoWList[]={"Null",",Sun,",",Mon,",",Tue,",",Wed,",",Thr,",",Fri,",",Sat,"}; //DOW from 1-7
byte second, minute, hour, DoW, Date, month, year;     //Btye variables for BCD time
String FName = "0Quake01.txt"; //SD card file name to create/write/read
unsigned int interval = 1000; //Time in milliseconds between pin readings 
const int LOADCELL_DOUT = 0;  //GPIO0 = D3  or  GPIO16 = D0  yellow wire 
const int LOADCELL_SCK = 2;   //GPIO2 = D4 brn wire
long calib = 0L;              //Scale offset to zero
long reading = 0L;            //Load cell reading
long value = 0L;              //Temp value from readScale
long minValue = -500L;        //Noise Filter. Below keep data. Zero for continuous.
long maxValue = 500L;         //Noise Filter. Above keep data. Zero for continuous.
int j = 0;                    //Testing. Show stored every j times, then delete file.
unsigned long recalInterval = 600000L; //Recalibration interval. 60k=1 min, 3600k=1hr
unsigned long calibTimeLast = 0L;      //Last recalibration time
long tempTime = 0L;           //Intermediate time storage

//RTC FUNCTIONS =====================================
byte BCD2DEC(byte val){             //Ex: 51 = 01010001 BCD. 01010001/16-->0101=5 then x10-->50.  
  return(((val/16)*10)+(val%16));}  //         01010001%16-->0001. 50+0001 = 51 DEC

void GetRTCTime(){                               //Routine read real time clock, format data
  byte second;byte minute;byte hour;byte DoW;byte Date;byte month;byte year;
  Wire.beginTransmission(DS1307);                //Open I2C to RTC DS1307
  Wire.write(0x00);                              //Write reg pointer to 0x00 Hex
  Wire.endTransmission();                        //End xmit to I2C.  Send requested data.
  Wire.requestFrom(DS1307, 7);                   //Get 7 bytes from RTC buffer
  second = BCD2DEC(Wire.read() & 0x7f);          //Seconds.  Remove hi order bit
  minute = BCD2DEC(Wire.read());                 //Minutes
  hour = BCD2DEC(Wire.read() & 0x3f);            //Hour.  Remove 2 hi order bits
  DoW = BCD2DEC(Wire.read());                    //Day of week
  Date = BCD2DEC(Wire.read());                   //Date
  month = BCD2DEC(Wire.read());                  //Month
  year = BCD2DEC(Wire.read());                   //Year
  timeString = 2000+year;                        //Build Date-Time data to write to SD
  if (month<10){timeString = timeString + '0';}  //Pad leading 0 if needed
  timeString = timeString + month;               //Month (1-12)  
  if(Date<10){timeString = timeString + '0';}    //Pad leading 0 if needed
  timeString = timeString + Date;                //Date (1-30)
  timeString = timeString + DoWList[DoW];        //1Sun-7Sat (0=null)
  if (hour<10){timeString = timeString + '0';}   //Pad leading 0 if needed
  timeString = timeString + hour;                //HH (0-24)
  if (minute<10){timeString = timeString + '0';} //Pad leading 0 if needed
  timeString = timeString + minute;              //MM (0-60)
  if (second<10){timeString = timeString + '0';} //Pad leading 0 if needed
  timeString = timeString + second;              //SS (0-60)
}
//WiFi RELATED FUNCTIONS ============================
void handleRoot(){                               //Allows data to be sent via WiFi
  server.send(200, "text/html", "IP Linked. Add '/getdata' to start transfer");
}

void getdata() {
  openFile(FILE_READ);                                           //Open SD file read
  int SDfileSz = dataFile.size();                                //Get file size
  if (NoPlot){Serial.print("SDfileSz: ");  Serial.println(SDfileSz);} //User info.
  server.sendHeader("Content-Length", (String)(SDfileSz));       //
  server.sendHeader("Cache-Control", "max-age=2628000, public"); //Cache 30 days
  size_t fsizeSent = server.streamFile(dataFile, "text/plain");  //
  if (NoPlot){Serial.print("fsizeSent: "); Serial.println(fsizeSent);}
  dataFile.close();                                              //Close SD file
  delay(100);
}

//SD CARD FUNCTIONS =================================
void openSD() {                              //Routine to open SD card
  if (NoPlot){Serial.println(); Serial.println("Open SD card");}    //User message.
  if (!SD.begin(15)) {                       //If not open, print message.  (CS=pin15)
    if (NoPlot){Serial.println("Open SD card failed");}
    return;}
  if (NoPlot){Serial.println("SD Card open");}
}

char openFile(char RW) {                     //Open SD file.  Only 1 open at a time.
  dataFile.close();                          //Ensure file status, before re-opening
  dataFile = SD.open(FName, RW);}            //Open Read at end.  Open at EOF for write/append

String print2File(String tmp1) {             //Print data to SD file
  openFile(FILE_WRITE);                      //Open user SD file for write
  if (dataFile) {                            //If file there & opened --> write
    dataFile.println(tmp1);                  //Print string to file
    dataFile.close();                        //Close file, flush buffer (reliable but slower)
  } else {Serial.println("Error opening file for write");}   //File didn't open
}

void getRecordFile() {                       //Read from SD file
  if (dataFile) {                            //If file is there
    Serial.write(dataFile.read());           //Read datafile, then write to COM.
  } else {Serial.println("Error opening file for read");}    //File didn't open
}

//Load Cell FUNCTIONS =================================
long readScale(){
    if (scale.wait_ready_timeout(1000)) {     //Wait x millisec to get reading
      reading = scale.read();                 //Load cell reading
      return reading;                         //Return reading
  }else {Serial.println("HX711 Offline.");}   //Else print load cell offline
}

void recalibrate(){                           //
  tempTime = calibTimeLast - millis();        //Check recalibration interval
  if (abs(tempTime) > recalInterval){         //Recalibrate every x time. 
    calib = -readScale();                     //New calibration
    GetRTCTime();                             //Get time from real time clock
    SDData = timeString+','+'C'+','+calib;    //Prepare calibration string
    print2File(SDData);                       //Write string to SD file
    if (NoPlot){Serial.print("Re-Calibration: ");Serial.println(SDData);}
    calibTimeLast = millis();                 //Keep current time for next interation
  }
}

// SETUP AND LOOP =============================
void setup() {                               //Required setup routine
  Wire.begin();                              //Join I2C bus as primary
  Serial.begin(74880);                       //Open serial com (74880, 38400, 115200)
  openSD();                                  //Call open SD card routine
  scale.begin(LOADCELL_DOUT, LOADCELL_SCK);  //Open load cell
  delay(1000);                               //Allow serial to startup
  calib = -readScale();                      //Offset to zero load cell.
  if (NoPlot){Serial.print("Calibration: ");Serial.println(calib);}
  delay(1000);                               //Allow SD to start
  GetRTCTime();                              //Get time from real time clock
  SDData = timeString+','+'C'+','+calib;     //Prepare calibration string
  print2File(SDData);                        //Write string to SD file
  if (NoPlot){Serial.print("Connecting to network");}
  WiFi.mode(WIFI_STA);                       //Station mode
  WiFi.config(local_ip, gateway, subnet);    //
  WiFi.begin(ssid, password);                //Init WiFi connect
  while (WiFi.status() != WL_CONNECTED) {    //Wait for connect
    delay(500);
    if (NoPlot){Serial.print(".");}          //Wait dot.
  }
  if (NoPlot){Serial.print("SSID Connected: ");Serial.println(ssid);}       //SSID Info.
  if (NoPlot){Serial.print("IP Address: ");Serial.println(WiFi.localIP());} //IP Info.
  server.on("/", handleRoot);                                    //Web input
  server.on("/getdata", getdata);                                //Web input
  server.begin();                                                //Start server
  if (NoPlot){Serial.println("HTTP Server Started");}            //User info.
}

void loop() {                                //Get data, write timestamped records
    server.handleClient();                   //Handle client calls
    value = readScale() + calib;             //Calibrated data
    String withScale = "-5000 ";             //Output string fixed Y scale Min for plotter
    withScale += value;                      //Add seismometer data
    withScale += " 5000";                    //Add Maxmimum scale
    Serial.println(withScale);               //Output to serial plotter, for calibration, etc

    if(value > maxValue or value < minValue){//Decide if take reading
      if (NoPlot){Serial.print("Capturing Data ");Serial.println(timeString);} //Inform user.
      GetRTCTime();                          //Get time from real time clock
      SDData = timeString+','+'D'+','+value; //Prepare data string to write to SD
      print2File(SDData);                    //Write data to SD file
//      j++;                                   //Used in testing only.
    } else {recalibrate();}                  //Time based Recalibration, if not busy
/*    if(j > 100){                             //Testing every 100 writes, dump SD file
      if (NoPlot){Serial.println("File Write Done");}
      openFile(FILE_READ);                   //Testing. Open SD file at start for read. Remove for plotting
      while (dataFile.available()) {         //Testing. Read SD file until EOF
        getRecordFile();}                    //Testing. Get 1 line from SD file
      delay(4000);                           //Testing.Print out data wait x seconds
      j = 0;                                 //Testing only
    dataFile.close();                        //Testing. Close file, flush buffer.
    if (NoPlot){Serial.println("Printout File Done.  Delete File.");} //Testing only. User info.
    SD.remove(FName);                        //Testing ONLY. Deletes SD file.    
    }*/
}
