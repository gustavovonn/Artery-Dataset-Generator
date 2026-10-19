#include "StationRecorder.h"

#include "Angles.h"
#include "Constants.h"
#include "CoordinatePolicies.h"

#include "DatasetSink.h"
#include "Json.h"

#include "artery/application/CaObject.h"
#include "artery/application/VehicleDataProvider.h"
#include "artery/envmod/LocalEnvironmentModel.h"
#include "artery/envmod/TraCIEnvironmentModelObject.h"
#include "artery/envmod/sensor/Sensor.h"
#include "artery/envmod/service/CpObject.h"
#include "artery/traci/Cast.h"
#include "artery/traci/VehicleController.h"
#include "artery/utility/round.h"
#include "traci/API.h"

#include <boost/units/systems/si/prefixes.hpp>
#include <omnetpp.h>

#include <cmath>

namespace dataset
{

using namespace omnetpp;
using File = DatasetSink::File;

Define_Module(StationRecorder)

static const simsignal_t scSignalCamSent = cComponent::registerSignal("CamSent");
static const simsignal_t scSignalCamReceived = cComponent::registerSignal("CamReceived");
static const simsignal_t scSignalCpmSent = cComponent::registerSignal("CpmSent");
static const simsignal_t scSignalCpmReceived = cComponent::registerSignal("CpmReceived");

namespace
{

using Angles::wrap360;
constexpr double kRadToDeg = ConstantsWGS84::RAD_TO_DEG;

// CDD CartesianAngle of a compass heading, 0.1 deg
double cartesianDirection(double headingRad)
{
    return std::round(Angles::compassToCartesianDeg(headingRad * kRadToDeg) * 10.0) / 10.0;
}

uint64_t cpmReferenceTime(const artery::Cpm& cpm)
{
    uint64_t referenceTime = 0;
    asn_INTEGER2uint64(&cpm->payload.managementContainer.referenceTime, &referenceTime);
    return referenceTime;
}

JsonObject classification(const Vanetza_ITS2_ObjectClassDescription_t& description)
{
    JsonObject o;
    if (description.list.count == 0) {
        return o;
    }
    const Vanetza_ITS2_ObjectClass_t& objectClass = description.list.array[0]->objectClass;
    switch (objectClass.present) {
        case Vanetza_ITS2_ObjectClass_PR_vehicleSubClass:
            o.field("class", "vehicle").field("subclass", objectClass.choice.vehicleSubClass);
            break;
        case Vanetza_ITS2_ObjectClass_PR_vruSubClass:
            o.field("class", "vru").field("subclass", static_cast<int>(objectClass.choice.vruSubClass.present));
            break;
        case Vanetza_ITS2_ObjectClass_PR_groupSubClass:
            o.field("class", "group").null("subclass");
            break;
        case Vanetza_ITS2_ObjectClass_PR_otherSubClass:
            o.field("class", "other").field("subclass", objectClass.choice.otherSubClass);
            break;
        default:
            o.null("class").null("subclass");
    }
    return o;
}

}  // namespace

void StationRecorder::initialize()
{
    ItsG5BaseService::initialize();

    cModule* sink = getSimulation()->getSystemModule()->getSubmodule(par("sinkModule").stringValue());
    mSink = check_and_cast<DatasetSink*>(sink);
    mVdp = &getFacilities().get_const<artery::VehicleDataProvider>();
    mController = getFacilities().get_mutable_ptr<traci::VehicleController>();
    mLem = getFacilities().get_mutable_ptr<artery::LocalEnvironmentModel>();
    mCpServiceName = par("cpServiceName").stdstringValue();
    mPoseInterval = par("poseInterval");
    mLastPose = SIMTIME_ZERO;

    subscribe(scSignalCamSent);
    subscribe(scSignalCamReceived);
    subscribe(scSignalCpmSent);
    subscribe(scSignalCpmReceived);
}

void StationRecorder::trigger()
{
    Enter_Method("trigger");
    if (!mSink->isRecording()) {
        return;
    }
    if (!mVehicleRecorded) {
        recordVehicle();
        recordPose();
        mVehicleRecorded = true;
        mLastPose = simTime();
    } else if (simTime() - mLastPose >= mPoseInterval) {
        recordPose();
        mLastPose = simTime();
    }
}

void StationRecorder::receiveSignal(cComponent*, simsignal_t signal, cObject* obj, cObject*)
{
    if (!mSink->isRecording()) {
        return;
    }

    if (signal == scSignalCamSent) {
        if (auto* cam = dynamic_cast<artery::CaObject*>(obj)) {
            recordCamSent(*cam);
        }
    } else if (signal == scSignalCamReceived) {
        if (auto* cam = dynamic_cast<artery::CaObject*>(obj)) {
            const auto& msg = cam->asn1();
            recordReception("cam", msg->header.stationID, msg->cam.generationDeltaTime);
        }
    } else if (signal == scSignalCpmSent) {
        if (auto* cpm = dynamic_cast<artery::CpObject*>(obj)) {
            recordCpmSent(*cpm);
        }
    } else if (signal == scSignalCpmReceived) {
        if (auto* cpm = dynamic_cast<artery::CpObject*>(obj)) {
            const auto& msg = cpm->asn1();
            recordReception("cpm", msg->header.stationId, cpmReferenceTime(msg));
        }
    }
}

void StationRecorder::recordVehicle()
{
    JsonObject o;
    o.field("station", mVdp->getStationId());
    o.field("stationType", static_cast<int>(mVdp->getStationType()));
    if (mController) {
        o.field("sumoId", mController->getVehicleId())
            .field("vehicleType", mController->getTypeId())
            .field("length", mController->getLength().value())
            .field("width", mController->getWidth().value());
    } else {
        o.null("sumoId").null("vehicleType").null("length").null("width");
    }
    o.field("hasCpm", getParentModule()->getSubmodule(mCpServiceName.c_str()) != nullptr);
    o.field("firstSeen", mSink->time(simTime()));
    mSink->write(File::Vehicles, o.str());
}

void StationRecorder::recordPose()
{
    JsonObject o;
    o.field("station", mVdp->getStationId()).field("time", mSink->time(simTime()));
    if (mController) {
        const auto sumo = traci::position_cast(mController->getBoundary(), mVdp->position());
        o.field("x", sumo.x).field("y", sumo.y);
    } else {
        o.null("x").null("y");
    }
    o.field("lat", mVdp->latitude().value())
        .field("lon", mVdp->longitude().value())
        .field("heading", wrap360((mVdp->heading().value() + gridBearing()) * kRadToDeg))
        .field("speed", mVdp->speed().value())
        .field("accel", mVdp->acceleration().value())
        .field("yawRate", mVdp->yaw_rate().value() * kRadToDeg);
    mSink->write(File::Pose, o.str());
}

void StationRecorder::recordCamSent(const artery::CaObject& obj)
{
    const auto& cam = obj.asn1();
    const uint32_t sender = cam->header.stationID;
    const long gdt = cam->cam.generationDeltaTime;
    const uint64_t id = mSink->registerMessage(File::CamSent, sender, gdt);

    const BasicContainer_t& basic = cam->cam.camParameters.basicContainer;
    const ReferencePosition_t& ref = basic.referencePosition;

    JsonObject o;
    o.field("messageID", id)
        .field("sendTime", mSink->time(simTime()))
        // generationDeltaTime is the time of the reference position (EN 302 637-2), Artery: VDP update
        .field("refSimTime", mSink->time(mVdp->updated()))
        .field("sender", sender)
        .field("generationDeltaTime", gdt)
        .field("stationType", basic.stationType)
        .optional("lat", ref.latitude != Latitude_unavailable, ref.latitude / 1e7)
        .optional("lon", ref.longitude != Longitude_unavailable, ref.longitude / 1e7);

    const HighFrequencyContainer_t& hfc = cam->cam.camParameters.highFrequencyContainer;
    if (hfc.present == HighFrequencyContainer_PR_basicVehicleContainerHighFrequency) {
        const BasicVehicleContainerHighFrequency_t& bvc = hfc.choice.basicVehicleContainerHighFrequency;
        const long heading = bvc.heading.headingValue;
        const long speed = bvc.speed.speedValue;
        // Artery: accel in 1 m/s2 steps, yaw rate in 1 deg/s steps; re-derived per CDD
        const double accel = mVdp->acceleration().value();
        const double yawRate = mVdp->yaw_rate().value() * kRadToDeg;
        // Artery sends length and width unavailable; filled from the vehicle, CDD 0.1 m
        const double length = mController ? std::round(mController->getLength().value() * 10.0) / 10.0 : -1.0;
        const double width = mController ? std::round(mController->getWidth().value() * 10.0) / 10.0 : -1.0;
        o.optional("heading", heading != HeadingValue_unavailable, wrap360(heading / 10.0 + gridBearing() * kRadToDeg))
            .optional("speed", speed != SpeedValue_unavailable, speed / 100.0)
            .optional("accel", accel >= -16.0 && accel <= 16.1, std::round(accel * 10.0) / 10.0)
            .optional("yawRate", std::abs(yawRate) <= 327.66, std::round(yawRate * 100.0) / 100.0)
            .optional("vehicleLength", length > 0.0, length)
            .optional("vehicleWidth", width > 0.0, width);
    } else {
        o.null("heading").null("speed").null("accel").null("yawRate").null("vehicleLength").null("vehicleWidth");
    }

    mSink->write(File::CamSent, o.str());
}

void StationRecorder::recordCpmSent(const artery::CpObject& obj)
{
    const auto& cpm = obj.asn1();
    const uint32_t sender = cpm->header.stationId;
    const uint64_t referenceTime = cpmReferenceTime(cpm);
    const uint64_t id = mSink->registerMessage(File::CpmSent, sender, referenceTime);
    const double refSimTime = mSink->time(mVdp->updated());
    const auto& ref = cpm->payload.managementContainer.referencePosition;

    JsonObject o;
    o.field("messageID", id)
        .field("sendTime", mSink->time(simTime()))
        .field("sender", sender)
        .field("referenceTime", referenceTime)
        .field("refSimTime", refSimTime)
        .optional("lat", ref.latitude != Vanetza_ITS2_Latitude_unavailable, ref.latitude / 1e7)
        .optional("lon", ref.longitude != Vanetza_ITS2_Longitude_unavailable, ref.longitude / 1e7);

    bool hasHeading = false;
    double heading = 0.0;
    JsonArray sensors;
    bool hasSensorInfo = false;
    JsonArray objects;

    const OracleMap oracle = buildOracle();
    const double cosB = std::cos(gridBearing());
    const double sinB = std::sin(gridBearing());
    const auto& containers = cpm->payload.cpmContainers.list;
    for (int i = 0; i < containers.count; ++i) {
        const Vanetza_ITS2_WrappedCpmContainer_t* wcc = containers.array[i];
        switch (wcc->containerData.present) {
            case Vanetza_ITS2_WrappedCpmContainer__containerData_PR_OriginatingVehicleContainer: {
                const auto& angle = wcc->containerData.choice.OriginatingVehicleContainer.orientationAngle;
                hasHeading = angle.value != Vanetza_ITS2_Wgs84AngleValue_unavailable;
                heading = wrap360(angle.value / 10.0 + gridBearing() * kRadToDeg);
                break;
            }
            case Vanetza_ITS2_WrappedCpmContainer__containerData_PR_SensorInformationContainer: {
                hasSensorInfo = true;
                const auto& sic = wcc->containerData.choice.SensorInformationContainer;
                for (int s = 0; s < sic.list.count; ++s) {
                    JsonObject sensor;
                    sensor.field("id", sic.list.array[s]->sensorId).field("type", sic.list.array[s]->sensorType);
                    sensors.add(sensor);
                }
                break;
            }
            case Vanetza_ITS2_WrappedCpmContainer__containerData_PR_PerceivedObjectContainer: {
                const auto& pos = wcc->containerData.choice.PerceivedObjectContainer.perceivedObjects.list;
                for (int p = 0; p < pos.count; ++p) {
                    const Vanetza_ITS2_PerceivedObject_t* po = pos.array[p];
                    const long xCm = po->position.xCoordinate.value;
                    const long yCm = po->position.yCoordinate.value;
                    // Artery encodes the delta with the opposite sign of the CDD
                    const long mdt = -po->measurementDeltaTime;

                    const double xGrid = xCm / 100.0;
                    const double yGrid = -yCm / 100.0;

                    JsonObject ob;
                    ob.optional("objectId", po->objectId != nullptr, po->objectId ? *po->objectId : 0L)
                        .field("measurementDeltaTime", mdt)
                        .field("measTime", refSimTime + mdt / 1000.0)
                        // Artery grid y grows southwards; grid east/north rotated to true East-North-Up
                        .field("x", xGrid * cosB + yGrid * sinB)
                        .field("y", -xGrid * sinB + yGrid * cosB);

                    const auto match = oracle.find({xCm, yCm});
                    const bool matched = match != oracle.end();

                    bool hasVelocity = false;
                    if (po->velocity && po->velocity->present == Vanetza_ITS2_Velocity3dWithConfidence_PR_polarVelocity) {
                        const long speed = po->velocity->choice.polarVelocity.velocityMagnitude.speedValue;
                        hasVelocity = speed != Vanetza_ITS2_SpeedValue_unavailable;
                        ob.optional("speed", hasVelocity, speed / 100.0);
                    } else {
                        ob.null("speed");
                    }
                    // Artery fills velocityDirection with radians*10; re-derived per TS from the object heading
                    ob.optional("direction", hasVelocity && matched && match->second.hasHeading,
                        matched ? cartesianDirection(match->second.headingRad + gridBearing()) : 0.0);
                    ob.field("hasVelocity", hasVelocity);

                    ob.optional("objectAge", po->objectAge != nullptr, po->objectAge ? *po->objectAge : 0L)
                        .optional("perceptionQuality", po->objectPerceptionQuality != nullptr,
                            po->objectPerceptionQuality ? *po->objectPerceptionQuality : 0L);
                    if (po->classification) {
                        ob.field("classification", classification(*po->classification));
                    } else {
                        ob.null("classification");
                    }

                    JsonArray sensorIds;
                    if (po->sensorIdList) {
                        for (int s = 0; s < po->sensorIdList->list.count; ++s) {
                            sensorIds.add(*po->sensorIdList->list.array[s]);
                        }
                    }
                    ob.raw("sensorIdList", sensorIds.str());

                    // oracle, not on the wire
                    ob.optional("objectStation", matched && match->second.station >= 0, matched ? match->second.station : -1L);
                    JsonArray detectedBy;
                    if (matched) {
                        for (const std::string& name : match->second.sensors) {
                            detectedBy.add(name);
                        }
                    }
                    ob.raw("detectedBy", detectedBy.str());
                    objects.add(ob);
                }
                break;
            }
            default:
                break;
        }
    }

    o.optional("heading", hasHeading, heading);
    if (hasSensorInfo) {
        o.raw("sensors", sensors.str());
    } else {
        o.null("sensors");
    }
    o.raw("objects", objects.str());
    mSink->write(File::CpmSent, o.str());
}

void StationRecorder::recordReception(const char* msg, uint32_t sender, uint64_t key)
{
    const File file = std::string(msg) == "cam" ? File::CamSent : File::CpmSent;
    uint64_t id = 0;
    if (!mSink->lookupMessage(file, sender, key, id)) {
        return;
    }
    JsonObject o;
    o.field("messageID", id).field("msg", msg).field("receiver", mVdp->getStationId()).field("rcvTime", mSink->time(simTime()));
    mSink->write(File::Reception, o.str());
}

// True bearing of grid north at this vehicle (meridian convergence), through SUMO's projection
double StationRecorder::gridBearing()
{
    if (!mGridBearingKnown && mController) {
        const auto api = mController->getTraCI();
        traci::TraCIPosition p = traci::position_cast(mController->getBoundary(), mController->getPosition());
        traci::TraCIPosition q = p;
        q.y += 100.0;
        const traci::TraCIGeoPosition a = api->convertGeo(p);
        const traci::TraCIGeoPosition b = api->convertGeo(q);
        mGridBearing = Wgs84CoordinatePolicy::bearing(a.latitude, a.longitude, b.latitude, b.longitude);
        mGridBearingKnown = true;
    }
    return mGridBearing;
}

// Keyed like CpService rounds positions, so every perceived object resolves to its tracked object
StationRecorder::OracleMap StationRecorder::buildOracle() const
{
    OracleMap oracle;
    if (!mLem) {
        return oracle;
    }

    using boost::units::si::centi;
    using boost::units::si::meter;
    const artery::Position& ego = mVdp->position();
    for (const auto& tracked : mLem->allObjects()) {
        auto object = tracked.first.lock();
        if (!object) {
            continue;
        }
        const artery::Position& centre = object->getCentrePoint();
        const long xCm = artery::round(centre.x - ego.x, meter * centi);
        const long yCm = artery::round(centre.y - ego.y, meter * centi);

        Oracle entry;
        if (auto traciObject = std::dynamic_pointer_cast<artery::TraCIEnvironmentModelObject>(object)) {
            const artery::VehicleDataProvider& vdp = traciObject->getVehicleData();
            entry.station = vdp.getStationId();
            entry.hasHeading = true;
            entry.headingRad = vdp.heading().value();
        }
        for (const auto& sensor : tracked.second.sensors()) {
            if (sensor.first) {
                entry.sensors.push_back(sensor.first->getSensorName());
            }
        }
        oracle.emplace(std::make_pair(xCm, yCm), std::move(entry));
    }
    return oracle;
}

}  // namespace dataset
