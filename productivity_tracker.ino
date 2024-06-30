#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Int64String.h>
#include "arduino_secrets.h"

// Configuration flags
const bool TEST_MODE = false;
const bool DEBUGGING_MODE = true;
const bool NO_LOG_SENDING_MODE = false; // don't send logs to IFTTT

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
  uint16_t time;
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
  {0, "Idle", {63, 0, 0}, {"Default", "", "", "", ""}, false},
  {1, "Distracted", {255, 0, 0}, {"Default", "", "", "", ""}, false},
  {2, "Basic human needs", {255, 127, 0}, {"Default", "", "", "", ""}, false},
  {3, "Entertainment", {255, 255, 0}, {"Default", "", "", "", ""}, false},
  {4, "Family", {0, 255, 0}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true},
  {5, "Personal", {0, 0, 255}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true},
  {6, "Work", {143, 0, 255}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true},
  {7, "Project7", {255, 0, 255}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true},
  {8, "Project8", {0, 255, 255}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true},
  {9, "Project9", {255, 255, 255}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true}
};

// Previous pressed button index
byte previousPressedButtonIndex = 0;

const unsigned long BACKOFF_TIME = 2000; // Backoff time (in milliseconds)
unsigned long lastButtonPressTime = 0; // Time of the last button press
unsigned long firstButtonPressTime = 0; // Time of the first button press in a sequence
bool logPending = false; // Whether a log is pending submission
bool awaitingSecondDigit = false; // Whether we are waiting for a second digit
char firstDigit = '\0'; // First digit of the project selection
unsigned long previousLogTime = 0; // Time of the previous log
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

  void setStateLeds(bool led0, bool led1, bool led2, bool led3, bool led4) {
    // Ensure only one state LED is on at a time due to shared resistor
    digitalWrite(pins[0], led0 ? HIGH : LOW);
    digitalWrite(pins[1], led1 ? HIGH : LOW);
    digitalWrite(pins[2], led2 ? HIGH : LOW);
    digitalWrite(pins[3], led3 ? HIGH : LOW);
    digitalWrite(pins[4], led4 ? HIGH : LOW);
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
  const char* ssid;
  const char* password;

public:
  Network(const char* ssid, const char* password)
    : ssid(ssid), password(password), httpClient(wifiClient, ifttt_host, ifttt_port) {}

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
    if (NO_LOG_SENDING_MODE) {
      return true;
    }

    digitalWrite(LED_BUILTIN, HIGH); // Turn on the built-in LED during the request

    httpClient.beginRequest();
    httpClient.post(webhookPath);
    httpClient.sendHeader("Host", ifttt_host);
    httpClient.sendHeader("Content-Type", "application/json");
    httpClient.sendHeader("Content-Length", jsonPayload.length());
    httpClient.sendHeader("Connection", "close");
    httpClient.beginBody();
    httpClient.print(jsonPayload);
    httpClient.endRequest();

    int statusCode = httpClient.responseStatusCode();
    String response = httpClient.responseBody();

    debugLogLn("Status code: " + String(statusCode));
    debugLogLn("Response: " + response);

    digitalWrite(LED_BUILTIN, LOW); // Turn off the built-in LED after the request

    return statusCode == 200;
  }

  bool sendGraphQLRequest(const String& jsonPayload) {
    if (NO_LOG_SENDING_MODE) {
      return true;
    }

    digitalWrite(LED_BUILTIN, HIGH); // Turn on the built-in LED during the request

    HttpClient graphqlClient(wifiClient, graphql_host, graphql_port);
    graphqlClient.beginRequest();
    graphqlClient.post(graphql_path);
    graphqlClient.sendHeader("Host", graphql_host);
    graphqlClient.sendHeader("Content-Type", "application/json");
    graphqlClient.sendHeader("Authorization", graphql_auth_token);
    graphqlClient.sendHeader("Content-Length", jsonPayload.length());
    graphqlClient.sendHeader("Connection", "close");
    graphqlClient.beginBody();
    graphqlClient.print(jsonPayload);
    graphqlClient.endRequest();

    int statusCode = graphqlClient.responseStatusCode();
    String response = graphqlClient.responseBody();

    debugLogLn("GraphQL Status code: " + String(statusCode));
    debugLogLn("GraphQL Response: " + response);

    digitalWrite(LED_BUILTIN, LOW); // Turn off the built-in LED after the request

    return statusCode == 200;
  }
};

// RequestBuilder class
class RequestBuilder {
public:
  static String buildLogPayload(Log log) {
    String jsonPayload = "{";
    jsonPayload += "\"time\":" + String(log.time) + ",";
    jsonPayload += "\"project\":" + String(log.project) + ",";
    jsonPayload += "\"state\":" + String(log.state);
    jsonPayload += "}";
    return jsonPayload;
  }

  // time is expect to be milliseconds since start of epoch
  static String buildGraphQLPayload(Log log, uint64_t time) {
    String jsonPayload = "{";
    jsonPayload += "\"query\":\"mutation ($NewProcrastinationLogInput: NewProcrastinationLogInput!) { newProcrastinationLog(NewProcrastinationLogInput: $NewProcrastinationLogInput) { success errors { message queryPathKey __typename } instanceIdToNavigateTo __typename } }\",";
    jsonPayload += "\"variables\":{\"NewProcrastinationLogInput\":{\"title\":\"Procrastination alert!\",\"description\":\"Tom has been procrastinating for more than 5 minutes now! \[Sent from Tom's Arduino\]\",\"startTime\":\"" + int64String(time) + "\"}}";
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
RgbLed rgbLed(3, 5, 4, 0.2, 1, 0.15);
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

  rgbLed.setup();
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
  timeClient.update();

  if (!NO_LOG_SENDING_MODE) {
    network.sendRequest(RequestBuilder::buildLogPayload({0, project, state}));
  }

  rgbLed.setColor(projects[project].color[0], projects[project].color[1], projects[project].color[2]);
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
  if (currentLog.project == 1 && !currentLogWasSubmittedToGraphQL && ((millis()/1000 - currentLog.time) >= 300)) {
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
  Project defaultProject = {projectId, "Unknown Project", {255, 255, 255}, {"Deep work", "Getting organized", "Meetings", "Communication", "Bureaucracy"}, true};
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
    rgbLed.setColor(proj.color[0], proj.color[1], proj.color[2]);
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
Log createCurrentLog () {
  unsigned long currentTime = millis() / 1000;
  unsigned long logTime = currentTime - (backdateCount * 300); // 300 seconds = 5 minutes
  // Ensure the log time does not backdate to before the previous log time
  if (logTime <= previousLogTime) {
    logTime = previousLogTime + 1;
  }

  previousLogTime = logTime;
  backdateCount = 0; // Reset backdate count after logging

  return { logTime, project, state };
}

/**
 * Logs the current Log
 * @return True if the log was successfully sent, false otherwise.
 */
bool logProjectAndState(Log log) {
  return network.sendRequest(RequestBuilder::buildLogPayload(log));
}

bool createGraphQLEntry (Log log) {
  unsigned long currentTime = millis() / 1000;
  unsigned long backdateSeconds = currentTime - log.time; // how much time we actually backdated

  const unsigned long timeSeconds = timeClient.getEpochTime();
  uint64_t time = uint64_t(timeSeconds - backdateSeconds) * 1000;
  return network.sendGraphQLRequest(RequestBuilder::buildGraphQLPayload(log, time));
}

void runTestMode() {
  rgbLed.setColor(255, 255, 0);
  delay(500);
  rgbLed.setColor(0, 255, 0);
  delay(500);
  rgbLed.setColor(0, 0, 255);
  delay(500);
  if (test()) {
    rgbLed.setColor(0, 255, 0);
  } else {
    rgbLed.setColor(255, 0, 0);
  }
}

bool test() {
  // Implement actual test logic here
  return true;
}
