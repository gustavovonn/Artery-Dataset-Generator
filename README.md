# Artery Dataset Generator

Honest CAM/CPM ground-truth traces from an Artery simulation of a LuST square, and offline construction of faulty subsets following VeReMi Extension.

```
omnetpp.ini              all simulation parameters
config/                  NED (DatasetWorld, DatasetSink, StationRecorder), sensors.xml, lust-region.xml, faults.toml
include/, src/           StationRecorder (per vehicle), DatasetSink
artery/scenarios/LuSTScenario   LuST (submodule added on artery)
scripts/                 docker, build, run, inject_faults.py, to_parquet.py
trace_schema.md          format
data/groundtruth/        simulation output
data/subsets/            injected subsets
```

## Setup

```bash
git submodule update --init --recursive          # artery and its extern/, LuST
scripts/setup_docker.sh                          # container "artery-dataset", repo at /workspace
scripts/build.sh
```

`[Config lust]` is the preliminary-results run: SUMO starts at 06:30 (`--begin 23400`) and 120 s are recorded from the start, without warm-up.

## Run

```bash
scripts/run.sh                 # [Config lust] -> data/groundtruth/lust-run0/
scripts/run.sh -r 1            # other seed
```

## Subsets

```bash
python3 scripts/inject_faults.py config/faults.toml
python3 scripts/to_parquet.py data/groundtruth/lust-run0     # needs pyarrow
```

Implemented, offline: VeReMi Extension sensor error models (position, speed, acceleration, heading) and the CAM malfunctions ConstPos, ConstPosOffset, RandomPos, RandomPosOffset, ConstSpeed, ConstSpeedOffset, RandomSpeed, RandomSpeedOffset, with F2MD magnitudes. Attacks that add messages (DoS, Sybil, replay, disruptive) need new receptions and are not offline-injectable. Radar noise is not implemented.

## Parameter choices

| Parameter | Value | Reference |
|---|---|---|
| Window | 120 s from 06:30 (preliminary results) | — |
| Region | provisional square, `config/lust-region.xml` | to be selected |
| TX power / sensitivity | 20 mW / −89 dBm | F2MD LuST configuration (VeReMi Extension) |
| Propagation | GEMV² | Boban et al., IEEE TVT 2014; Artery `scenarios/gemv2` |
| DCC | on (CAM and CPM) | Artery defaults |
| CAM generation | EN 302 637-2 triggers | Artery `CaService` |
| CPM generation | ETSI TS 103 324 V2.1.1 |
| Radars | 65 m ±40°, 150 m ±5°, front and rear | ETSI TR 103 562 V2.1.1, Table 4 |
| CPM penetration | 1.0 | `<penetration rate>` in `omnetpp.ini` |
