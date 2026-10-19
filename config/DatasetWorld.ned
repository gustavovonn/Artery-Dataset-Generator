package dataset;

import artery.envmod.World;

network DatasetWorld extends World
{
    submodules:
        sink: DatasetSink {
            @display("p=220,20");
        }
}
