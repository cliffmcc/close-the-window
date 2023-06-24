//
// Created by Cliff McCollum on 28/04/2021.
//

#ifndef UNTITLED_MAIN_H
#define UNTITLED_MAIN_H

#define kOUTPUT_DEVICE Serial
#define _DEBUG_
#ifdef _DEBUG_
#define DEBUG_WRAP(x) x
#else
#define DEBUG_WRAP(x)
#endif

#define _USE_LED_
#ifdef _USE_LED_
#define LED_WRAP(x) x
#else
#define LED_WRAP(x)
#endif

void setupHardware();
void setupPersistence();
void connectNetwork();
void checkWindow();
void persistData(); // only used in DeepSleep version

bool windowSwitchIsOpen();

int get_temperature_tonight();
bool SendOpenNotification(int temperature);

#endif //UNTITLED_MAIN_H
