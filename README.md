# Productivity Tracker

This project is a productivity tracker designed for the Arduino MKR WiFi 1010. It tracks different productivity states and projects, logs the data to an IFTTT webhook, and can send a GraphQL request for specific project IDs.

## Table of Contents
- [Hardware Requirements](#hardware-requirements)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [Code Explanation](#code-explanation)
- [Assumptions](#assumptions)
  - [GraphQL Interface](#graphql-interface)
  - [IFTTT Hook](#ifttt-hook)
- [Contributing](#contributing)
- [License](#license)

## Hardware Requirements

- **Arduino MKR WiFi 1010**: The main microcontroller board used for this project.
- **Keypad**: Used for inputting project and state selections.
- **RGB LED**: Indicates the current project with different colors.
- **State LEDs**: Indicate the current state within a project.
- **WiFi Connectivity**: Required for sending logs to IFTTT and GraphQL endpoint.

## Features

- **Project and State Tracking**: Track different projects and their states using a keypad.
- **LED Indicators**: Visual feedback using RGB and state LEDs.
- **Logging to IFTTT**: Send logs to an IFTTT webhook.
- **GraphQL Integration**: Send a GraphQL request for specific project IDs.
- **RTC Time Management**: Fetch time from an NTP server and manage time using RTC.

## Installation

1. **Clone the Repository**:
    ```sh
    git clone https://github.com/DiscreteConcrete/arduino_productivity_tracker.git
    cd arduino_productivity_tracker
    ```

2. **Install Arduino Libraries**:
    - WiFiNINA
    - ArduinoHttpClient
    - RTCZero
    - NTPClient

    You can install these libraries through the Arduino Library Manager.

3. **Configure Secrets**:
    - Create a `secrets.h` file and define the following secrets:
      ```cpp
      #define SECRET_WIFI_SSID "your_wifi_ssid"
      #define SECRET_WIFI_PASSWORD "your_wifi_password"
      #define SECRET_IFTTT_WEBHOOK_PATH "/trigger/productivity_track/json/with/key/your_ifttt_key"
      #define SECRET_GRAPHQL_HOST "your_graphql_host"
      #define SECRET_GRAPHQL_PATH "/api/v1/apps/gql/your_graphql_product_id"
      #define SECRET_GRAPHQL_AUTH_TOKEN "your_graphql_auth_token"
      ```

4. **Upload the Code**:
    - Open the project in the Arduino IDE.
    - Select the correct board and port: `Tools` -> `Board` -> `Arduino MKR WiFi 1010`.
    - Upload the code to your Arduino MKR WiFi 1010.

## Usage

- **Start the Tracker**: Once the code is uploaded, the tracker will initialize and connect to WiFi.
- **Input Projects and States**: Use the keypad to select projects and states.
- **View LED Indicators**: The RGB LED indicates the current project, while state LEDs show the current state.
- **Logging**: Logs are automatically sent to the configured IFTTT webhook and GraphQL endpoint (for project ID 1).

## Code Explanation

### Main Components

1. **Project and State Structures**:
    - Projects and states are defined using structs to keep track of different productivity states.

2. **RGB LED and State LED Classes**:
    - `RgbLed` and `StateLed` classes manage the LED indicators for projects and states.

3. **Network Class**:
    - Manages WiFi connectivity and handles HTTP requests to IFTTT and GraphQL endpoints.

4. **Keypad Class**:
    - Handles input from the keypad and maps analog values to button presses.

5. **TimeManager Class**:
    - Fetches time from an NTP server and manages time using RTC.

### Functions

- `setup()`: Initializes the system, connects to WiFi, sets up LEDs, and initializes time management.
- `loop()`: Main loop handling button presses, logging, and updating LED indicators.
- `handleButtonPress(char pressedButton)`: Manages the logic for handling button presses on the keypad.
- `logProjectAndState()`: Logs the current project and state to the IFTTT webhook or GraphQL endpoint.

## Assumptions

### GraphQL Interface

- **Endpoint**: The GraphQL endpoint is assumed to be available at `https://your_graphql_host/api/v1/apps/gql/your_graphql_product_id`.
- **Authentication**: The endpoint requires an authentication token provided in the `Authorization` header.
- **Mutation**: The GraphQL mutation used to log data is as follows:
    ```graphql
    mutation ($NewProcrastinationLogInput: NewProcrastinationLogInput!) {
      newProcrastinationLog(NewProcrastinationLogInput: $NewProcrastinationLogInput) {
        success
        errors {
          message
          queryPathKey
          __typename
        }
        instanceIdToNavigateTo
        __typename
      }
    }
    ```
- **Variables**: The mutation expects a `NewProcrastinationLogInput` object which includes fields such as `title`, `description`, and `startTime` (epoch time in milliseconds).

### IFTTT Hook

- **Endpoint**: The IFTTT webhook is triggered at the path defined in `SECRET_IFTTT_WEBHOOK_PATH`.
- **Payload**: The payload sent to the IFTTT webhook is a JSON object containing the time (seconds since startup), project, and state.
- **Example Payload**:
    ```json
    {
      "time": 1250,
      "project": 1,
      "state": 0
    }
    ```

## Contributing

Contributions are welcome! Please open an issue or submit a pull request with your improvements or bug fixes.

## License

This project is licensed under the MIT License. See the `LICENSE` file for more details.
