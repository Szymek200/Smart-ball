#include "storage.h"

//przydaloby sie wielowatkowe zapis i odczyt w tym samym czasie.
//wszystkie pomiary zapisujemy do RAM

//first for nornalized acceleration, second gyroscope




//dokladne dane uderzenia
void storage::saveEvent(std::deque<Sample> &event, unsigned long duration) {
    // Write header
    std::string header = "HIT";
    size_t length = header.size();
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(header.data(), length);

    // Save duration
    file.write(reinterpret_cast<const char*>(&duration), sizeof(duration));

    // Save number of samples
    size_t numSamples = event.size();
    file.write(reinterpret_cast<const char*>(&numSamples), sizeof(numSamples));

    // Write each sample sequentially
    for (const auto &s : event) {
        file.write(reinterpret_cast<const char*>(&s.timestamp), sizeof(s.timestamp));

        file.write(reinterpret_cast<const char*>(&s.accel.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&s.accel.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&s.accel.z), sizeof(float));

        file.write(reinterpret_cast<const char*>(&s.gyro.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&s.gyro.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&s.gyro.z), sizeof(float));
    }

    // End marker
    std::string endMarker = "HIT_END";
    length = endMarker.size();
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(endMarker.data(), length);
}


void storage::sendEventWifi(const std::deque<Sample> &event, unsigned long duration) {
    if (!wifiClient || !wifiClient.connected())
        return;

    // header
    std::string header = "HIT";
    size_t length = header.size();
    wifiClient.write((uint8_t*)&length, sizeof(length));
    wifiClient.write((uint8_t*)header.data(), length);

    // duration
    wifiClient.write((uint8_t*)&duration, sizeof(duration));

    // sample count
    size_t numSamples = event.size();
    wifiClient.write((uint8_t*)&numSamples, sizeof(numSamples));

    for (const auto &s : event) {
        wifiClient.write((uint8_t*)&s.timestamp, sizeof(s.timestamp));
        wifiClient.write((uint8_t*)&s.accel.x, sizeof(float));
        wifiClient.write((uint8_t*)&s.accel.y, sizeof(float));
        wifiClient.write((uint8_t*)&s.accel.z, sizeof(float));
        wifiClient.write((uint8_t*)&s.gyro.x, sizeof(float));
        wifiClient.write((uint8_t*)&s.gyro.y, sizeof(float));
        wifiClient.write((uint8_t*)&s.gyro.z, sizeof(float));
    }

    // footer
    std::string endMarker = "HIT_END";
    length = endMarker.size();
    wifiClient.write((uint8_t*)&length, sizeof(length));
    wifiClient.write((uint8_t*)endMarker.data(), length);
}


//zapisanie pojedynczej probki
//gdy nic sie nie dzieje

// Save single background sample
void storage::saveSample(const Sample &s) {
    // Save timestamp first
    file.write(reinterpret_cast<const char*>(&s.timestamp), sizeof(s.timestamp));

    file.write(reinterpret_cast<const char*>(&s.accel.x), sizeof(float));
    file.write(reinterpret_cast<const char*>(&s.accel.y), sizeof(float));
    file.write(reinterpret_cast<const char*>(&s.accel.z), sizeof(float));

    file.write(reinterpret_cast<const char*>(&s.gyro.x), sizeof(float));
    file.write(reinterpret_cast<const char*>(&s.gyro.y), sizeof(float));
    file.write(reinterpret_cast<const char*>(&s.gyro.z), sizeof(float));
}

void storage::sendSampleWifi(const Sample &s) {
    if (!wifiClient || !wifiClient.connected())
        return;

    wifiClient.write((uint8_t*)&s.timestamp, sizeof(s.timestamp));
    wifiClient.write((uint8_t*)&s.accel.x, sizeof(float));
    wifiClient.write((uint8_t*)&s.accel.y, sizeof(float));
    wifiClient.write((uint8_t*)&s.accel.z, sizeof(float));
    wifiClient.write((uint8_t*)&s.gyro.x, sizeof(float));
    wifiClient.write((uint8_t*)&s.gyro.y, sizeof(float));
    wifiClient.write((uint8_t*)&s.gyro.z, sizeof(float));
}

void storage::setupWiFiAP() {
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(apSSID, apPassword);

    if (!ok) {
        Serial.println("AP start failed!");
    } else {
        Serial.println("WiFi AP started.");
        Serial.print("SSID: "); Serial.println(apSSID);
        Serial.print("IP: "); Serial.println(WiFi.softAPIP());
        Serial.print("AP IP address: ");

    }

    wifiServer.begin();
}

void storage::sendWifi() {
    if (!wifiClient || !wifiClient.connected()) {
        wifiClient = wifiServer.available(); // accept new client
        if (wifiClient) {
            Serial.println("WiFi client connected");
        }
    }
}


