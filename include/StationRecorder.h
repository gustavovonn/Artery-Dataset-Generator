#ifndef STATION_RECORDER_H_
#define STATION_RECORDER_H_

#include "artery/application/ItsG5BaseService.h"

#include <omnetpp/simtime.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace artery
{
class CaObject;
class CpObject;
class LocalEnvironmentModel;
class VehicleDataProvider;
}  // namespace artery

namespace traci
{
class VehicleController;
}

namespace dataset
{

class DatasetSink;

// Per-vehicle recorder of sent/received CAMs and CPMs, own pose and vehicle attributes
class StationRecorder : public artery::ItsG5BaseService
{
public:
    using artery::ItsG5BaseService::receiveSignal;

    bool requiresListener() const override { return false; }
    void trigger() override;
    void receiveSignal(omnetpp::cComponent*, omnetpp::simsignal_t, omnetpp::cObject*, omnetpp::cObject*) override;

protected:
    void initialize() override;

private:
    struct Oracle {
        long station = -1;
        bool hasHeading = false;
        double headingRad = 0.0;  // clockwise from grid north
        std::vector<std::string> sensors;
    };
    using OracleMap = std::map<std::pair<long, long>, Oracle>;  // Artery (xCm, yCm) -> object

    void recordVehicle();
    void recordPose();
    void recordCamSent(const artery::CaObject&);
    void recordCpmSent(const artery::CpObject&);
    void recordReception(const char* msg, uint32_t sender, uint64_t key);
    OracleMap buildOracle() const;
    double gridBearing();

    DatasetSink* mSink = nullptr;
    const artery::VehicleDataProvider* mVdp = nullptr;
    const traci::VehicleController* mController = nullptr;
    const artery::LocalEnvironmentModel* mLem = nullptr;
    std::string mCpServiceName;
    omnetpp::simtime_t mPoseInterval;
    omnetpp::simtime_t mLastPose;
    bool mVehicleRecorded = false;
    bool mGridBearingKnown = false;
    double mGridBearing = 0.0;  // [rad] true bearing of grid north
};

}  // namespace dataset

#endif /* STATION_RECORDER_H_ */
