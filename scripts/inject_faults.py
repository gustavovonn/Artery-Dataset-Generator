#!/usr/bin/env python3
"""Builds a faulty subset from a ground-truth run, following VeReMi Extension.

Usage: python3 scripts/inject_faults.py config/faults.toml
"""
import heapq
import json
import math
import os
import random
import shutil
import sys
import tomllib

EARTH_RADIUS = 6371000.0

# F2MD attackTypes enum values
ATTACK_IDS = {
    "Genuine": 0, "ConstPos": 1, "ConstPosOffset": 2, "RandomPos": 3, "RandomPosOffset": 4,
    "ConstSpeed": 5, "ConstSpeedOffset": 6, "RandomSpeed": 7, "RandomSpeedOffset": 8,
}
POSITION_FAULTS = ("ConstPos", "ConstPosOffset", "RandomPos", "RandomPosOffset")


def read_jsonl(path):
    with open(path) as f:
        for line in f:
            if line.strip():
                yield json.loads(line)


def write_jsonl(f, record):
    f.write(json.dumps(record, separators=(",", ":")) + "\n")


def shift(lat, lon, east, north):
    dlat = math.degrees(north / EARTH_RADIUS)
    dlon = math.degrees(east / (EARTH_RADIUS * math.cos(math.radians(lat))))
    return lat + dlat, lon + dlon


def velocity(speed, heading):
    h = math.radians(heading)
    return speed * math.sin(h), speed * math.cos(h)  # east, north


class SensorError:
    """VeReMi Extension Sec. IV-B, one instance per vehicle."""

    def __init__(self, rng, cfg):
        self.cfg = cfg
        self.rng = rng
        self.e0_pos = (rng.uniform(-5.0, 5.0), rng.uniform(-5.0, 5.0))
        self.e_pos = self.e0_pos
        self.e0_speed = rng.gauss(0.0, 0.00016)
        self.e0_heading = rng.uniform(-20.0, 20.0)
        self.last_speed_error = None
        self.last_time = None
        self.fix_time = None  # instant of e_pos
        self.speed = 0.0  # latest true speed, for CPMs

    def position(self, time):
        """One GNSS fix per instant: a CAM and a CPM of the same instant share its error."""
        if not self.cfg.get("position", True):
            return 0.0, 0.0
        if time != self.fix_time:
            self.e_pos = tuple(
                self.rng.gauss((e0 + e) / 2.0, abs(0.03 * e0)) for e0, e in zip(self.e0_pos, self.e_pos))
            self.fix_time = time
        return self.e_pos

    def heading(self):
        if not self.cfg.get("heading", True):
            return 0.0
        return self.e0_heading * math.exp(-0.1 * self.speed)

    def apply(self, msg, kind):
        if kind == "cam" and msg.get("speed") is not None:
            self.speed = msg["speed"]
        east, north = self.position(msg["refSimTime"])
        if msg.get("lat") is not None and msg.get("lon") is not None:
            msg["lat"], msg["lon"] = shift(msg["lat"], msg["lon"], east, north)
        if msg.get("heading") is not None:
            msg["heading"] = (msg["heading"] + self.heading()) % 360.0
        if kind != "cam" or msg.get("speed") is None:
            return
        speed_error = msg["speed"] * self.e0_speed if self.cfg.get("speed", True) else 0.0
        msg["speed"] += speed_error
        if self.cfg.get("acceleration", True) and msg.get("accel") is not None and self.last_speed_error is not None:
            dt = msg["sendTime"] - self.last_time
            if dt > 0:
                msg["accel"] += (speed_error - self.last_speed_error) / dt
        self.last_speed_error = speed_error
        self.last_time = msg["sendTime"]


class Fault:
    """VeReMi Extension Sec. IV-C malfunctions as implemented in F2MD MDAttack.cc.

    A CPM carries the author's reference position but not its speed, so only position malfunctions
    reach it, moving every object reported relative to that position."""

    def __init__(self, kind, rng, params, bounds):
        self.kind = kind
        self.rng = rng
        self.p = params
        self.bounds = bounds  # lat_min, lat_max, lon_min, lon_max
        pv = params["par_var"]
        lo, hi = 1.0 - pv, 1.0 + pv
        self.const_pos = (rng.uniform(bounds[0], bounds[1]), rng.uniform(bounds[2], bounds[3]))
        self.const_pos_offset = tuple(rng.uniform(lo * params["pos_offset"], hi * params["pos_offset"]) for _ in range(2))
        self.const_speed = tuple(rng.uniform(0.0, params["speed"]) for _ in range(2))
        self.const_speed_offset = tuple(rng.uniform(lo * params["speed_offset"], hi * params["speed_offset"]) for _ in range(2))
        self.fix = None  # (instant, lat, lon) of the latest faulty position

    def apply(self, msg, kind):
        """Returns True when the message content was altered."""
        if kind == "cpm":
            if self.kind not in POSITION_FAULTS or msg.get("lat") is None or msg.get("lon") is None:
                return False
        elif any(msg.get(f) is None for f in ("lat", "lon", "heading", "speed")):
            return False
        if self.kind in POSITION_FAULTS:
            msg["lat"], msg["lon"] = self.faulty_position(msg)
        else:
            self.apply_speed(msg)
        return True

    def faulty_position(self, msg):
        """One faulty fix per instant: a CAM and a CPM of the same instant share it."""
        time = msg["refSimTime"]
        if self.fix is not None and self.fix[0] == time:
            return self.fix[1], self.fix[2]
        k = self.kind
        p = self.p
        rng = self.rng
        lat, lon = msg["lat"], msg["lon"]
        # F2MD coordinates: x east, y south
        if k == "ConstPos":
            lat, lon = self.const_pos
        elif k == "ConstPosOffset":
            dx, dy = self.const_pos_offset
            lat, lon = shift(lat, lon, dx, -dy)
        elif k == "RandomPos":
            b = self.bounds
            lat, lon = rng.uniform(b[0], b[1]), rng.uniform(b[2], b[3])
        elif k == "RandomPosOffset":
            dx = rng.uniform(0.0, p["pos_offset"]) - p["pos_offset"] / 2.0
            dy = rng.uniform(0.0, p["pos_offset"]) - p["pos_offset"] / 2.0
            lat, lon = shift(lat, lon, dx, -dy)
        self.fix = (time, lat, lon)
        return lat, lon

    def apply_speed(self, msg):
        k = self.kind
        p = self.p
        rng = self.rng
        if k == "ConstSpeed":
            msg["speed"] = math.hypot(*self.const_speed)
        elif k == "ConstSpeedOffset":
            ve, vn = velocity(msg["speed"], msg["heading"])
            dx, dy = self.const_speed_offset
            msg["speed"] = math.hypot(ve + dx, vn - dy)
        elif k == "RandomSpeed":
            msg["speed"] = math.hypot(rng.uniform(0.0, p["speed"]), rng.uniform(0.0, p["speed"]))
        elif k == "RandomSpeedOffset":
            lo, hi = (1.0 - p["par_var"]) * p["speed_offset"], (1.0 + p["par_var"]) * p["speed_offset"]
            dx = rng.uniform(lo, hi) * rng.choice((1.0, -1.0))
            dy = rng.uniform(lo, hi) * rng.choice((1.0, -1.0))
            ve, vn = velocity(msg["speed"], msg["heading"])
            msg["speed"] = math.hypot(ve + dx, vn - dy)
        else:
            raise ValueError(f"unsupported fault type {k}")


def bounds_of(cam_path):
    lat_min = lon_min = math.inf
    lat_max = lon_max = -math.inf
    for m in read_jsonl(cam_path):
        if m.get("lat") is None:
            continue
        lat_min, lat_max = min(lat_min, m["lat"]), max(lat_max, m["lat"])
        lon_min, lon_max = min(lon_min, m["lon"]), max(lon_max, m["lon"])
    return lat_min, lat_max, lon_min, lon_max


def main(config_path):
    with open(config_path, "rb") as f:
        cfg = tomllib.load(f)
    src, dst = cfg["input"], cfg["output"]
    os.makedirs(dst, exist_ok=True)
    rng = random.Random(cfg.get("seed", 0))
    sensor_cfg = cfg.get("sensor_errors")
    fault_cfg = cfg.get("faults", {})
    params = fault_cfg.get("params", {})
    types = fault_cfg.get("types", [])
    for t in types:
        if t not in ATTACK_IDS or t == "Genuine":
            sys.exit(f"unsupported fault type: {t}")

    cam_path = os.path.join(src, "cam_sent.jsonl")
    cpm_path = os.path.join(src, "cpm_sent.jsonl")
    bounds = bounds_of(cam_path)

    # fault assignment in order of first appearance
    vehicles = list(read_jsonl(os.path.join(src, "vehicles.jsonl")))
    assignment = {}
    next_type = 0
    for v in vehicles:
        kind = "Genuine"
        if types and rng.random() < fault_cfg.get("attacker_rate", 0.0):
            kind = types[next_type % len(types)]
            next_type += 1
        assignment[v["station"]] = kind
    with open(os.path.join(dst, "vehicles.jsonl"), "w") as f:
        for v in vehicles:
            v["faultType"] = assignment[v["station"]]
            write_jsonl(f, v)

    sensors = {}
    faults = {s: Fault(k, random.Random(rng.random()), params, bounds)
              for s, k in assignment.items() if k != "Genuine"}
    start = fault_cfg.get("start_time", 0.0)

    def tagged(path, kind):
        for m in read_jsonl(path):
            yield m["sendTime"], kind, m

    with open(os.path.join(dst, "cam_sent.jsonl"), "w") as cam_out, \
            open(os.path.join(dst, "cpm_sent.jsonl"), "w") as cpm_out:
        stream = heapq.merge(tagged(cam_path, "cam"), tagged(cpm_path, "cpm"), key=lambda e: (e[0], e[1]))
        for _, kind, msg in stream:
            sender = msg["sender"]
            if sensor_cfg is not None:
                if sender not in sensors:
                    sensors[sender] = SensorError(random.Random(rng.random()), sensor_cfg)
                sensors[sender].apply(msg, kind)
            attack = 0
            fault = faults.get(sender)
            if fault and msg["sendTime"] >= start and fault.apply(msg, kind):
                attack = ATTACK_IDS[fault.kind]
            msg["attackType"] = attack
            write_jsonl(cam_out, msg)

    for name in ("reception.jsonl", "vehicle_pose.jsonl"):
        target = os.path.join(dst, name)
        if os.path.exists(target):
            os.remove(target)
        try:
            os.link(os.path.join(src, name), target)
        except OSError:
            shutil.copyfile(os.path.join(src, name), target)

    with open(os.path.join(src, "metadata.json")) as f:
        meta = json.load(f)
    meta["injection"] = {"source": src, "config": cfg, "attackIds": ATTACK_IDS}
    with open(os.path.join(dst, "metadata.json"), "w") as f:
        json.dump(meta, f, indent=2)
    print(f"wrote {dst}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
