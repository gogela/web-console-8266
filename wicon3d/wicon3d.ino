/*
 * wicon3d - wifi console for 3D printer
 *
 *  
 *
 */

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <SD.h>

#define FS_NO_GLOBALS
#include <FS.h>
#include <SPI.h>

#define MARLIN 1 // indicates the connected serial device is MARLIN reprap board - sends IP to reprap display, set to 0 for gereric serial consle
#define SERIAL_PORT Serial //which serial port to be used
#define DEFAULT_BAUDRATE 115200
#define WEBSOCKET_PORT 81
#define HTTP_PORT 80

// your wifi ssid/pwd
#define PRIMARY_SSID "xxxx" 
#define PRIMARY_PWD "********"
// optional
#define SECONDARY_SSID "yyyy" 
#define SECONDARY_PWD "****************"

SDClass* SDg=NULL;

ESP8266WiFiMulti WiFiMulti;
WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);
ESP8266WebServer server(HTTP_PORT);
fs::File fsUploadFile;
//sd::File SDUploadFile;
File SDUploadFile;
boolean sdStarted=false;


String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean debugging = false;  //turns on/off debugging


/**** service routines ****/
/*** does debug output to attached serial if enabled***/
void debugPrintln(String s){
  if(debugging) SERIAL_PORT.println(s);
}
void debugPrint(String s){
  if(debugging) SERIAL_PORT.print(s);
}
void debugPrintf(const char *format, ...){
   if(debugging){
      va_list arglist;
      va_start( arglist, format );
      vprintf( format, arglist );
      va_end( arglist );
   }
  
}

/**** WebSockes handling ****/

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
    static uint8_t buf[3]={1};
    switch(type) {
        case WStype_DISCONNECTED:
            debugPrintf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                debugPrintf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        
        // send message to client
        webSocket.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
            debugPrintf("[%u] get Text: %s\n", num, payload);
            Serial.printf("%s\n", payload); //print to serial
            webSocket.sendBIN(num,buf, 1); //kind of ACK - flow control to indicate readiness for next message (sends binary message containing 1)
            break;
        case WStype_BIN: //normally unused here
            debugPrintf("[%u] get binary lenght: %u\n", num, lenght);
            //hexdump(payload, lenght);
            webSocket.sendBIN(num,buf, 1); //kind of ACK - flow control to indicate readiness for next message (sends binary message containing 1)
            break;
    }

}


/**** hardware settings handlers ****/

void handleSetSerial() {
  if(!server.hasArg("baudrate")) {server.send(500, "text/plain", "BAD ARGS"); return;}  //set baudrate for serial
  String baudrate = server.arg("baudrate");
  long br=baudrate.toInt();
      
  SERIAL_PORT.flush();
  SERIAL_PORT.end();
  SERIAL_PORT.begin(br);
  server.send(200, "text/plain", "");

  
  if(server.hasArg("debug")) // set debug output on/off
    { 
       debugging=true;
      SERIAL_PORT.setDebugOutput(true);
      debugPrintln("Console debug enabled");
      debugPrintln("Baudrate: "+baudrate);
     }
    else {
      SERIAL_PORT.setDebugOutput(false);
      debugging=false;
    }

}


/**** WebServer ****/

//server stuff
//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

 
    


/**** SPIFS handlers *****/

void handleSPIFSGet(){
  String path=server.uri();
  debugPrintln("handleSPIFSGet: " + path);
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    fs::File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return;
  }
  server.send(404, "text/plain", "FileNotFound");
  return;
}

void handleSPIFSUpload(){
  
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    debugPrint("handleFileUpload Name: "); debugPrintln(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //debugPrint("handleFileUpload Data: "); debugPrintln(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    debugPrint("handleFileUpload Size: "); debugPrintln(String(upload.totalSize));
  }
}

void handleSPIFSDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg("path");
  debugPrintln("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}


void handleSPIFSList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  debugPrintln("handleFileList: " + path);
  fs::Dir dir = SPIFFS.openDir(path);
  //path = String();

  String output = "[";
  while(dir.next()){
    fs::File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}




/*** SD Card handlers ***/
void handleSDmount()
{
      
   //SD filesystem - mount
    //SDg->rootClose(); //just in case something remains open
    //int result=SDg->begin(16);
    //if (result>0) {
    
    //debugPrint("SD initialization failed! (1=card,2=volume,3=root):");
    //debugPrintln(String(result));
    //if (result==1) debugPrintln("Card err code:"+String(SDg->cardError()));
  digitalWrite(2,LOW); //enable esp card access
 
  SDg=new SDClass();
  if (!SDg->begin(16)){  
        debugPrint("SD initialization failed!");
        server.send(500, "text/plain", "Mounting error");
        return;
      
    }
  
  server.send(200, "text/plain", "Mounted");
 
}

void handleSDunmount()
{
  //SD filesystem unmount //stupid way , the SD library has no method
  //SDg->rootClose();
//  int result=SDg->begin(15);
//  if (result>0) {
//    debugPrint("SD initialization failed! (1=card,2=volume,3=root):");
//    debugPrintln(String(result));
//    if (result==1) debugPrintln("Card err code:"+String(SDg->cardError()));
//  } 
  //if(!SDg->begin(15)) debugPrintln("unmount error");
  //digitalWrite(16,HIGH);
  digitalWrite(2,HIGH);
  delete(SDg);
  SDg=NULL;
  server.send(200, "text/plain", "Unmounted");
}


void handleSDGet(){
  String path=server.uri();
  if (path.startsWith("/SDMOUNT/")) {
    String filename = path.substring(8);
    debugPrintln("handleSDGet: " + filename);
    String contentType = getContentType(filename);
    if(SDg->exists((char*)filename.c_str())){
       //sd::File file = SDg->open(filename, FILE_READ);
       File file = SDg->open(filename, FILE_READ);
       size_t sent = server.streamFile(file, contentType);
       file.close();
       return;
    }
  }
  server.send(404, "text/plain", "FileNotFound");
  return;
}


void handleSDupload(){
    if(server.uri() != "/sdupload") return;
    debugPrintln("sdupload");
    HTTPUpload& upload = server.upload();
   
    if(upload.status == UPLOAD_FILE_START){
      
     if(SDg->exists((char *)upload.filename.c_str())) {
      debugPrintln("sdupload: file exists - removing old");
      SDg->remove((char *)upload.filename.c_str());
    }
    
    SDUploadFile = SDg->open(upload.filename.c_str(), FILE_WRITE);
    debugPrint("Upload: START, filename: "); debugPrintln(upload.filename);
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(SDUploadFile) SDUploadFile.write(upload.buf, upload.currentSize);
    debugPrint("Upload: WRITE, Bytes: "); debugPrintln(String(upload.currentSize));
  } else if(upload.status == UPLOAD_FILE_END){
    if(SDUploadFile) SDUploadFile.close();
    debugPrint("Upload: END, Size: "); debugPrintln(String(upload.totalSize));
    server.send(200, "text/plain", "Uploaded bytes:"+ String(upload.totalSize));
  }
}


void handleSDfileList(){
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  debugPrintln("handleSDFileList: " + path);
  //sd::File dir = SDg->open(path);
  File dir = SDg->open(path);
  //path = String();
  if(!dir.isDirectory()){
    dir.close();
    debugPrintln("no dir");
    server.send(404, "text/plain", "Dir or File Not Found");
    return ;
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    
    //sd::File entry = dir.openNextFile();
    File entry = dir.openNextFile();
    if (!entry)
    break;

    debugPrintln(entry.name());
    String output;
    if (cnt > 0)
      output = ',';

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.name();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
 }
 server.sendContent("]");
 dir.close();
}
  

void handleSDdelete(){
  
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = "/"+server.arg("path");
  
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  
  debugPrintln("running sdexist");
  if(!SDg->exists((char *)path.c_str()))     return server.send(404, "text/plain", "FileNotFound");
  
  debugPrintln("running SDg->remove"); 
  SDg->remove((char *)path.c_str());
  server.send(200, "text/plain", "");
  path = String();

  }    

/*** general get handler ***/
void handleGet(){   //SPIFS or SD switch
  String path=server.uri();
  debugPrintln("handleGet: " + path);
  if (path.startsWith("/SDMOUNT"))
    handleSDGet();
  else
    handleSPIFSGet();
  return;
}   

/*** firmware  OAT updates ****/
void handleOTAupload(){

      HTTPUpload& upload = server.upload();
      if(upload.status == UPLOAD_FILE_START){
        
        //        WiFiUDP::stopAll();
        String filename = upload.filename;
        debugPrint("OTA update - filename: "); debugPrintln(filename);
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if(!Update.begin(maxSketchSpace)){ //start with max available size
          String err=String(Update.getError());
          debugPrintln("update.begin error:"+err);
        }
      } else if(upload.status == UPLOAD_FILE_WRITE){
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          String err=String(Update.getError());
          debugPrintln("update.wite error:"+err);
        }
      } else if(upload.status == UPLOAD_FILE_END){
        if(Update.end(true)){ //true to set the size to the current progress
          debugPrintln("Update Success: %u\nRebooting...\n");
        } else {
          String err=String(Update.getError());
          debugPrintln("update.end error:"+err);
        }
        
      }
      yield();
  }

/**** SETUP *****/

void setup() {
    pinMode(2,OUTPUT);
    digitalWrite(2,HIGH); //switch SD to marlin port

    
    
    SERIAL_PORT.begin(DEFAULT_BAUDRATE); //set default baudrate
    Serial.setDebugOutput(false);  // debug output to serial disabled by default

    // waiting for boot process to complete | TODO - really needed?     
    for(uint8_t t = 4; t > 0; t--) {  
        debugPrintf("[SETUP] BOOT WAIT %d...\n", t);
        SERIAL_PORT.flush();
        delay(1000);
    }

    // define APs
    WiFiMulti.addAP(PRIMARY_SSID, PRIMARY_PWD);
  #if defined(SECONDARY_SSID) 
    WiFiMulti.addAP(SECONDARY_SSID, SECONDARY_PWD);
  #endif

    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    //inputString.reserve(200);

  #ifdef MARLIN
    // print IP address to Marlin console
    SERIAL_PORT.println("");
    SERIAL_PORT.println("M117 myIP:"+WiFi.localIP().toString()); //show local IP on display (works on Marlin)
  #endif  
  
    // init spifs filesystem
    SPIFFS.begin();
 
  
  /****** handlers, urls ************/
  //serial settings
  server.on("/setserial", HTTP_GET, handleSetSerial);
  
  //SPIFS handles
  server.on("/spifslist", HTTP_GET, handleSPIFSList); //list directory
  //server.on("/spifsget", HTTP_GET, handleSPIFSGet); //get the file
  server.on("/spifsdelete", HTTP_DELETE, handleSPIFSDelete);  //delete file
  server.on("/spifsupload", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleSPIFSUpload); //upload file

  //SD card handles
  server.on("/sdmount", HTTP_GET, handleSDmount);
  server.on("/sdunmount", HTTP_GET, handleSDunmount);
  server.on("/sdlist", HTTP_GET, handleSDfileList);
  server.on("/sdget", HTTP_GET, handleSDGet);
  server.on("/sddelete", HTTP_DELETE, handleSDdelete);
  server.on("/sdupload", HTTP_POST,[](){ server.send(200, "text/plain", ""); }, handleSDupload);
  
  //OTA firmware update
  server.on("/update", HTTP_POST, [](){
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    },handleOTAupload);


  //undefined URI, get file/htmlpage from FS
  server.onNotFound(handleGet);

  
  server.begin(); // start the server
  debugPrintln("HTTP server started");
}


/**** Main loop ****/

void loop() {
  
    server.handleClient(); //httpd server loop
    webSocket.loop();  //websocket server loop

    while (Serial.available()) {  //read from serial
    char c = Serial.read();  //gets one byte from serial buffer
    inputString += c; //makes the String readString
    delay(2);  //slow looping to allow buffer to fill with next character
    if (c == '\n') {
      stringComplete = true;
    }
  }

  if (stringComplete) {  //send serial input to websocket
    webSocket.broadcastTXT(inputString+"\n");
    // clear the string:
    inputString = "";
    stringComplete = false;
  }
}


  
    
