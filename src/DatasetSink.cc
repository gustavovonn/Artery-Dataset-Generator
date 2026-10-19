#include "DatasetSink.h"

#include "Json.h"

#include <omnetpp.h>

#include <filesystem>
#include <sstream>

namespace dataset
{

using namespace omnetpp;
namespace fs = std::filesystem;

Define_Module(DatasetSink)

static constexpr std::size_t kRecentPerStation = 32;

void DatasetSink::initialize()
{
    mOutputDir = par("outputDir").stdstringValue();
    mStartTime = par("startTime");

    fs::create_directories(mOutputDir);
    auto open = [this](std::ofstream& os, const char* name) {
        os.open(mOutputDir + "/" + name, std::ios::out | std::ios::trunc);
        if (!os) {
            throw cRuntimeError("cannot open %s/%s", mOutputDir.c_str(), name);
        }
    };
    open(mCamSent, "cam_sent.jsonl");
    open(mCpmSent, "cpm_sent.jsonl");
    open(mReception, "reception.jsonl");
    open(mPose, "vehicle_pose.jsonl");
    open(mVehicles, "vehicles.jsonl");

    // configuration snapshot
    const fs::path configDir = fs::path(mOutputDir) / "config";
    fs::create_directories(configDir);
    std::istringstream files(par("snapshotFiles").stdstringValue());
    std::string file;
    while (files >> file) {
        if (fs::exists(file)) {
            fs::copy_file(file, configDir / fs::path(file).filename(), fs::copy_options::overwrite_existing);
        } else {
            EV_WARN << "snapshot file not found: " << file << "\n";
        }
    }
}

void DatasetSink::finish()
{
    for (std::ofstream* os : {&mCamSent, &mCpmSent, &mReception, &mPose, &mVehicles}) {
        os->flush();
    }
    writeMetadata();
}

bool DatasetSink::isRecording() const
{
    return simTime() >= mStartTime;
}

double DatasetSink::time(simtime_t t) const
{
    return (t - mStartTime).dbl();
}

void DatasetSink::write(File file, const std::string& line)
{
    stream(file) << line << '\n';
    ++mRows[file];
}

uint64_t DatasetSink::registerMessage(File file, uint32_t station, uint64_t key)
{
    const uint64_t id = mNextMessageId++;
    Recent& entries = recent(file)[station];
    entries.emplace_back(key, id);
    if (entries.size() > kRecentPerStation) {
        entries.pop_front();
    }
    return id;
}

bool DatasetSink::lookupMessage(File file, uint32_t station, uint64_t key, uint64_t& id) const
{
    const auto& table = recent(file);
    auto found = table.find(station);
    if (found == table.end()) {
        return false;
    }
    // newest first: CAM keys wrap every 65.536 s
    for (auto it = found->second.rbegin(); it != found->second.rend(); ++it) {
        if (it->first == key) {
            id = it->second;
            return true;
        }
    }
    return false;
}

std::unordered_map<uint32_t, DatasetSink::Recent>& DatasetSink::recent(File file)
{
    return file == File::CpmSent ? mRecentCpms : mRecentCams;
}

const std::unordered_map<uint32_t, DatasetSink::Recent>& DatasetSink::recent(File file) const
{
    return file == File::CpmSent ? mRecentCpms : mRecentCams;
}

std::ofstream& DatasetSink::stream(File file)
{
    switch (file) {
        case File::CamSent: return mCamSent;
        case File::CpmSent: return mCpmSent;
        case File::Reception: return mReception;
        case File::Pose: return mPose;
        case File::Vehicles: return mVehicles;
    }
    throw cRuntimeError("unknown dataset file");
}

void DatasetSink::writeMetadata() const
{
    cConfigurationEx* config = getEnvir()->getConfigEx();
    auto rows = [this](File file) {
        auto found = mRows.find(file);
        return found != mRows.end() ? found->second : uint64_t(0);
    };

    JsonObject counts;
    counts.field("cam_sent", rows(File::CamSent))
        .field("cpm_sent", rows(File::CpmSent))
        .field("reception", rows(File::Reception))
        .field("vehicle_pose", rows(File::Pose))
        .field("vehicles", rows(File::Vehicles));

    JsonObject meta;
    meta.field("schemaVersion", 1)
        .field("configName", config->getVariable(CFGVAR_CONFIGNAME))
        .field("runNumber", config->getVariable(CFGVAR_RUNNUMBER))
        .field("seedSet", config->getVariable(CFGVAR_SEEDSET))
        .field("warmup", mStartTime.dbl())
        .field("duration", time(simTime()))
        .field("cpmConvention", "ETSI TS 103 324 V2.1.1, converted from Artery r2 encoding")
        .field("rows", counts);

    std::ofstream os(mOutputDir + "/metadata.json", std::ios::out | std::ios::trunc);
    os << meta.str() << '\n';
}

}  // namespace dataset
