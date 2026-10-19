# Trace Schema

A ground-truth run lives in `data/groundtruth/<config>-run<n>/`. Everything persisted is ground truth: message content as it goes on the wire with zero sensor error, plus simulator truth marked **oracle**. Sensor errors and faults are applied offline (`scripts/inject_faults.py`), producing `data/subsets/<name>/` with the same files.

All files are JSON Lines, one object per line, written in simulation-time order. Times are seconds since `sink.startTime`, the end of the warm-up (06:30 for the preliminary `lust` run, which has none). Unavailable values are `null`.

| File | One row per |
|---|---|
| `cam_sent.jsonl` | CAM generated |
| `cpm_sent.jsonl` | CPM generated, perceived objects nested |
| `reception.jsonl` | (message, receiver) |
| `vehicle_pose.jsonl` | (vehicle, `poseInterval`) |
| `vehicles.jsonl` | vehicle, at its first recorded step |
| `metadata.json` | run |
| `config/` | snapshot of `omnetpp.ini` and referenced files |

`messageID` is unique across CAMs and CPMs of a run and joins `reception` to `cam_sent` / `cpm_sent`.

---

## `cam_sent.jsonl`

Decoded from the CAM as sent (ETSI EN 302 637-2, Artery `CaService`).

| Field | Unit | Source |
|---|---|---|
| `messageID` | | recorder |
| `sendTime` | s | generation time |
| `sender` | | `header.stationID` |
| `refSimTime` | s | time of the reference position, i.e. of `generationDeltaTime` |
| `generationDeltaTime` | ms | `generationDeltaTime` (TimestampIts mod 65536) |
| `stationType` | | `basicContainer.stationType` |
| `lat`, `lon` | deg | `basicContainer.referencePosition` (front bumper) |
| `heading` | deg | HF `heading`, clockwise from true north; see *Artery deviations* |
| `speed` | m/s | HF `speed` |
| `accel` | m/s² | HF `longitudinalAcceleration`; see *Artery deviations* |
| `yawRate` | deg/s | HF `yawRate`, positive to the left; see *Artery deviations* |
| `vehicleLength`, `vehicleWidth` | m | HF; see *Artery deviations* |

Subsets add `attackType` (F2MD `attackTypes` id, `0` = genuine; only while the fault is active, as VeReMi Extension).

## `cpm_sent.jsonl`

Decoded from the CPM as sent (ETSI TS 103 324 V2.1.1, Artery `CpService`) and **expressed in the TS conventions**; see *Artery deviations* below.

| Field | Unit | Source |
|---|---|---|
| `messageID` | | recorder |
| `sendTime` | s | generation time |
| `sender` | | `header.stationId` |
| `referenceTime` | TAI ms | Management `referenceTime` |
| `refSimTime` | s | simulation time of `referenceTime` |
| `lat`, `lon` | deg | Management `referencePosition` (front bumper) |
| `heading` | deg | Originating Vehicle `orientationAngle`, clockwise from true north; see *Artery deviations* |
| `sensors` | | Sensor Information Container `[{id, type}]`; `null` when absent (sent every 1 s) |
| `objects` | | Perceived Object Container, below |

The author's speed is not in the CPM (TS 103 324 Originating Vehicle Container has none); take it from `cam_sent` or `vehicle_pose`.

### `objects[]`

| Field | Unit | Source |
|---|---|---|
| `objectId` | | `objectId`, author-local, reused after 60 s idle (TS 103 324 7.1.8.2, Annex F) |
| `measurementDeltaTime` | ms | measurement time − `referenceTime` (negative = before) |
| `measTime` | s | `refSimTime + measurementDeltaTime` |
| `x`, `y` | m | `position`: object bounding-box centre, East-North-Up relative to the author reference point (TS 103 324 7.1.8.1) |
| `speed` | m/s | `velocity.polarVelocity.velocityMagnitude` |
| `direction` | deg | velocity direction, counter-clockwise from x (east), 0.1 deg |
| `hasVelocity` | | `velocity` present |
| `objectAge` | ms | `objectAge` |
| `perceptionQuality` | | `objectPerceptionQuality` |
| `classification` | | `{class, subclass}` of the first `classification` entry |
| `sensorIdList` | | `sensorIdList`, ids of `sensors` |
| `objectStation` | | **oracle**: station id of the perceived object, `null` if not a station |
| `detectedBy` | | **oracle**: names of the radars tracking it (`config/sensors.xml`) |

Positions are relative so that radar error (per object) and GNSS error of the author (whole frame) stay separable offline.

### Artery deviations corrected by the recorder

| Field | Artery encoding | Recorded (CDD, TS 103 324) |
|---|---|---|
| CAM `accel` | m/s² truncated into a 0.1 m/s² field (10× too small) | from the sender's VDP, 0.1 m/s², `null` outside −16…16.1 |
| CAM `yawRate` | rounded to 1 deg/s | from the sender's VDP, 0.01 deg/s |
| CAM `vehicleLength`, `vehicleWidth` | unavailable | from the SUMO vehicle, 0.1 m |
| every heading, `position`, `velocityDirection` | relative to SUMO grid north (UTM, ~2° meridian convergence in LuST) | rotated to true north by the bearing of grid north at the vehicle, through SUMO's projection |
| `position.y` | grid y, growing south | `y = −y_artery` (north) |
| `measurementDeltaTime` | `referenceTime − measurement` | negated |
| `velocityDirection` | heading in radians × 10 | from the object's heading: `90° − heading` |

`vehicle_pose` `x`, `y` stay in SUMO grid coordinates (oracle); its `heading` is true north like the messages.

## `reception.jsonl`

| Field | Unit | |
|---|---|---|
| `messageID` | | joins `cam_sent` / `cpm_sent` |
| `msg` | | `cam` or `cpm` |
| `receiver` | | station id |
| `rcvTime` | s | |

Messages sent before `startTime` are not recorded, nor their receptions.

## `vehicle_pose.jsonl`

Simulator truth of every vehicle, every `poseInterval` (default 0.1 s).

| Field | Unit | |
|---|---|---|
| `station`, `time` | –, s | |
| `x`, `y` | m | SUMO network coordinates (y north) |
| `lat`, `lon` | deg | front bumper |
| `heading` | deg | clockwise from grid north |
| `speed`, `accel` | m/s, m/s² | |
| `yawRate` | deg/s | positive to the left |

## `vehicles.jsonl`

| Field | |
|---|---|
| `station`, `sumoId`, `vehicleType`, `stationType` | identity |
| `length`, `width` | m, SUMO vehicle type |
| `hasCpm` | runs CpService (penetration rate) |
| `firstSeen` | s |

Subsets add `faultType`.

## `metadata.json`

`schemaVersion`, `configName`, `runNumber`, `seedSet`, `warmup` (s), `duration` (s), `cpmConvention`, `rows` per file. Subsets add `injection` (source run, the full fault config, attack ids). Every simulation parameter is in `config/omnetpp.ini`.
