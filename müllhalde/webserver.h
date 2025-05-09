#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <WebServer.h> // Ensure WebServer is included

void initializeWiFi();
void initializeServer(WebServer& server);
void handleClientRequests(WebServer& server);
void SendWebsite();
void SendXML();
void SendJSON();
void UpdateSlider();
void ProcessButton_0();
void ProcessButton_1();

#endif // WEBSERVER_H
