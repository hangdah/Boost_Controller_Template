/**
 * @file    main.c
 * @brief   Boost application entry point
 * @author  dah
 */

#include "boost_app.h"

void main(void)
{
    BoostApp_Init();
    BoostScheduler_Start();

    for (;;)
    {
        BoostApp_BackgroundTask();
    }
}
