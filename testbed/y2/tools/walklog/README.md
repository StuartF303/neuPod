# walklog: field-test logger for the Y2

A busybox `sh` script for unattended tests away from the bench (a walk with headphones, a battery run). It runs
detached on the Y2, so unplugging USB does not stop it, and writes to the persistent ext4 rootfs:

| File | Content |
| --- | --- |
| `/opt/neupod-logs/walk-<date>-batt.csv` | every 30 s (`WALKLOG_PERIOD`): time, uptime, battery %, status, mV, average current (mA; reads about 0 on battery on the Y2), temperature (fixed 25.0 on the Y2), USB online, load, free RAM, firmware pid |
| `/opt/neupod-logs/walk-<date>-fw.log` | the firmware log (`/tmp/neupod-fw.log`, which lives in RAM and is cut back at 256 KB), each line prefixed with the time; the per-second `neuPod alive` lines are dropped |

`sync` runs every 10 samples, so a flat battery loses at most about 5 minutes.

```sh
scp -O testbed/y2/tools/walklog/walklog.sh root@192.168.7.2:/opt/walklog.sh
ssh root@192.168.7.2 /opt/walklog.sh start     # before setting off
ssh root@192.168.7.2 /opt/walklog.sh status
ssh root@192.168.7.2 /opt/walklog.sh stop      # when back
scp -O 'root@192.168.7.2:/opt/neupod-logs/walk-*' .
```

The Y2 has no RTC, so the date is wrong unless it was set; times are only good relative to each other. Switch off
bench dumps (`NEUPOD_BT_PCM_DUMP` in `/etc/neupod-fw.env`) first: it writes about 10 MB a minute to RAM.
