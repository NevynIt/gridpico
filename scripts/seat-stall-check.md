# Local PM stall reminder

Run these commands from the repository root:

```sh
./scripts/seat-stall-check status
./scripts/seat-stall-check enable
./scripts/seat-stall-check enable --interval 1800
./scripts/seat-stall-check disable
```

The initial interval is 30 minutes. `--interval` takes seconds and updates the saved interval. An enable without that flag reuses the saved interval. A fresh enable or interval change waits a full interval. Repeating enable on an already running worker with the same interval leaves its timer alone.

The worker uses the existing `seat-send` helper to queue a fixed reminder for `project-manager` from `stall-check@dev`. Attempts follow a monotonic schedule at the configured interval. When a send finishes, the worker waits for the first future tick and skips any ticks that passed during the send. A queue receipt records acceptance. Failed, timed-out, and uncertain sends follow the same schedule. The only operator controls are enable, disable and status.

Status shows whether the worker holds its singleton lock, the configured interval, the last attempt and its outcome, and the last accepted queue receipt. Disable waits for a current bounded send and worker exit and preserves this history. Disable explicitly when the backlog is clear.

The local worker needs Python 3, GNU `timeout`, and the usual `seat-send` environment. After process loss or a machine restart, use `status` and explicitly `enable` to restart it. The next attempt waits a full interval even if the previous worker died during a send. State and the worker error log stay in the ignored `.team/local/stall-check/` directory. The updated worker reads existing acknowledgment-era state as attempt history.

The safe host check uses a temporary fake `seat-send` and queues nothing:

```sh
python3 scripts/tests/test-seat-stall-check.py
```
