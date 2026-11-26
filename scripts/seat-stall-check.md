# Local PM stall reminder

Run these commands from the repository root:

```sh
./scripts/seat-stall-check status
./scripts/seat-stall-check enable
./scripts/seat-stall-check enable --interval 1800
./scripts/seat-stall-check ack
./scripts/seat-stall-check disable
```

The initial interval is 30 minutes. `--interval` takes seconds and updates the saved interval. An enable without that flag reuses the saved interval. A fresh enable, interval change, or acknowledgment waits a full interval. Repeating enable on an already running worker with the same interval leaves its timer alone.

The worker uses the existing `seat-send` helper to queue a fixed reminder for `project-manager` from `stall-check@dev`. A queue receipt records acceptance, and the reminder stays outstanding until PM runs `ack` after reading it. A timed-out, failed, or interrupted send also reserves the outstanding slot. Check the original thread or queue before acknowledging an uncertain attempt. Acknowledgment clears the local reservation and preserves last-send history.

Status shows whether the worker holds its singleton lock, the configured interval, the last attempt, the last accepted queue receipt, and any outstanding reminder. Disable waits for a current bounded send and worker exit. Disable preserves the outstanding slot. Disable explicitly when the backlog is clear.

The local worker needs Python 3, GNU `timeout`, and the usual `seat-send` environment. It has no service startup entry. After process loss or a machine restart, use `status` and explicitly `enable` to restart the worker. Pending time is discarded and missed intervals are never replayed. State and the worker error log stay in the ignored `.team/local/stall-check/` directory. Keep the state files so deduplication survives restarts.

The safe host check uses a temporary fake `seat-send` and queues nothing:

```sh
python3 scripts/tests/test-seat-stall-check.py
```
