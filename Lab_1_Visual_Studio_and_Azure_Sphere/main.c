﻿/*
 *   Please read the disclaimer and the developer board selection section below
 *
 *
 *   DISCLAIMER
 *
 *   The learning_path_libs functions provided in the learning_path_libs folder:
 *
 *	   1. are NOT supported Azure Sphere APIs.
 *	   2. are prefixed with lp_, typedefs are prefixed with LP_
 *	   3. are built from the Azure Sphere SDK Samples at https://github.com/Azure/azure-sphere-samples
 *	   4. are not intended as a substitute for understanding the Azure Sphere SDK Samples.
 *	   5. aim to follow best practices as demonstrated by the Azure Sphere SDK Samples.
 *	   6. are provided as is and as a convenience to aid the Azure Sphere Developer Learning experience.
 *
 *
 *   DEVELOPER BOARD SELECTION
 *
 *   The following developer boards are supported.
 *
 *	   1. AVNET Azure Sphere Starter Kit.
 *	   2. Seeed Studio Azure Sphere MT3620 Development Kit aka Reference Design Board or rdb.
 *	   3. Seeed Studio Seeed Studio MT3620 Mini Dev Board.
 *
 *   ENABLE YOUR DEVELOPER BOARD
 *
 *   Each Azure Sphere developer board manufacturer maps pins differently. You need to select the configuration that matches your board.
 *
 *   Follow these steps:
 *
 *	   1. Open CMakeLists.txt.
 *	   2. Uncomment the set command that matches your developer board.
 *	   3. Click File, then Save to save the CMakeLists.txt file which will auto generate the CMake Cache.
 */

// Hardware definition
#include "hw/azure_sphere_learning_path.h"

// Learning Path Libraries
#include "azure_iot.h"
#include "exit_codes.h"
#include "globals.h"
#include "peripheral_gpio.h"
#include "terminate.h"
#include "timer.h"

// System Libraries
#include "applibs_versions.h"
#include <applibs/gpio.h>
#include <applibs/log.h>
#include <applibs/powermanagement.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

// Hardware specific
#ifdef OEM_AVNET
#include "AVNET/board.h"
#include "AVNET/imu_temp_pressure.h"
#include "AVNET/light_sensor.h"
#endif // OEM_AVNET

// Hardware specific
#ifdef OEM_SEEED_STUDIO
#include "SEEED_STUDIO/board.h"
#endif // SEEED_STUDIO


#define JSON_MESSAGE_BYTES 256  // Number of bytes to allocate for the JSON telemetry message for IoT Central

// Forward signatures
static void InitPeripheralsAndHandlers(void);
static void ClosePeripheralsAndHandlers(void);
static void Led1BlinkHandler(EventLoopTimer* eventLoopTimer);
static void Led2OffHandler(EventLoopTimer* eventLoopTimer);
static void MeasureSensorHandler(EventLoopTimer* eventLoopTimer);
static void ButtonPressCheckHandler(EventLoopTimer* eventLoopTimer);
static void NetworkConnectionStatusHandler(EventLoopTimer* eventLoopTimer);

static char msgBuffer[JSON_MESSAGE_BYTES] = { 0 };

static const struct timespec led2BlinkPeriod = { 0, 300 * 1000 * 1000 };

static int led1BlinkIntervalIndex = 2;
static const struct timespec led1BlinkIntervals[] = { {0, 125000000}, {0, 250000000}, {0, 500000000}, {0, 750000000}, {1, 0} };
static const int led1BlinkIntervalsCount = NELEMS(led1BlinkIntervals);

// GPIO Input Peripherals
static LP_PERIPHERAL_GPIO buttonA = { .pin = BUTTON_A, .direction = LP_INPUT, .initialise = lp_openPeripheralGpio, .name = "buttonA" };
static LP_PERIPHERAL_GPIO buttonB = { .pin = BUTTON_B, .direction = LP_INPUT, .initialise = lp_openPeripheralGpio, .name = "buttonB" };

// GPIO Output Peripherals
static LP_PERIPHERAL_GPIO led1 = { .pin = LED1, .direction = LP_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true,
	.initialise = lp_openPeripheralGpio, .name = "led1" };

static LP_PERIPHERAL_GPIO led2 = { .pin = LED2, .direction = LP_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true,
	.initialise = lp_openPeripheralGpio, .name = "led2" };

static LP_PERIPHERAL_GPIO networkConnectedLed = { .pin = NETWORK_CONNECTED_LED, .direction = LP_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true,
	.initialise = lp_openPeripheralGpio, .name = "networkConnectedLed" };

// Timers
static LP_TIMER led1BlinkTimer = { .period = { 0, 125000000 }, .name = "led1BlinkTimer", .handler = Led1BlinkHandler };
static LP_TIMER led2BlinkOffOneShotTimer = { .period = { 0, 0 }, .name = "led2BlinkOffOneShotTimer", .handler = Led2OffHandler };
static LP_TIMER buttonPressCheckTimer = { .period = { 0, 1000000 }, .name = "buttonPressCheckTimer", .handler = ButtonPressCheckHandler };
static LP_TIMER networkConnectionStatusTimer = { .period = { 5, 0 }, .name = "networkConnectionStatusTimer", .handler = NetworkConnectionStatusHandler };
static LP_TIMER measureSensorTimer = { .period = { 10, 0 }, .name = "measureSensorTimer", .handler = MeasureSensorHandler };

// Initialize Sets
LP_PERIPHERAL_GPIO* peripheralSet[] = { &buttonA, &buttonB, &led1, &led2, &networkConnectedLed };
LP_TIMER* timerSet[] = { &led1BlinkTimer, &led2BlinkOffOneShotTimer, &buttonPressCheckTimer, &networkConnectionStatusTimer, &measureSensorTimer };


int main(int argc, char* argv[])
{
	lp_registerTerminationHandler();

	InitPeripheralsAndHandlers();

	// Main loop
	while (!lp_isTerminationRequired())
	{
		int result = EventLoop_Run(lp_getTimerEventLoop(), -1, true);
		// Continue if interrupted by signal, e.g. due to breakpoint being set.
		if (result == -1 && errno != EINTR)
		{
			lp_terminate(ExitCode_Main_EventLoopFail);
		}
	}

	ClosePeripheralsAndHandlers();

	Log_Debug("Application exiting.\n");
	return lp_getTerminationExitCode();
}

/// <summary>
/// Check status of connection to Azure IoT
/// </summary>
static void NetworkConnectionStatusHandler(EventLoopTimer* eventLoopTimer)
{
	if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0)
	{
		lp_terminate(ExitCode_ConsumeEventLoopTimeEvent);
		return;
	}

	if (lp_isNetworkReady())
	{
		lp_gpioOn(&networkConnectedLed);
	}
	else
	{
		lp_gpioOff(&networkConnectedLed);
	}
}

/// <summary>
/// Turn on LED2 and set a one shot timer to turn LED2 off
/// </summary>
static void Led2On(void)
{
	lp_gpioOn(&led2);
	lp_setOneShotTimer(&led2BlinkOffOneShotTimer, &led2BlinkPeriod);
}

/// <summary>
/// One shot timer to turn LED2 off
/// </summary>
static void Led2OffHandler(EventLoopTimer* eventLoopTimer)
{
	if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0)
	{
		lp_terminate(ExitCode_ConsumeEventLoopTimeEvent);
		return;
	}
	lp_gpioOff(&led2);
}

/// <summary>
/// Read sensor and send to Azure IoT
/// </summary>
static void MeasureSensorHandler(EventLoopTimer* eventLoopTimer)
{
	static int msgId = 0;
	static LP_ENVIRONMENT environment;
	static const char* MsgTemplate = "{ \"Temperature\": \"%3.2f\", \"Humidity\": \"%3.1f\", \"Pressure\":\"%3.1f\", \"Light\":%d, \"MsgId\":%d }";

	if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0)
	{
		lp_terminate(ExitCode_ConsumeEventLoopTimeEvent);
		return;
	}

	if (lp_readTelemetry(&environment))
	{
		if (snprintf(msgBuffer, JSON_MESSAGE_BYTES, MsgTemplate, environment.temperature, environment.humidity, environment.pressure, environment.light, msgId++) > 0)
		{
			Log_Debug("%s\n", msgBuffer);
			Led2On();
		}
	}
}

/// <summary>
/// Handler to check for Button Presses
/// </summary>
static void ButtonPressCheckHandler(EventLoopTimer* eventLoopTimer)
{
	static GPIO_Value_Type buttonAState;
	static GPIO_Value_Type buttonBState;

	if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0)
	{
		lp_terminate(ExitCode_ConsumeEventLoopTimeEvent);
		return;
	}

	if (lp_gpioGetState(&buttonA, &buttonAState) || lp_gpioGetState(&buttonB, &buttonBState))
	{
		led1BlinkIntervalIndex = (led1BlinkIntervalIndex + 1) % led1BlinkIntervalsCount;
		lp_changeTimer(&led1BlinkTimer, &led1BlinkIntervals[led1BlinkIntervalIndex]);
	}
}

/// <summary>
/// Blink Led1 Handler
/// </summary>
static void Led1BlinkHandler(EventLoopTimer* eventLoopTimer)
{
	static bool led_state = false;

	if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0)
	{
		lp_terminate(ExitCode_ConsumeEventLoopTimeEvent);
		return;
	}

	led_state = !led_state;

	if (led_state) { lp_gpioOff(&led1); }
	else { lp_gpioOn(&led1); }
}

/// <summary>
///  Initialize peripherals, device twins, direct methods, timers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static void InitPeripheralsAndHandlers(void)
{
	lp_initializeDevKit();

	lp_openPeripheralGpioSet(peripheralSet, NELEMS(peripheralSet));
	lp_startTimerSet(timerSet, NELEMS(timerSet));
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
	Log_Debug("Closing file descriptors\n");

	lp_stopTimerSet();
	lp_closePeripheralGpioSet();
	lp_closeDevKit();

	lp_stopTimerEventLoop();
}