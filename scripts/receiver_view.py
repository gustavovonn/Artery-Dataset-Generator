#!/usr/bin/env python3
"""Materializes the reception join of a run (ground truth or subset), one file per receiver, as VeReMi.

Usage: python3 scripts/receiver_view.py <run dir> [out dir, default <run dir>/receivers]
Writes <out>/<receiver>.jsonl: one line per reception, in rcvTime order, with the reception fields
(receiver, rcvTime, messageID, msg) followed by every field of the message as sent (cam_sent or
cpm_sent, objects nested, attackType in subsets). Nothing is filtered: CPMs without objects or
without a reference position are kept. Receptions of unrecorded messages are skipped and counted.
"""
import json
import os
import sys


def raw_messages(path):
    """messageID -> the message fields other than messageID, as JSON without braces, kept as written."""
    out = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            m = json.loads(line)
            message_id = m.pop("messageID")
            prefix = '{"messageID":' + json.dumps(message_id) + ","
            # the recorder writes messageID first; anything else is re-encoded
            out[message_id] = line[len(prefix):-1] if line.startswith(prefix) else json.dumps(m, separators=(",", ":"))[1:-1]
    return out


def main(run_dir, out_dir):
    messages = {"cam": raw_messages(os.path.join(run_dir, "cam_sent.jsonl")),
                "cpm": raw_messages(os.path.join(run_dir, "cpm_sent.jsonl"))}

    # reception.jsonl is in time order, so each receiver's list stays in rcvTime order
    by_receiver = {}
    with open(os.path.join(run_dir, "reception.jsonl")) as f:
        for line in f:
            if line.strip():
                r = json.loads(line)
                by_receiver.setdefault(r["receiver"], []).append((r["rcvTime"], r["messageID"], r["msg"]))

    os.makedirs(out_dir, exist_ok=True)
    written = skipped = 0
    for receiver, receptions in by_receiver.items():
        with open(os.path.join(out_dir, f"{receiver}.jsonl"), "w") as out:
            for rcv_time, message_id, kind in receptions:
                body = messages[kind].get(message_id)
                if body is None:
                    skipped += 1
                    continue
                head = json.dumps({"receiver": receiver, "rcvTime": rcv_time, "messageID": message_id, "msg": kind},
                                  separators=(",", ":"))
                out.write(head[:-1] + "," + body + "}\n")
                written += 1
    print(f"{out_dir}: {len(by_receiver)} receivers, {written} receptions, {skipped} of unrecorded messages skipped")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    run = sys.argv[1]
    main(run, sys.argv[2] if len(sys.argv) == 3 else os.path.join(run, "receivers"))
