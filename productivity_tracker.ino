#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "arduino_secrets.h"

// Configuration flags
const bool TEST_MODE = false; // run tests - note that very little time went into this
const bool DEBUGGING_MODE = false; // send debug logs to Serial - note that requires Serial listening in order to start
const bool NO_LOG_SENDING_MODE = false; // don't send logs to IFTTT or GraphQL

// Replace with your IFTTT webhook details
const char* ifttt_host = "maker.ifttt.com";
const int ifttt_port = 443;
const char* webhookPath = SECRET_IFTTT_WEBHOOK_PATH;

// Replace with your network credentials
const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASSWORD;

// GraphQL details
const char* graphql_host = SECRET_GRAPHQL_HOST;
const int graphql_port = 443;
const char* graphql_path = SECRET_GRAPHQL_PATH;
const char* graphql_auth_token = SECRET_GRAPHQL_AUTH_TOKEN;

// time tracking
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Structure for logging
struct Log {
  unsigned long created_at; // Real-time timestamp in seconds since epoch
  unsigned long started_at; // Real-time timestamp in seconds since epoch, can be backdated
  unsigned long time_since_reboot; // Seconds since the last device restart
  uint8_t project;
  uint8_t state;
};

// Structure for projects and states
struct Project {
  uint8_t index;
  const char* name;
  uint8_t color[3];
  const char* states[5];
  bool hasMultipleStates;
};

// Define empty states
const char* emptyStates[5] = {"Default", "", "", "", ""};

// Define default states
const char* defaultStates[5] = {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"};

// Define projects and their states
Project projects[] = {
  {0, "Idle", {63, 0, 0}, {emptyStates[0], emptyStates[1], emptyStates[2], emptyStates[3], emptyStates[4]}, false},
  {1, "Distracted", {255, 0, 0}, {emptyStates[0], emptyStates[1], emptyStates[2], emptyStates[3], emptyStates[4]}, false},
  {2, "Basic human needs", {255, 127, 0}, {emptyStates[0], emptyStates[1], emptyStates[2], emptyStates[3], emptyStates[4]}, false},
  {3, "Entertainment", {255, 255, 0}, {emptyStates[0], emptyStates[1], emptyStates[2], emptyStates[3], emptyStates[4]}, false},
  {4, "Family", {0, 255, 0}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true},
  {5, "Personal", {0, 0, 255}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true},
  {6, "Work", {143, 0, 255}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true},
  {7, "Project7", {255, 0, 255}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true},
  {8, "Project8", {0, 255, 255}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true},
  {9, "Project9", {255, 255, 255}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true}
};

// Previous pressed button index
byte previousPressedButtonIndex = 0;

const unsigned long BACKOFF_TIME = 3000; // Backoff time (in milliseconds)
unsigned long lastButtonPressTime = 0; // Time of the last button press
unsigned long firstButtonPressTime = 0; // Time of the first button press in a sequence
bool logPending = false; // Whether a log is pending submission
bool awaitingSecondDigit = false; // Whether we are waiting for a second digit
char firstDigit = '\0'; // First digit of the project selection
int backdateCount = 0; // Count of how many times the backdate button was pressed

uint8_t project = 0;
uint8_t state = 0;
Log currentLog;
bool currentLogWasSubmittedToGraphQL = false;

// RGB LED class
class RgbLed {
  const byte pinR;
  const byte pinG;
  const byte pinB;
  const float multiplierR;
  const float multiplierG;
  const float multiplierB;

public:
  RgbLed(byte r, byte g, byte b, float multR, float multG, float multB) 
    : pinR(r), pinG(g), pinB(b), multiplierR(multR), multiplierG(multG), multiplierB(multB) {}

  void setup() {
    pinMode(pinR, OUTPUT);
    pinMode(pinG, OUTPUT);
    pinMode(pinB, OUTPUT);
  }

  void setColor(uint8_t red, uint8_t green, uint8_t blue) {
    analogWrite(pinR, (uint8_t)(red * multiplierR));
    analogWrite(pinG, (uint8_t)(green * multiplierG));
    analogWrite(pinB, (uint8_t)(blue * multiplierB));
  }
};

// State LED class
class StateLed {
  const byte pins[5];

public:
  StateLed(const byte p[5]) : pins{p[0], p[1], p[2], p[3], p[4]} {}

  void setup() {
    for (int i = 0; i < 5; i++) {
      pinMode(pins[i], OUTPUT);
    }
  }

  void updateStateLeds(uint8_t project, uint8_t state) {
    // Ensure only one state LED is on at a time due to shared resistor
    setStateLeds(false, false, false, false, false);
    if (project == 0 || project == 1 || project == 2 || project == 3) {
      // For these projects, no state LEDs should be on
      return;
    }

    switch (state) {
      case 0: setStateLeds(true, false, false, false, false); break;
      case 1: setStateLeds(false, true, false, false, false); break;
      case 2: setStateLeds(false, false, true, false, false); break;
      case 3: setStateLeds(false, false, false, true, false); break;
      case 4: setStateLeds(false, false, false, false, true); break;
    }
  }

private:
  // This is private so that we can ensure only one state LED is on at a time due to shared resistor
  void setStateLeds(bool led0, bool led1, bool led2, bool led3, bool led4) {
    digitalWrite(pins[0], led0 ? HIGH : LOW);
    digitalWrite(pins[1], led1 ? HIGH : LOW);
    digitalWrite(pins[2], led2 ? HIGH : LOW);
    digitalWrite(pins[3], led3 ? HIGH : LOW);
    digitalWrite(pins[4], led4 ? HIGH : LOW);
  }
};

// Function to log messages if debugging mode is enabled
void debugLog(const String& message) {
  if (DEBUGGING_MODE) {
    Serial.print(message);
  }
}

// Function to log messages with newline if debugging mode is enabled
void debugLogLn(const String& message) {
  if (DEBUGGING_MODE) {
    Serial.println(message);
  }
}

// Function to convert IP address to string
String ip2Str(IPAddress ip) {
  String s = "";
  for (int i = 0; i < 4; i++) {
    s += i ? "." + String(ip[i]) : String(ip[i]);
  }
  return s;
}

// Network class
class Network {
  WiFiSSLClient wifiClient;
  HttpClient httpClient;
  HttpClient graphqlClient;
  const char* ssid;
  const char* password;

public:
  Network(const char* ssid, const char* password)
    : ssid(ssid), password(password), httpClient(wifiClient, ifttt_host, ifttt_port), 
      graphqlClient(wifiClient, graphql_host, graphql_port) {}

  void setup() {
    debugLogLn("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      debugLog(".");
    }
    debugLogLn("Connected to WiFi!");
    debugLogLn("Device IP Address: ");
    debugLog(ip2Str(WiFi.localIP()));
  }

  bool sendRequest(const String& jsonPayload) {
    return performHTTPRequest(httpClient, webhookPath, ifttt_host, jsonPayload);
  }

  bool sendGraphQLRequest(const String& jsonPayload) {
    return performHTTPRequest(graphqlClient, graphql_path, graphql_host, jsonPayload, graphql_auth_token);
  }

private:
  bool performHTTPRequest(HttpClient &client, const char* path, const char* host, const String& jsonPayload, const char* authToken = nullptr) {
    if (NO_LOG_SENDING_MODE) {
      return true;
    }

    toggleLED(true);

    client.beginRequest();
    client.post(path);
    client.sendHeader("Host", host);
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Content-Length", jsonPayload.length());
    client.sendHeader("Connection", "close");
    if (authToken) {
      client.sendHeader("Authorization", authToken);
    }
    client.beginBody();
    client.print(jsonPayload);
    client.endRequest();

    int statusCode = client.responseStatusCode();
    String response = client.responseBody();

    debugLogLn("Status code: " + String(statusCode));
    debugLogLn("Response: " + response);

    toggleLED(false);

    return statusCode == 200;
  }

  void toggleLED(bool state) {
    digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
  }
};


// RequestBuilder class
class RequestBuilder {
public:
  static String buildLogPayload(Log log) {
    String jsonPayload = "{";
    jsonPayload += "\"created_at\":" + String(log.created_at) + ",";
    jsonPayload += "\"started_at\":" + String(log.started_at) + ",";
    jsonPayload += "\"time_since_reboot\":" + String(log.time_since_reboot) + ",";
    jsonPayload += "\"project\":" + String(log.project) + ",";
    jsonPayload += "\"state\":" + String(log.state);
    jsonPayload += "}";
    return jsonPayload;
  }

  // time is expect to be milliseconds since start of epoch
  static String buildGraphQLPayload(Log log) {
    String jsonPayload = "{";
    jsonPayload += "\"query\":\"mutation ($NewProcrastinationLogInput: NewProcrastinationLogInput!) { newProcrastinationLog(NewProcrastinationLogInput: $NewProcrastinationLogInput) { success errors { message queryPathKey __typename } instanceIdToNavigateTo __typename } }\",";
    jsonPayload += "\"variables\":{\"NewProcrastinationLogInput\":{\"title\":\"Procrastination alert!\",\"description\":\"Tom has been procrastinating for more than 5 minutes now! \[Sent from Tom's Arduino\]\",\"startTime\":\"" + String(log.started_at) + "000\"}}";
    jsonPayload += "}";
    return jsonPayload;
  }
};

// Keypad class
class Keypad {
  const int numButtons;
  const int* measuredVals;
  const byte* signalToButtonMap;
  const char* buttonLabels[17];
  byte previousButtonIndex;
  int analogPin; // The analog pin to read from

public:
  Keypad(int numButtons, const int* measuredVals, const byte* signalToButtonMap, const char* buttonLabels[17], int analogPin)
    : numButtons(numButtons), measuredVals(measuredVals), signalToButtonMap(signalToButtonMap), analogPin(analogPin), previousButtonIndex(0) {
    for (int i = 0; i < 17; i++) {
      this->buttonLabels[i] = buttonLabels[i];
    }
  }

  // return 0 on no pressed button - not to be confused with '0'!
  char getPressedButton() {
    int analogValue = analogRead(analogPin);
    int signalIndex = signalToSignalIndex(analogValue);
    if (signalIndex == 0) {
      previousButtonIndex = 0;
      return 0;
    }
    byte buttonIndex = signalToButtonMap[signalIndex];
    if (buttonIndex != previousButtonIndex) {
      previousButtonIndex = buttonIndex;
      debugLogLn(
        "Analog value: " + String(analogValue)
        + ", Signal index: " + String(signalIndex)
        + ", Button index: " + String(buttonIndex)
      );
      return buttonLabels[buttonIndex][0]; // Correctly return the character
    } else {
      return 0;
    }
  }

private:
  int signalToSignalIndex(int analogValue) {
    int upperBound;
    int lowerBound;
    for (int i = 0; i < numButtons; i++) {
      upperBound = (i == 0) ? 1024 : (measuredVals[i - 1] + measuredVals[i]) / 2;
      lowerBound = (i == numButtons - 1) ? measuredVals[i] / 2 : (measuredVals[i] + measuredVals[i + 1]) / 2;
      if (analogValue >= lowerBound && analogValue < upperBound) return i + 1;
    }
    return 0;
  }
};

// Define the number of buttons and their analog values
const int numberOfButtons = 16;
const int measuredValues[numberOfButtons] = {1015, 927, 835, 726, 674, 640, 588, 535, 509, 485, 458, 427, 407, 390, 370, 350};

// Mapping signal index to button index
// this should be precisely pins inserted the other way round
const byte signalIndexToButtonIndex[numberOfButtons + 1] = {0, 16, 12, 8, 4, 15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1};

// Button labels
const char* buttonLabels[17] = {"", "1", "2", "3", "A", "4", "5", "6", "B", "7", "8", "9", "C", "*", "0", "#", "D"};

// Global instances
RgbLed projectLed(3, 5, 4, 0.2, 1, 0.15);
const byte stateLedPins[5] = {2, 1, 0, 7, 8};
StateLed stateLed(stateLedPins);
Network network(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);
Keypad keypad(numberOfButtons, measuredValues, signalIndexToButtonIndex, buttonLabels, A1);

// Function prototypes
void handleButtonPress(char pressedButton);
void selectProject(uint8_t selectedProject);
void selectState(uint8_t selectedState);
bool logProjectAndState();
bool createGraphQLEntry();
void runTestMode();
bool test();
Project getProjectById(uint8_t projectId);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  if (TEST_MODE) {
    runTestMode();
    return;
  }

  projectLed.setup();
  stateLed.setup();
  
  if (DEBUGGING_MODE) {
    Serial.begin(9600);
    while (!Serial) {
      delay(10);
    }
    debugLogLn("Serial communication started.");
  }

  network.setup();

  timeClient.begin();
  currentLog = createCurrentLog(); // will also run the first timeClient.update()
  // note that the first log is not sent anywhere

  projectLed.setColor(projects[project].color[0], projects[project].color[1], projects[project].color[2]);
  stateLed.updateStateLeds(project, state);
}

void loop() {
  if (TEST_MODE) {
    delay(100);
    return;
  }
  
  char pressedButton = keypad.getPressedButton();
  
  if (pressedButton != 0) {
    debugLogLn("Button pressed: " + String(pressedButton));
    handleButtonPress(pressedButton);
    lastButtonPressTime = millis();
    logPending = true;
    delay(200); // Add delay to prevent multiple rapid clicks
  }

  // Check if backoff time has passed and a log is pending
  if (logPending && (millis() - lastButtonPressTime >= BACKOFF_TIME)) {
    if (project != currentLog.project || state != currentLog.state) {
      currentLog = createCurrentLog();
      currentLogWasSubmittedToGraphQL = false;
      logProjectAndState(currentLog);
    }
    logPending = false;
  }

  // Check if the second digit timeout has passed
  if (awaitingSecondDigit && (millis() - firstButtonPressTime >= BACKOFF_TIME)) {
    selectProject(firstDigit - '0');
    awaitingSecondDigit = false;
  }

  // Check if there is a 5-minute procrastination log ready to be submitted
  if (currentLog.project == 1 && !currentLogWasSubmittedToGraphQL && ((millis()/1000 - currentLog.time_since_reboot) >= 300)) {
    currentLogWasSubmittedToGraphQL = true;
    createGraphQLEntry(currentLog);
  }

  delay(50);
}

/**
 * Handles the button press logic, including project and state selection.
 * @param pressedButton The character of the pressed button.
 */
void handleButtonPress(char pressedButton) {
  if (pressedButton >= '0' && pressedButton <= '9') {
    if (awaitingSecondDigit) {
      uint8_t projectId = (firstDigit - '0') * 10 + (pressedButton - '0');
      selectProject(projectId);
      awaitingSecondDigit = false;
    } else {
      firstDigit = pressedButton;
      firstButtonPressTime = millis();
      awaitingSecondDigit = true;
      selectProject(firstDigit - '0'); // Initially select the single-digit project
    }
  } else {
    awaitingSecondDigit = false;
    switch (pressedButton) {
      case 'A': selectState(0); break;
      case 'B': selectState(1); break;
      case 'C': selectState(2); break;
      case 'D': selectState(3); break;
      case '#': selectState(4); break;
      case '*':
        backdateCount++;
        break;
    }
  }
}

/**
 * Retrieves the project by its ID. If not found, returns a default project.
 * @param projectId The ID of the project.
 * @return The project with the specified ID, or a default project if not found.
 */
Project getProjectById(uint8_t projectId) {
  for (int i = 0; i < sizeof(projects)/sizeof(projects[0]); i++) {
    if (projects[i].index == projectId) {
      return projects[i];
    }
  }
  // If not found, return a default project
  Project defaultProject = {projectId, "Unknown Project", {255, 255, 255}, {defaultStates[0], defaultStates[1], defaultStates[2], defaultStates[3], defaultStates[4]}, true};
  return defaultProject;
}

/**
 * Selects a project based on the given project ID.
 * @param selectedProject The ID of the project to select.
 */
void selectProject(uint8_t selectedProject) {
  Project proj = getProjectById(selectedProject);
  if (project != proj.index) {
    project = proj.index;
    debugLogLn("Updating project to: " + String(proj.name));
    state = 0;
    projectLed.setColor(proj.color[0], proj.color[1], proj.color[2]);
    stateLed.updateStateLeds(project, state);
    backdateCount = 0; // Reset backdate count on project change
  }
}

/**
 * Selects a state based on the given state ID.
 * @param selectedState The ID of the state to select.
 */
void selectState(uint8_t selectedState) {
  if (state != selectedState) {
    state = selectedState;
    debugLogLn("Updating state to: " + String(projects[project].states[state]));
    stateLed.updateStateLeds(project, state);
    backdateCount = 0; // Reset backdate count on state change
  }
}

/**
 * Creates the current Log, considering any backdate adjustments.
 * @return the Log
 */
Log createCurrentLog() {
  timeClient.update();
  unsigned long time_since_reboot = millis() / 1000; // Current uptime in seconds
  unsigned long created_at = timeClient.getEpochTime(); // Get current epoch time from NTP server in seconds
  unsigned long started_at = created_at - (backdateCount * 300); // Adjust for any backdate
  
  backdateCount = 0; // Reset after logging
  
  return {created_at, started_at, time_since_reboot, project, state};
}

/**
 * Logs the current Log with IFTTT webhook
 * @return True if the log was successfully sent, false otherwise.
 */
bool logProjectAndState(Log log) {
  return network.sendRequest(RequestBuilder::buildLogPayload(log));
}

/**
 * Logs the current Log with GraphQL endpoint
 * @return True if the log was successfully sent, false otherwise.
 */
bool createGraphQLEntry(Log log) {
  return network.sendGraphQLRequest(RequestBuilder::buildGraphQLPayload(log));
}

void runTestMode() {
  // first test out the RGB light to make sure we can see if tests succeeded!
  projectLed.setColor(255, 0, 0);
  delay(500);
  projectLed.setColor(0, 255, 0);
  delay(500);
  projectLed.setColor(0, 0, 255);
  delay(500);
  if (test()) {
    projectLed.setColor(0, 255, 0);
  } else {
    projectLed.setColor(255, 0, 0);
  }
}

bool test() {
  // Implement actual test logic here
  return true;
}
