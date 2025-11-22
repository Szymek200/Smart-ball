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

void storage::sendWifi()
{

}


