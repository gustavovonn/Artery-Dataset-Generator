# Artery Dataset Generator

Honest CAM/CPM ground-truth traces from an Artery simulation of a LuST square, and offline construction of faulty subsets following VeReMi Extension.

```
omnetpp.ini              all simulation parameters
config/                  NED (DatasetWorld, DatasetSink, StationRecorder), sensors.xml, lust-region.xml, faults.toml
include/, src/           StationRecorder (per vehicle), DatasetSink
artery/scenarios/LuSTScenario   LuST (submodule added on artery)
scripts/                 docker, build, run, inject_faults.py, receiver_view.py, to_parquet.py
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

`[Config lust-full]` is the preliminary-results run: the complete LuST, SUMO starts at 07:00 (`--begin 25200`) and 40 s are recorded from the start, without warm-up. `[Config lust]` restricts it to the region of interest, 120 s from 06:30.

## Run

```bash
scripts/run.sh                 # [Config lust] -> data/groundtruth/lust-run0/
scripts/run.sh -c lust-full    # complete LuST, 40 s from 07:00, otherwise as lust -> data/groundtruth/lust-full-run0/
scripts/run.sh -r 1            # other seed
```

## Subsets

```bash
python3 scripts/inject_faults.py config/faults.toml
python3 scripts/receiver_view.py data/subsets/lust-full-run0-veremi   # receivers/<id>.jsonl
python3 scripts/to_parquet.py data/groundtruth/lust-full-run0    # needs pyarrow
```

Implemented, offline: VeReMi Extension sensor error models (position, speed, acceleration, heading) and the malfunctions ConstPos, ConstPosOffset, RandomPos, RandomPosOffset, ConstSpeed, ConstSpeedOffset, RandomSpeed, RandomSpeedOffset, with F2MD magnitudes. Position malfunctions reach CAMs and the CPM reference position; speed malfunctions reach CAMs only (the CPM has no author speed). Attacks that add messages (DoS, Sybil, replay, disruptive) need new receptions and are not offline-injectable. Radar noise is not implemented.

## Parameter choices

| Parameter | Value | Reference |
|---|---|---|
| Window | 40 s from 07:00 (`lust-full`, preliminary results); 120 s from 06:30 (`lust`) | — |
| Region | complete LuST (`lust-full`); provisional square, `config/lust-region.xml` (`lust`) | to be selected |
| TX power / sensitivity | 20 mW / −89 dBm | F2MD LuST configuration (VeReMi Extension) |
| Propagation | GEMV² | Boban et al., IEEE TVT 2014; Artery `scenarios/gemv2` |
| DCC | on (CAM and CPM) | Artery defaults |
| CAM generation | EN 302 637-2 triggers | Artery `CaService` |
| CPM generation | ETSI TS 103 324 V2.1.1 |
| Radars | 65 m ±40°, 150 m ±5°, front and rear | ETSI TR 103 562 V2.1.1, Table 4 |
| CPM penetration | 1.0 | `<penetration rate>` in `omnetpp.ini` |
