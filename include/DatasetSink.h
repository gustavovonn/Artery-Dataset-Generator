#ifndef DATASET_SINK_H_
#define DATASET_SINK_H_

#include <omnetpp/csimplemodule.h>

#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace dataset
{

// Owns the dataset files of one run; per-station StationRecorders write through it
class DatasetSink : public omnetpp::cSimpleModule
{
public:
    enum class File { CamSent, CpmSent, Reception, Pose, Vehicles };

    bool isRecording() const;
    // dataset time: 0 at startTime
    double time(omnetpp::simtime_t) const;
    void write(File, const std::string& line);

    // message ids are global; (station, key) resolves a received message to its id
    uint64_t registerMessage(File, uint32_t station, uint64_t key);
    bool lookupMessage(File, uint32_t station, uint64_t key, uint64_t& id) const;

protected:
    void initialize() override;
    void finish() override;

private:
    using Recent = std::deque<std::pair<uint64_t, uint64_t>>;  // key, message id

    std::unordered_map<uint32_t, Recent>& recent(File);
    const std::unordered_map<uint32_t, Recent>& recent(File) const;
    std::ofstream& stream(File);
    void writeMetadata() const;

    std::string mOutputDir;
    omnetpp::simtime_t mStartTime;
    std::ofstream mCamSent;
    std::ofstream mCpmSent;
    std::ofstream mReception;
    std::ofstream mPose;
    std::ofstream mVehicles;
    std::unordered_map<uint32_t, Recent> mRecentCams;
    std::unordered_map<uint32_t, Recent> mRecentCpms;
    std::map<File, uint64_t> mRows;
    uint64_t mNextMessageId = 1;
};

}  // namespace dataset

#endif /* DATASET_SINK_H_ */
