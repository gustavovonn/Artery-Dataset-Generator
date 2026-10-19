package dataset;

// Owns the dataset files of one run
simple DatasetSink
{
    parameters:
        @class(dataset::DatasetSink);
        string outputDir;
        double startTime @unit(s) = default(0s);
        // copied into <outputDir>/config
        string snapshotFiles = default("omnetpp.ini");
        @display("i=block/table");
}
