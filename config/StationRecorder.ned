package dataset;

import artery.application.ItsG5Service;

// Per-station recorder; writes through the network-level DatasetSink
simple StationRecorder like ItsG5Service
{
    parameters:
        @class(dataset::StationRecorder);
        string sinkModule = default("sink");
        // service name of CpService in the services XML
        string cpServiceName = default("CP");
        double poseInterval @unit(s) = default(0.1s);
}
