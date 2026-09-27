# PDO Stage 1 test

This stage validates EtherCAT motor command/feedback PDO transport while the
TIM1 power outputs remain inhibited. EtherCAT commands are cached for debug,
but they are not applied to the FOC set-points.

## RxPDO (master to STM32, 16 bytes)

| Offset | Object | Type | Meaning |
|---:|---|---|---|
| 0 | 0x7000:01 | UINT16 | Control word (not active in Stage 1) |
| 2 | 0x7000:02 | UINT16 | Command counter |
| 4 | 0x7000:03 | INT32 | Target velocity, 0.001 rad/s |
| 8 | 0x7000:04 | UINT16 | Reserved 1 |
| 10 | 0x7000:05 | UINT16 | Reserved 2 |
| 12 | 0x7000:06 | UINT32 | Reserved 3 |

## TxPDO (STM32 to master, 16 bytes)

| Offset | Object | Type | Meaning |
|---:|---|---|---|
| 0 | 0x6000:01 | UINT16 | Status word |
| 2 | 0x6000:02 | UINT16 | FOC motor status |
| 4 | 0x6000:03 | INT32 | Actual velocity, 0.001 rad/s |
| 8 | 0x6000:04 | INT16 | Actual Iq, 0.01 A |
| 10 | 0x6000:05 | INT16 | Actual Id, 0.01 A |
| 12 | 0x6000:06 | UINT16 | FOC error flags |
| 14 | 0x6000:07 | UINT16 | Bus voltage, 0.01 V |

Status word bits:

- bit 0: EtherCAT output handler is active (OP)
- bit 1: FOC state is ready
- bit 2: Stage 1 motor inhibit is active (always 1 in this stage)
- bit 3: FOC fault is present
- bit 4: encoder data is valid

## Test procedure

1. Build and flash `MDK-ARM/ECAT/ECAT.hex`.
2. Copy `ESI/LAN9252_FOC_PDO.xml` to the TwinCAT EtherCAT ESI directory.
3. Restart TwinCAT/XAE or reload device descriptions, then rescan the slave.
4. Enter OP and verify status-word bits 0 and 2 are set.
5. Write `Command Counter` and `Target Velocity` from TwinCAT.
6. In Keil Watch, verify `gEcatMotorRxPdo` matches the TwinCAT values and
   `gEcatRxPdoCount` keeps increasing.
7. Confirm TIM1 MOE remains disabled and the motor cannot be energized.

Do not proceed to the enable/watchdog stage until signed positive and negative
velocity values and all feedback scaling have been verified.
