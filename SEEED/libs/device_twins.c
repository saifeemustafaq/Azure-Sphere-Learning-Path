#include "device_twins.h"

void SetDesiredState(JSON_Object* desiredProperties, DeviceTwinBinding* deviceTwinBinding);
void DeviceTwinsReportStatusCallback(int result, void* context);
bool DeviceTwinUpdateReportedState(char* reportedPropertiesString);
bool TwinReportState(DeviceTwinBinding* deviceTwinBinding);


DeviceTwinBinding** _deviceTwins = NULL;
size_t _deviceTwinCount = 0;


void OpenDeviceTwinSet(DeviceTwinBinding* deviceTwins[], size_t deviceTwinCount) {
	_deviceTwins = deviceTwins;
	_deviceTwinCount = deviceTwinCount;

	for (int i = 0; i < _deviceTwinCount; i++) {
		OpenDeviceTwin(_deviceTwins[i]);
	}
}

void CloseDeviceTwinSet(void) {
	for (int i = 0; i < _deviceTwinCount; i++) { CloseDeviceTwin(_deviceTwins[i]); }
}

void OpenDeviceTwin(DeviceTwinBinding* deviceTwinBinding) {
	if (deviceTwinBinding->twinType == TYPE_UNKNOWN) {
		Log_Debug("\n\nDevice Twin '%s' missing type information.\nInclude .twinType option in DeviceTwinBinding definition.\nExample .twinType=TYPE_BOOL. Valid types include TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING.\n\n", deviceTwinBinding->twinProperty);
		Terminate();
	}

	// types JSON and String allocated dynamically when called in azure_iot.c
	switch (deviceTwinBinding->twinType) {
	case TYPE_INT:
		deviceTwinBinding->twinState = malloc(sizeof(int));
		break;
	case TYPE_FLOAT:
		deviceTwinBinding->twinState = malloc(sizeof(float));
		break;
	case TYPE_BOOL:
		deviceTwinBinding->twinState = malloc(sizeof(bool));
		break;
	default:
		break;
	}
}

void CloseDeviceTwin(DeviceTwinBinding* deviceTwinBinding) {
	if (deviceTwinBinding->twinState != NULL) {
		free(deviceTwinBinding->twinState);
		deviceTwinBinding->twinState = NULL;
	}
}

/// <summary>
///     Callback invoked when a Device Twin update is received from IoT Hub.
/// </summary>
/// <param name="payload">contains the Device Twin JSON document (desired and reported)</param>
/// <param name="payloadSize">size of the Device Twin JSON document</param>
void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
	size_t payloadSize, void* userContextCallback) {
	JSON_Value* root_value = NULL;
	JSON_Object* root_object = NULL;

	char* payLoadString = (char*)malloc(payloadSize + 1);
	if (payLoadString == NULL) {
		goto cleanup;
	}

	memcpy(payLoadString, payload, payloadSize);
	payLoadString[payloadSize] = 0; //null terminate string

	root_value = json_parse_string(payLoadString);
	if (root_value == NULL) {
		goto cleanup;
	}

	root_object = json_value_get_object(root_value);
	if (root_object == NULL) {
		goto cleanup;
	}

	JSON_Object* desiredProperties = json_object_dotget_object(root_object, "desired");
	if (desiredProperties == NULL) {
		desiredProperties = root_object;
	}

	for (int i = 0; i < _deviceTwinCount; i++) {
		JSON_Object* currentJSONProperties = json_object_dotget_object(desiredProperties, _deviceTwins[i]->twinProperty);
		if (currentJSONProperties != NULL) {
			SetDesiredState(currentJSONProperties, _deviceTwins[i]);
		}
	}

cleanup:
	// Release the allocated memory.
	if (root_value != NULL) {
		json_value_free(root_value);
	}

	if (payLoadString != NULL) {
		free(payLoadString);
		payLoadString = NULL;
	}
}

/// <summary>
///     Checks to see if the device twin twinProperty(name) is found in the json object. If yes, then act upon the request
/// </summary>
void SetDesiredState(JSON_Object* jsonObject, DeviceTwinBinding* deviceTwinBinding) {

	switch (deviceTwinBinding->twinType) {
	case TYPE_INT:
		if (json_object_has_value_of_type(jsonObject, "value", JSONNumber)) {
			*(int*)deviceTwinBinding->twinState = (int)json_object_get_number(jsonObject, "value");

			if (deviceTwinBinding->handler != NULL) {
				deviceTwinBinding->handler(deviceTwinBinding);
			}
			TwinReportState(deviceTwinBinding);
		}
		break;
	case TYPE_FLOAT:
		if (json_object_has_value_of_type(jsonObject, "value", JSONNumber)) {
			*(float*)deviceTwinBinding->twinState = (float)json_object_get_number(jsonObject, "value");

			if (deviceTwinBinding->handler != NULL) {
				deviceTwinBinding->handler(deviceTwinBinding);
			}
			TwinReportState(deviceTwinBinding);
		}
		break;
	case TYPE_BOOL:
		if (json_object_has_value_of_type(jsonObject, "value", JSONBoolean)) {
			*(bool*)deviceTwinBinding->twinState = (bool)json_object_get_boolean(jsonObject, "value");

			if (deviceTwinBinding->handler != NULL) {
				deviceTwinBinding->handler(deviceTwinBinding);
			}
			TwinReportState(deviceTwinBinding);
		}
		break;
	case TYPE_STRING:
		if (json_object_has_value_of_type(jsonObject, "value", JSONString)) {
			deviceTwinBinding->twinState = (char*)json_object_get_string(jsonObject, "value");

			if (deviceTwinBinding->handler != NULL) {
				deviceTwinBinding->handler(deviceTwinBinding);
			}
			TwinReportState(deviceTwinBinding);
			deviceTwinBinding->twinState = NULL;
		}
		break;
	default:
		break;
	}
}

bool DeviceTwinReportState(DeviceTwinBinding* deviceTwinBinding, void* state) {
	int len = 0;

	if (deviceTwinBinding == NULL) {
		return false;
	}

	if (!ConnectToAzureIot()) {
		return false;
	}

	static char reportedPropertiesString[DEVICE_TWIN_REPORT_LEN] = { 0 };
	switch (deviceTwinBinding->twinType) {
	case TYPE_INT:
		*(int*)deviceTwinBinding->twinState = *(int*)state;
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":%d}", deviceTwinBinding->twinProperty,
			(*(int*)deviceTwinBinding->twinState));
		break;
	case TYPE_FLOAT:
		*(float*)deviceTwinBinding->twinState = *(float*)state;
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":%f}", deviceTwinBinding->twinProperty,
			(*(float*)deviceTwinBinding->twinState));
		break;
	case TYPE_BOOL:
		*(bool*)deviceTwinBinding->twinState = *(bool*)state;
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":%s}", deviceTwinBinding->twinProperty,
			(*(bool*)deviceTwinBinding->twinState ? "true" : "false"));
		break;
	case TYPE_STRING:
		deviceTwinBinding->twinState = NULL;
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":\"%s\"}", deviceTwinBinding->twinProperty, (char*)state);
		break;
	case TYPE_UNKNOWN:
		Log_Debug("Device Twin Type Unknown");
		break;
	default:
		break;
	}

	if (len == 0) { return false; }

	return DeviceTwinUpdateReportedState(reportedPropertiesString);
}

bool TwinReportState(DeviceTwinBinding* deviceTwinBinding) {
	int len = 0;

	if (!ConnectToAzureIot()) {
		return false;
	}

	static char reportedPropertiesString[DEVICE_TWIN_REPORT_LEN] = { 0 };

	switch (deviceTwinBinding->twinType) {
	case TYPE_INT:
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":%d}", deviceTwinBinding->twinProperty,
			(*(int*)deviceTwinBinding->twinState));
		break;
	case TYPE_FLOAT:
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":%f}", deviceTwinBinding->twinProperty,
			(*(float*)deviceTwinBinding->twinState));
		break;
	case TYPE_BOOL:
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":%s}", deviceTwinBinding->twinProperty,
			(*(bool*)deviceTwinBinding->twinState ? "true" : "false"));
		break;
	case TYPE_STRING:
		len = snprintf(reportedPropertiesString, DEVICE_TWIN_REPORT_LEN, "{\"%s\":\"%s\"}", deviceTwinBinding->twinProperty,
			(deviceTwinBinding->twinState));
		break;
	default:
		break;
	}

	if (len == 0) { return false; }

	return DeviceTwinUpdateReportedState(reportedPropertiesString);

}

bool DeviceTwinUpdateReportedState(char* reportedPropertiesString) {
	if (IoTHubDeviceClient_LL_SendReportedState(
		GetAzureIotClientHandle(), (unsigned char*)reportedPropertiesString,
		strlen(reportedPropertiesString), DeviceTwinsReportStatusCallback, 0) != IOTHUB_CLIENT_OK) {
		Log_Debug("ERROR: failed to set reported state for '%s'.\n", reportedPropertiesString);
		return false;
	}
	else {
		Log_Debug("INFO: Reported state updated '%s'.\n", reportedPropertiesString);
		return true;
	}
}


/// <summary>
///     Callback invoked when the Device Twin reported properties are accepted by IoT Hub.
/// </summary>
void DeviceTwinsReportStatusCallback(int result, void* context) {
	Log_Debug("INFO: Device Twin reported properties update result: HTTP status code %d\n", result);
}