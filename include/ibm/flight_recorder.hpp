#pragma once

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#define FLIGHT_RECORDER_MAX_ENTRIES 300

namespace bmcweb
{
namespace flightrecorder
{
using FlightRecorderData = nlohmann::json;
using FlightRecorderTimeStamp = std::string;
using FlightRecorderRecord =
    std::tuple<FlightRecorderTimeStamp, FlightRecorderData>;
using FlightRecorderCassette = std::vector<FlightRecorderRecord>;
static constexpr auto flightRecorderDumpPath = "/tmp/redfish_events_flight_recorder";

std::string getCurrentSystemTime()
{
    using namespace std::chrono;
    const time_point<system_clock> tp = system_clock::now();
    std::time_t tt = system_clock::to_time_t(tp);
    auto ms = duration_cast<microseconds>(tp.time_since_epoch()) -
              duration_cast<seconds>(tp.time_since_epoch());

    std::stringstream ss;
    ss << std::put_time(std::localtime(&tt), "%F %Z %T.")
       << std::to_string(ms.count());
    return ss.str();
}

/** @class FlightRecorder
 *
 *  The class for implementing the BMCWEB flight recorder logic. This class
 *  handles the insertion of the data into the recorder and also provides
 *  API's to dump the flight recorder into a file.
 */

class FlightRecorder
{
  private:
    FlightRecorder() : index(0)
    {
        flightRecorderPolicy = FLIGHT_RECORDER_MAX_ENTRIES ? true : false;
        if (flightRecorderPolicy)
        {
            tapeRecorder = FlightRecorderCassette(FLIGHT_RECORDER_MAX_ENTRIES);
        }
    }

  protected:
    unsigned int index;
    FlightRecorderCassette tapeRecorder;
    bool flightRecorderPolicy;

  public:
    FlightRecorder(const FlightRecorder&) = delete;
    FlightRecorder(FlightRecorder&&) = delete;
    FlightRecorder& operator=(const FlightRecorder&) = delete;
    FlightRecorder& operator=(FlightRecorder&&) = delete;
    ~FlightRecorder() = default;

    static FlightRecorder& GetInstance()
    {
        static FlightRecorder flightRecorder;
        return flightRecorder;
    }
 
    /** @brief Add records to the flightRecorder
     *
     *  @param[in] eventJson  - The redfish event json data
     *
     *  @return void
     */
    void saveRecord(const FlightRecorderData& eventJson)
    {
        // if the flight recorder policy is enabled, then only insert the
        // messages into the flight recorder, if not this function will be just
        // a no-op
        if (flightRecorderPolicy)
        {
    	    unsigned int currentIndex = index++;
            tapeRecorder[currentIndex] = std::make_tuple(
                getCurrentSystemTime(), eventJson);
            index = (currentIndex == FLIGHT_RECORDER_MAX_ENTRIES - 1) ? 0
                                                                      : index;
        }
    }

    /** @brief play flight recorder
     *
     *  @return void
     */

    void playRecorder()
    {
        if (flightRecorderPolicy)
        {
            std::ofstream recorderOutputFile(flightRecorderDumpPath);
            for (const auto& message : tapeRecorder)
            {
                recorderOutputFile << std::get<0>(message)
                                  << " : ";
		recorderOutputFile << std::get<1>(message) << "\n";
                recorderOutputFile << std::endl;
            }
            recorderOutputFile.close();
        }
    }
};

} // namespace flightrecorder
} // namespace bmcweb
