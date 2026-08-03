/*
 * boost_app.h
 *
 * Top-level Boost application interfaces.
 */

#ifndef APP_APP_LIBRARIES_BOOST_APP_H_
#define APP_APP_LIBRARIES_BOOST_APP_H_

#include "function.h"

BoostController *GetBoostHandle(void);
void BoostApp_Init(void);
void BoostScheduler_Start(void);
void BoostApp_BackgroundTask(void);

#endif /* APP_APP_LIBRARIES_BOOST_APP_H_ */
