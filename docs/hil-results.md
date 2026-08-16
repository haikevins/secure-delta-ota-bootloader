# Hardware-in-the-Loop Results

The deterministic fault matrix was executed on the target hardware and completed **9/9 PASS**.

| Scenario | Expected invariant | Result |
|---|---|---|
| `control-secure-delta` | signed delta installs and confirms v2 | PASS |
| `patch-reset` | reset during reconstruction recovers and confirms v2 | PASS |
| `backup-reset` | backup checkpoint resumes safely | PASS |
| `install-midpage-reset` | torn internal-flash install is replayed safely | PASS |
| `mqtt-drop-after-accepted` | orchestration reconnect does not duplicate the accepted update | PASS |
| `https-truncate` | incomplete artifact never reaches installation | PASS |
| `tampered-signature` | STM32 rejects the artifact and preserves v1 | PASS |
| `rollback-control` | unhealthy candidate rolls back exactly to v1 | PASS |
| `rollback-reset` | reset during rollback resumes and restores exact v1 | PASS |

Final hardware markers:

```text
STM32_METADATA label=rollback-reset generation=74 state=0 active_version=1 pending_version=0 boot_attempts=0 last_error=0x0008B003
STM32_VERIFY=PASS label=rollback-reset app=v1
SCENARIO=PASS id=rollback-reset generation=74 gateway=EXPECTED_FAIL
FAULT_WITNESS=PASS id=patch-reset
FAULT_WITNESS=PASS id=backup-reset
FAULT_WITNESS=PASS id=install-midpage-reset
MQTT_ISOLATION=PASS
ROLLBACK_FAULT_WITNESS=PASS
HIL hardware test: PASS (9 deterministic scenarios)
```

`last_error=0x0008B003` is retained as the rollback diagnostic while the update state is back to IDLE and v1 is active.

No HIL signing private key was persisted in the repository.
