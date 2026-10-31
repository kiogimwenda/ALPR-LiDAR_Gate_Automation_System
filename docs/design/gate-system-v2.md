# Gate System v2: Design Spec

**Status:** draft for review (programme step 3). **Date:** 2026-10-31.
**Binding input:** `docs/handoff/CONTINUE.md`, "NEXT PROGRAMME". User decisions are kept as stated. Where this spec disagrees or adds a requirement, it says so in §2 instead of changing the decision silently.
**Evidence tags:** `[S#]` = source in §25. `[code]` = read in this repo. `[calc]` = my calculation. `[assumed]` = a placeholder to confirm by bench test or part selection.
**Revision 2 (2026-10-31):** applies the senior review and these binding user decisions: mechanical release on every bolt; keyed outside override; 24 V locks; exit button beats lockdown; force-open of a blocklisted vehicle admin-only with a reason and alert; unknown vehicles exit by button or app; a local server per site; GCP oversight (§26).

## 1. Summary

- The controller stops being a motor driver and becomes a **lock sequencer and supervisor in front of a commercial operator**. The operator keeps motor drive, collision sensing and beam response.
- **Recommended operators:** Centurion D5-Evo (or a D10 model for heavier gates) for sliding, Vector2 or Vantage on the V-Series controller for swing. Open is commanded on **FRX** (open-only, idempotent), close on **TRG**, stop on **LCK/STP in ESTOP mode**. State comes from the **Status** output plus our own limit sensors.
- **`LCK/STP` doubles as a hardware interlock:** the operator can move only while a logic AND of "all bolts retracted, all maglocks released, firmware permit" holds a contact closed. Firmware bugs, a dead board and the operator's own 433 MHz receiver therefore cannot drive into a bolt.
- **New gate states:** `Unlocking`, `Locking`, `ClosedInsecure`, `Verifying`, `Calibrating`. `Closed` now means *closed and locks proven*.
- **One server `GateArbiter`** takes every open request (plate, app, RTE, guard). The controller has one local fallback, for RTE only, with a durable event log.
- **Remaining flags (§2):** DC motors rule out AC clamp sensors; a USB L1 has limited reach; the Status output is not potential-free; leaf-to-leaf locks do not anchor to anything fixed. Egress and outage entry are closed by user decision (F1, F2).

## 2. Flags on user decisions

Each flag states the decision kept, the problem, and the handling. F1, F2 and F4 are closed by user decision.

| # | Sev | Decision | Problem | Proposal |
|---|---|---|---|---|
| F1 | **Closed** | Fail-secure bolts; battery by default | A fail-secure bolt stays shut with no power, so a flat battery, board fault, jam or fire must not trap anyone. | **Decided:** a mechanical release on every bolt, plus a keyed outside override, plus the hard-wired maglock cut (§6.4). Commissioning sign-off requires them, and the **operator release key kept reachable inside**, since an engaged operator still holds the gate after bolts and maglocks release. |
| F2 | **Closed** | Entry stays locked without the server | In an outage the family cannot drive in by software. | **Decided:** no software bypass. Outside entry in an outage is by the keyed override (§6.4), with the operator's key release for hand opening. A site UPS for server, cameras and LiDAR reduces how often it is needed (§12). |
| F3 | **High** | Current sensing by "Hall or clamp-on" | D5-Evo is a **12 V DC motor, 10 A at rated load** `[S1]`. An AC clamp or current transformer reads nothing on DC. | Split-core **DC Hall** sensor only. §15. |
| F4 | **High** | Two LiDAR per gate, local server per site | The L1 is USB-serial at 2 Mbaud `[code: docs]`; Ethernet needs Unitree's serial-to-UDP adapter. USB will not span most gate-to-server runs. | Use the UDP adapter, or a small gate-side bridge, unless the server is within USB reach. Check stock and price (Q7). §18. |
| F5 | **High** | Dual leaf: locks leaf to leaf only | Leaf-to-leaf locks tie the leaves to each other, not to the ground or a pillar. The tie blocks the leaves spreading, but near the closed position a shove in the swing direction is amplified into high tension across the joint (toggle geometry), and the whole resistance depends on hinge and operator stiffness. Top/bottom bolt alignment also relies on leaves that sag. | Layout kept as specified. Add a mechanical check at survey. Recommend considering a ground drop-bolt or centre stop for high-security sites (Q3). |
| F6 | **High** | Refuse to drive "while any bolt is still extended" | Not sufficient on its own. An energised maglock also fights the operator and would cause false collisions. | **Extended:** refuse while any bolt is extended **or** any maglock is still bonded. |
| F7 | **High** | Status "operator status" as a verification input | Status is a 4.5 V, 20 mA LED drive, not a contact `[S1]`. It also only blinks a pattern. | Read through an opto with a pattern decoder. Do not connect directly to a GPIO. §5.2. |
| F8 | Med | Battery keeps locks running | Three maglocks plus board draw would use a typical alarm battery in hours `[calc]`. | **Staged shedding:** after a set time on battery, release the maglocks, since bolts "alone hold" by your decision. §12. |
| F9 | Med | Bolts retracted on power | Holding a solenoid during long opens can overheat it. Duty depends on the chosen part. | Bolts released on `Open` after a timeout; retract and verify before close. §7.4. |
| F10 | Med | Board rev 3: 240 V on board | Mains on a low-voltage controller changes the design class: creepage, fusing, surge, earthing, enclosure access. LRS-class supplies are enclosed chassis units, so only IRM-class can sit on the PCB. A power-supply module alone does not charge a battery. | Prefer a separate mains compartment. Add a charger or DC-UPS stage. Qualified electrician at install. §12, §20. |
| F11 | Med | "Clamp-on" plus "non-contact" inside operator | A sensor inside an IP55 operator housing still means opening it and routing a signal cable. | Acceptable; no conductor is cut. Confirm warranty position with the dealer (Q6). |
| F12 | Med | Exit goes first | A vehicle already committed in the gate throat cannot back out for an exit request. | Exit-first applies to **waiting** vehicles. Occupancy overrides. §10. |
| F13 | Med | README decision #5 says fail-safe **open** on power loss | Directly contradicts the new fail-secure decision. `README.md`, `commissioning.md` 2.9 are stale. | The new decision wins; docs updated in step 4. |
| F14 | Low | "D10 Evo" | I found Centurion documents for D10 / D10 Turbo and D10 Smart. "D10 Evo" appears only on a Kenyan reseller's page `[S9]`. | Specify the exact model and read that manual before ordering. Not verified: D10 voltage and terminals. |
| F15 | Low | Plate images, guest data, 30-day retention | Plates and faces are likely personal data under Kenya's Data Protection Act 2019. I have not reviewed this. | Get advice for estate deployments (Q10). Remote data handling is limited by §26. |
| F16 | Low | App stream from the sub-stream | Datasheet sub-stream max is 640×480 at 25 fps. The **third stream** offers 1280×720 `[S5]`. | Use third stream for the app, with H.264. §11.3. |

### Existing defects found in the code

| Where | Defect | Fix in step 4 |
|---|---|---|
| `firmware/main/main.cpp`, `gate_controller.cpp` | The firmware holds a relay for the entire motion and rejects `PULSE_RELAY`, while ADR-008 and commissioning describe a trigger pulse. Not compatible with an operator. | Operator actuator profile (§5.3). |
| `gate_controller.cpp::apply_policies` | `reverse_on_beam` posts Open after a beam trip. The operator reverses on its own Safety Close input `[S1]`, so a second command would **stop or reverse the gate again**. | Disabled in the operator profile. |
| `fusion_engine.cpp` step 5 | Force-open returns AUTHORIZED before the blocklist check, contradicting step 7's comment. | Sticky force-open never beats the blocklist. A per-request admin force with reason and alert can (§17.5). |
| `fusion_engine.cpp` steps 6, 7 | A low-confidence read of a blocklisted plate surfaces as LOW_CONFIDENCE, not a block alert. | Add a soft blocklist check that raises the alert without changing the verdict. |
| `field_controller_service.cpp` | Every event uses a single `default_site_id_`. Blocks multi-site. | Resolve site from the gate. §18. |
| `gate_service.proto` Telemetry | `supply_voltage_v`, `core_temp_c` are never populated by firmware `[code]`. | §16. |
| `allowlist_store` | Blocklist has no list method; AdminService exposes none. | §17. |
| `history_store.cpp` | No schema versioning at all (`CREATE IF NOT EXISTS` only). | §19. |

## 3. Scope and non-goals

In scope: design for step 4 (firmware, server, proto, simulator), with requirements for step 5 (board) and step 6 (app).
Out of scope: MMYRC models (only the hook, §17.5), app UI design, KiCad work.
Non-goals: replacing operator safety, driving motors directly, any network path that overrides a physical interlock.

## 4. Operator research and recommendation

### 4.1 Comparison

| | Centurion D5-Evo | Centurion V-Series (Vector2, Vantage) | FAAC / BFT |
|---|---|---|---|
| Type | Sliding, 500 kg max `[S1]` | Linear swing, single or double `[S2]` | Sliding and swing ranges |
| Supply | 90 to 240 V AC, battery-driven 12 V 7 Ah `[S1]` | Battery-backed `[S2]` | Many 230 V AC models (FAAC 740 is 230 V `[S13]`), so **no motion in an outage** on those; 24 V DC models exist (FAAC C721 `[S14]`) |
| Standby | 35 to 44 ops on 7 Ah `[S1]` | Not extracted | Model dependent |
| Inputs | TRG, FRX, PED, LCK/STP, Safety Open, Safety Close `[S1]` | Same family, plus MTR M, MTR S `[S2]` | Not verified. Manuals not obtained; expect open/stop/photocell contacts |
| Status | Status LED, 4.5 V 20 mA; closed, open, opening, closing, no mains, low battery, multi-collision `[S1]` | Status LED output `[S2]` | Not verified |
| Locks | No lock output in D5 terminal text reviewed | One lock output, up to 2 A; mag or strike, pre-release 0 to 24 s `[S2]` | Not verified |
| Kenya price | KSh 58,000 to 78,000 ex-VAT in listings `[S7][S8][S9]`; "from about KSh 94,000" with installation parts `[S10]` | Vantage listings KSh 99,500 to 110,000 `[S11][S12]`. **No Vector2 listing found** | Kenyan installer blog cites BFT/FAAC only at heavy-duty KSh 185,000 to 250,000 `[S10]`. No distributor found |
| Availability | Several Nairobi resellers. Stock varies; one showed out of stock `[S9]` | Vantage: yes. Vector2: unconfirmed | Not confirmed |

Kenyan prices are reseller listings of unknown date, not quotes. I found no proof that any reseller is authorised by Centurion.

### 4.2 Recommendation

**Centurion is the reference.** Reasons: local availability and installers, 12 V DC battery operation matching our outage case, and documented discrete inputs. FAAC/BFT stay as a generic "contact profile" with no reference support until a manual is checked.

Selection: D5-Evo up to 500 kg. D10 family for heavier gates (Premier quotes about 1,000 kg for the D10 Smart `[S10]`, check the datasheet). For swing, buy **Vantage or Vector2 on the V-Series controller**, whichever a dealer can supply. The V-Series guide says Vector2, Vert-X and V400/V500 use the same controller `[S3]`.

ADR-008 names "R5" and two per-leaf triggers. That is obsolete for this design: V-Series is one controller, one trigger, two motors. Update ADR-008. ADR-008 also lists RS-485 on D5-Evo; I found no public protocol in these manuals, so it is not used.

## 5. Gate types and operator signals

### 5.1 Mapping

| Our function | Centurion terminal | Behaviour `[S1]` | Sliding / single leaf | Dual leaf |
|---|---|---|---|---|
| `OPEN` | FRX to Com, 500 ms pulse | Opens a closed/closing gate; no effect while open/opening; **never closes** | Same | Same, one input; controller sequences leaves |
| `CLOSE` | TRG to Com, 500 ms pulse | Mode dependent; in Standard stationary → open or close; moving → stop; next → reverse `[S3]` | Sent only if decoded state is stationary open or part-open | Same |
| `STOP` / `PERMIT` | LCK/STP, ESTOP mode, normally-closed | Open circuit stops immediately `[S1]` | §6.3 | Same |
| Close beam | Safety Close | Prevents close; stops and opens if closing | Beam wired **to the operator**; we tap it | Same |
| Open beam (swing sweep) | Safety Open | Prevents opening; stops and closes if opening | Link fitted on sliding unless site needs it | **Wire it** for a sweep-zone beam |
| Status | Status to Com | LED drive | Opto, §5.2 | Same |
| Pedestrian | PED | Partial open | Optional, unused first release | Not mapped |

Operator settings required by commissioning: **Standard mode, Autoclose OFF, operator gate-lock feature OFF, LCK as ESTOP = Yes, onboard receiver disabled** unless the customer keeps remotes (the interlock still protects them). Leave collision sensing at factory-recommended settings. Firmware owns auto-close.

Single-leaf uses the sliding signal set but `leaf_count=1` and swing timing. Dual leaf has `leaf_count=2`: leaf delay and order are set **on the operator** (Vector2 Leaf Delay, Positive Close Mode to seat a maglock `[S4]`), never in firmware.

### 5.2 Status decoder

Status → opto → GPIO, sampled every 20 ms. A 2.5 s window classifies:

| Pattern `[S1]` | Decoded |
|---|---|
| Off | `OP_CLOSED` |
| On | `OP_OPEN_OR_PART` |
| Continuous slow flash | `OP_OPENING` |
| Continuous fast flash | `OP_CLOSING` |
| 1 flash per 2 s | Pillar-light override (ignore) |
| 2 flashes per 2 s | `OP_NO_MAINS` |
| 3 flashes per 2 s | `OP_LOW_BATT` |
| 4 flashes per 2 s | `OP_MULTI_COLLISION` |

Flash rates need measuring on a real unit (B1). Whether a fault pattern hides motion patterns is unknown (B2). Status is advisory; limit sensors decide position. The V-Series table is not fully extracted and must be confirmed.

### 5.3 Actuator abstraction

```cpp
class GateActuator {            // new, in gate_control
  virtual void open();          // FRX pulse | contactor open
  virtual void close();         // TRG pulse | contactor close
  virtual void stop();          // PERMIT drop then re-arm | contactors off
  virtual OperatorStatus status() const;
};
// ContactorActuator: today's behaviour, rev 2 boards and the simulator.
// OperatorActuator: pulses, permit, status decoder, direction check.
```

`Action::DriveMotorOpen/Close/StopMotor` keep their names and map to the actuator, so the state machine stays hardware-agnostic.

**Direction check:** applies only in the start window `op_start_timeout_ms` (default 2,000) after a command, and only if no beam trip or collision pattern occurred in that window or the prior 3 s. Mismatch = the first motion seen is opposite to the command: `stop()`, fault `OPERATOR_DIRECTION_MISMATCH`. No motion: one retry, then `OPERATOR_NO_RESPONSE`.

**Operator reversal.** Once motion has started, a change of direction is the operator doing its job (collision or beam). It raises `OperatorReversed`, firmware **follows** and logs, and sends no command. `stop()` and `FW_PERMIT` drop are suppressed for `reversal_guard_ms` (3,000) after a suspected reversal unless a human stop arrives. Lock interlock breaks still stop the operator in hardware; whether that can pin a person is bench test B13.

**Mid-travel resume:** TRG direction after a stop depends on the operator's memory. Open is safe on FRX. Close from a mid-travel stop sends TRG, runs the direction check, and if wrong stops and retries once. Behaviour after an ESTOP needs bench proof (B3).

## 6. Lock hardware model

### 6.1 Layouts

`lock_layout` is config:

| Layout | Gate type | Channels |
|---|---|---|
| `LOCK_5` | Dual leaf | B1 top bolt, B2 bottom bolt, M1, M2, M3 between them, leaf to leaf |
| `LOCK_4` | Sliding, single leaf | B1, B2 gate to pillar, M1, M2 |

Channel M3 is unused on 4-lock; its driver is off and its feedback ignored.

### 6.2 Electrical polarity

| Device | Unpowered | Powered | Reset state |
|---|---|---|---|
| Solenoid bolt, fail-secure | **Extended** (spring) | Retracted | Off → extended |
| Maglock | Released | Bonded | Off → released |

So a reset or power loss always leaves "bolts extended, maglocks released", matching your "bolts alone hold".

Feedback: bolt = **two inputs**, `RETRACTED` and `EXTENDED`, from a changeover contact. A single contact cannot tell "extended" from a broken wire. Both-true or both-false for over 300 ms = `LOCK_FEEDBACK_FAULT`. Maglock = one bond-sense input. Total 7 inputs for `LOCK_5`, 6 for `LOCK_4`.

Bolt "extended" proves the pin moved, not that it entered the keeper. Prefer a keeper-side sensor if the part offers one (Q4).

### 6.3 Hardware interlock (new requirement)

```
BOLT_RETRACTED_1..2 ─┐
MAG_RELEASED_1..3  ──┤
NOT KEY_ACTIVE     ──┤  discrete AND (no firmware) ──► K_PERMIT coil ──► NO contact ──► operator LCK/STP
WDT_OK             ──┤
FW_PERMIT (GPIO)   ──┘
```

`K_PERMIT` de-energised = LCK open = operator inhibited. Board dead, firmware hung, bolt not back: operator cannot move, from any source including its own radio or a keypad.

Consequences:
- LCK as ESTOP stops immediately if a bolt falls mid-travel. Correct behaviour.
- While `Open` with bolts released the operator is inhibited. Firmware re-permits before any close.
- A person cannot use the customer's remote without firmware cooperation. Tell customers this. Remotes wired to the operator do nothing while locked. Any remote must come in as an RTE-class source (§9) if wanted.
- An ESTOP-off with the system idle must not auto-start the operator. Verify (B3).

### 6.4 Emergency release, key override and watchdog (firmware-independent)

**Maglock power cut.** The maglock 24 V feed passes through two normally-closed contacts in series, upstream of the firmware-controlled drivers: an **inside emergency release** (latching, labelled, reachable without tools) and an **outside keyed override**. Opening either removes maglock power by wire alone, with hung firmware or a dead board. Each device has a second contact for sensing. The inside release shows its latched state at the button (a lamp or a mechanical flag, working without firmware), and if it stays latched over `release_latched_alert_s` (300) firmware raises `RELEASE_LATCHED`.

**Bolts.** Every bolt has a mechanical release from inside. The outside keyed override also retracts them mechanically (keyed cylinder or cable; depends on part, Q4). The operator keeps its own key release for hand opening `[S1]`. Bolt and maglock release alone do not free a gate held by an engaged operator, so **commissioning must place the operator release key reachable inside**, at the emergency release, sealed but not locked away.

**Key sense.** `KEY_SENSE_IN`, `KEY_SENSE_OUT` and `MAG_CUT_SENSE` go to the expander and also feed `KEY_ACTIVE` in the hardware AND, so the operator cannot move while someone is releasing the gate by hand. With key sense active, loss of bond, bolt movement or a released closed limit raises `KEY_OVERRIDE` (Warn, audited, alert), **not** `LOCK_DROPPED` or `GATE_FORCED`. State goes to `ClosedInsecure` (reason `KeyOverride`) or follows the limits. The same evidence with no key sense stays Critical. Clearing: contacts restored, locks re-proven by the normal Lock sequence.

**External watchdog.** A windowed hardware watchdog (TPS3431 class `[assumed]`) is kicked by a dedicated pin from the `gate_ctrl` pump task only. On expiry it latches `WDT_OK` low, which gates all five `LOCK_EN` lines and the permit in hardware until the MCU resets. The result is the safe state: bolts extended, maglocks released, operator inhibited. `WDT_TRIPPED` is readable after reset and raises a fault.

## 7. Lock sequencing

All times are defaults in `GateConfig`, to be tuned by bench test (B4).

### 7.1 Unlock (before any motion)

| Step | Action | Must see | Timeout | On failure |
|---|---|---|---|---|
| 1 | Check battery (§12) | Non-RTE: `V ≥ reserve_v`. RTE: `V ≥ cutoff_v` | n/a | Non-RTE: refuse, `BATTERY_RESERVE`. RTE: always attempted above cutoff; on failure logged, mechanical release is the way out |
| 2 | Maglocks off | All bonds lost | 500 ms | Retry once at +500 ms, then `MAGLOCK_STUCK` |
| 3 | Bolts on (pull) | All `RETRACTED` | 800 ms | Off 500 ms, retry ×2, then `BOLT_JAM` |
| 4 | Reduce to hold if PWM hold is used | | | |
| 5 | Assert `FW_PERMIT` | `K_PERMIT` readback | 100 ms | `PERMIT_FAULT` |
| 6 | Wait `pre_move_ms` (200) | | | |
| 7 | `actuator.open()` | Direction check | 2,000 ms | §5.3 |

Maglocks go first: their pull loads the bolts sideways. There is no "push against the gate to unload a bolt" step because motion is forbidden until bolts retract. If a bolt binds in practice, that is a mechanical fix, not firmware.

Failure at any step: bolts off, maglocks on, state `Faulted` (`LockJam`), alert, **no motion**. Clearing is defined in §7.3.

### 7.2 Lock (after closing)

| Step | Action | Must see | Timeout | On failure |
|---|---|---|---|---|
| 1 | All closed-limit inputs true; settle | Stable | 300 ms | |
| 2 | Drop `FW_PERMIT` | | | |
| 3 | Maglocks on | All bonded | 2,000 ms | Bolt step still runs, then `ClosedInsecure` |
| 4 | Bolts off | All `EXTENDED` | 1,000 ms | Retry: on 500 ms, off; ×2 |
| 5 | All proven | | | `Closed` |

Maglocks first pull the leaf into line so bolts land cleanly. This is the reverse of the unlock order. Failure ends in `ClosedInsecure`: gate shut, alert `LOCK_INCOMPLETE` naming the channel, and a repeat attempt every `relock_retry_s` (60) up to 5 times.

### 7.3 Faults

| Fault | Severity | Behaviour | Clear |
|---|---|---|---|
| `BOLT_JAM` | Error | No motion; maglocks restored | Admin reset or local reset (below), after fresh retract test |
| `MAGLOCK_STUCK` | Error | No motion | Same |
| `LOCK_FEEDBACK_FAULT` | Error | Treated as **unsafe to drive** and **not proven locked** | Input stable + reset |
| `LOCK_INCOMPLETE` | Warn→Error | `ClosedInsecure`, opening allowed if retract proves OK | Auto, on success |
| `LOCK_DROPPED` | Critical | Bond or bolt lost while `Closed` and not commanded: tamper or failure | Ack |
| `GATE_FORCED` | Critical | Closed limit released while `Closed`: forced or manual release | Ack + re-resolve |
| `PERMIT_FAULT` | Critical | Readback disagrees: drop all, no motion | Service |
| Driver overcurrent/open load | Error | Per-channel sense, §20 | Service |
| `KEY_OVERRIDE` | Warn | §6.4 | Contacts restored and locks proven |
| `WDT_TRIPPED` | Error | Safe state by hardware | Reset; logged |

**Fault clearing.**

| Class | Faults | RTE press | Auto retry | Local reset (RTE held 5 s, under the 10 s stuck limit) |
|---|---|---|---|---|
| Non-lock | `OPERATOR_NO_RESPONSE`, `OPERATOR_DIRECTION_MISMATCH`, `MotorTimeout`, unknown position at boot | Clears the fault and opens by FRX (open-only is safe from any position) | One automatic reset after 60 s if limits are consistent, at most once per 10 min; never moves the gate by itself | Allowed |
| Lock | `BOLT_JAM`, `MAGLOCK_STUCK`, `LOCK_FEEDBACK_FAULT` | Runs one fresh retract test; if proven, opens. If not, alert and remain; mechanical release | None | Allowed, runs the same retract test |
| Hardware | `PERMIT_FAULT`, both limits asserted | Logged only; mechanical release | None | Not allowed; service |

Every local reset and retry is a stored event, so this works offline. Admin reset (`COMMAND_KIND_CLEAR_FAULT`) remains for the online case.

### 7.4 Bolts while the gate is open

Bolts stay retracted from unlock through motion. After reaching `Open`, or on entering `StoppedOpen`/`StoppedClose`, firmware holds them for `bolt_hold_open_s` (default 45, must be **≤ the rated duty** of the chosen part; the timer is cumulative across a stop and is not reset by it), then releases them and drops `FW_PERMIT`. Any later motion first re-runs retract and verify: `Unlocking` for open, `PreClose` for close. A failed retract leaves the gate where it is, with an alert.

Maglocks stay off from unlock until close completes.

### 7.5 Per-lock cycle count

Each lock and each operator command increments a counter stored in NVS, batched every 10 events or on state rest to limit flash wear. Sent in §16.

## 8. State machine changes

Existing enum values stay stable because telemetry serialises them. `gate_state_machine` remains pure.

### 8.1 States

```cpp
enum class State : uint8_t {
  Initializing=0, Closed=1, Opening=2, Open=3, Closing=4,
  StoppedOpen=5, StoppedClose=6, Faulted=7,
  Unlocking=8,       // locks releasing; motor off
  Locking=9,         // at closed limit; locks engaging
  ClosedInsecure=10, // shut, at least one lock unproven
  PreClose=11,       // bolts retracting before a close
  Verifying=12,      // gate-type pulse
  Calibrating=13,    // measured open/close cycles
  Lockdown=14,       // closed, locked, remote opens refused
};
```

`Closed` semantics change to **closed and locks proven**. With `lock_layout=NONE` (rev 2), `Closing --LimitClosedHit--> Closed` is unchanged, so existing state machine tests keep passing.

### 8.2 Events (appended from 11)

`LocksReleased`, `LocksEngaged`, `LockFailed`, `LockDropped`, `OperatorMoving`, `OperatorNoResponse`, `OperatorDirectionWrong`, `OperatorCollision`, `OperatorReversed`, `KeyOverrideOn`, `KeyOverrideOff`, `LocalReset`, `CurrentOverrun`, `BeginVerify`, `VerifyPassed`, `VerifyFailed`, `BeginCalibrate`, `CalibrateDone`, `CalibrateFailed`, `LockdownOn`, `LockdownOff`, `GateForced`, `BatteryCritical`.

### 8.3 Actions and reasons

Actions: `ReleaseLocks`, `EngageLocks`, `RetractBolts`, `SetPermit`, `ClearPermit`, `RunVerifyPulse`, `RunCalibration`, `RaiseAlert`. Raise `kMaxActionsPerStep` from 3 to 6.

Reasons: `LockJam`, `LockIncomplete`, `LockFeedback`, `OperatorFault`, `OperatorCollision`, `GateTypeMismatch`, `CalibrationFailed`, `BatteryReserve`, `ForcedEntry`, `CurrentAnomaly`, `Lockdown`, `KeyOverride`, `Watchdog`.

### 8.4 Transitions

| From | Event | Guard | To | Actions |
|---|---|---|---|---|
| Closed, ClosedInsecure | CommandOpen | RTE: `V ≥ cutoff_v`, no lockdown check. Others: lockdown latch off, `V ≥ reserve_v` | Unlocking | ReleaseLocks |
| Unlocking | LocksReleased | | Opening | SetPermit, DriveMotorOpen |
| Unlocking | LockFailed | | Faulted (LockJam) | EngageLocks, ClearPermit, RaiseAlert |
| Opening | LimitOpenHit | all open limits | Open | StopMotor |
| Opening, Closing | OperatorCollision | | Follow Status | Operator owns reversal, we log, no command |
| Open | CommandClose | beam clear | PreClose | RetractBolts if rested |
| PreClose | LocksReleased | | Closing | SetPermit, DriveMotorClose |
| PreClose | LockFailed | | Open | RaiseAlert |
| Closing | LimitClosedHit | all closed limits | Locking | StopMotor, ClearPermit, EngageLocks |
| Closing | SafetyBeamTripped | operator profile | Opening | **No command**: follow the operator's own reversal; confirm by Status |
| Closing | SafetyBeamTripped | contactor profile | StoppedClose | Existing behaviour |
| Locking | LocksEngaged | | Closed | |
| Locking | LockFailed | | ClosedInsecure | RaiseAlert |
| ClosedInsecure | LocksEngaged | | Closed | |
| Closed | LockDropped, GateForced | | ClosedInsecure | RaiseAlert (Critical) |
| Opening, Closing (start window) | OperatorDirectionWrong, OperatorNoResponse | §5.3 | Faulted | StopMotor, RaiseAlert |
| any moving | CurrentOverrun | | unchanged | Log and alert only, no motion command |
| any moving | OperatorReversed | | Follow direction | Log only |
| Closed | BeginVerify | admin nonce, beam clear, lane empty | Verifying | ReleaseLocks then RunVerifyPulse |
| Verifying | VerifyPassed | | Calibrating or Locking | |
| Verifying | VerifyFailed | | Closing then Locking | StopMotor, RaiseAlert |
| Closed | LockdownOn | | Lockdown | |
| Lockdown | CommandOpen source RTE | `V ≥ cutoff_v` | Unlocking | RaiseAlert (Critical); latch stays set |
| Lockdown | KeyOverrideOn | | ClosedInsecure | Latch stays set |
| any | MotorTimeout | moving | Faulted | Existing |
| Stopped*, Open | BatteryCritical | beam clear | PreClose | §12 |
| Stopped*, Open | BatteryCritical | beam blocked | unchanged | Retry every 10 s; never close into a blocked beam |
| Locking | CommandOpen | RTE, or others under the Closed rule | Unlocking | Abort lock sequence |
| PreClose | CommandOpen | RTE: `V ≥ cutoff_v`. Others: lockdown latch off, `V ≥ reserve_v` | Open | Cancel close, re-arm timers; bolts stay retracted. If the guard fails, the close continues |
| Closing | CommandOpen | Same guard | Opening | FRX re-opens `[S1]`. If the guard fails, refused and the close continues |
| Verifying, Calibrating | CommandOpen | RTE, locks engaged | Unlocking | Abort procedure; config stays unverified |
| Verifying, Calibrating | CommandOpen | RTE, locks released | Opening | Abort procedure; config stays unverified |
| Verifying, Calibrating | CommandOpen | non-RTE | unchanged | Refused `BUSY` |
| ClosedInsecure | CommandOpen with lockdown latched | RTE only | Unlocking | Latch stays set |
| StoppedOpen, StoppedClose | CommandOpen | RTE at `cutoff_v`, others at reserve and latch off | Unlocking if bolts rested, else Opening | |
| StoppedOpen, StoppedClose | CommandClose | beam clear | PreClose if bolts rested, else Closing | |
| Unlocking | CommandStop | | Locking | Abort, re-engage |
| PreClose | CommandStop | | Open | Cancel close |
| Locking | LocksEngaged | lockdown latch set | Lockdown | Replaces Closed while latched |
| any | LockdownOn | | see below | Set NVS latch |
| any | LockdownOff | | unchanged, or Lockdown → Closed | Clear latch |
| Closed, ClosedInsecure | KeyOverrideOn | | ClosedInsecure | Log, alert |
| Opening, Closing | KeyOverrideOn | | Stopped* | Hardware already stops the operator; ClearPermit, log, alert |
| Unlocking, PreClose, Verifying, Calibrating | KeyOverrideOn | | Stopped* if moved, else ClosedInsecure (or Open from PreClose) | Abort, ClearPermit, no motion |
| Locking | KeyOverrideOn | | ClosedInsecure | Abort sequence |
| Open, Stopped*, Faulted | KeyOverrideOn | | unchanged | Log, alert; motion refused while active |
| ClosedInsecure | KeyOverrideOff | closed limits true | Locking | EngageLocks; retry counter reset, so relock restarts after its 5 tries were used |
| Open, Stopped*, Faulted | KeyOverrideOff | | unchanged | Log; motion allowed again by normal commands only, no auto-resume |
| Faulted | LocalReset | §7.3 | Initializing | Resolve position |
| Faulted | CommandOpen | RTE, §7.3 class allows | Unlocking or Opening | |

`CommandStop` remains honoured from every moving state. Motor watchdog uses calibrated run time × 1.5, instead of a fixed 30 s, once calibration exists. Faulted motion clears only by reset; on reset, `resolve_boot_position` also reads lock feedback: closed and all proven → `Closed`, closed and unproven → `ClosedInsecure`.

**Lockdown is a persisted latch, separate from the state.** `LockdownOn` by state:

| State | Effect |
|---|---|
| Closed | Lockdown |
| Closing, PreClose, Locking | Continue; end in Lockdown |
| Opening, Open, Stopped* | Beam-guarded close, then Lockdown. Not if the open came from RTE in progress: wait for `offline_close_s`, then close |
| Unlocking | Abort, Locking |
| Verifying, Calibrating | Abort, close |
| ClosedInsecure, Faulted | Latch only; Lockdown when proven and closed |

An RTE open under lockdown opens, closes after `offline_close_s` (beam-guarded), and returns to Lockdown. Only `LockdownOff` clears the latch.

### 8.5 State to wire map

Replaces the `to_wire_state` switch. Lockdown latch alone does not change the map while moving.

| `State` | `GateState` |
|---|---|
| Initializing | UNKNOWN (0) |
| Closed | CLOSED (1) |
| Opening | OPENING (2) |
| Open | OPEN (3) |
| Closing | CLOSING (4) |
| Faulted | FAULT (5) |
| Lockdown | LOCKDOWN (6) |
| Unlocking | UNLOCKING (7) |
| Locking | LOCKING (8) |
| ClosedInsecure | CLOSED_INSECURE (9) |
| Verifying | VERIFYING (10) |
| Calibrating | CALIBRATING (11) |
| PreClose | PRE_CLOSE (12) |
| StoppedOpen, StoppedClose | STOPPED (13), direction in `reason` (today both report UNKNOWN `[code]`) |

Boot: if mid-travel, stay in the present `init_unknown` fault. Never self-drive to find position.

`docs/diagrams/05-gate-state-machine.mmd` is updated with the code.

### 8.6 Accepted implementation deviations (step 4, phase 1)

- Extra events: `BoltsRested`, `LockFeedbackFault`, `PermitFault`. Extra actions: `PersistLockdown`, `RestBolts`. Extra reasons: `PermitFault`, `PositionUnknown`, `Busy`. `kMaxActionsPerStep` is 8, not 6 (the state-by-event test proves no step fills it).
- `RetractBolts` is emitted on every close from `Open`; the sequencer confirms at once if bolts are already retracted.
- A beam trip during `PreClose` cancels the close.
- A cancelled `PreClose` from a stopped state goes to `Unlocking`. Stop or key-on during a mid-travel `Unlocking` ends stopped.
- Lockdown during `Opening` closes on reaching `Open`, including after a reversal, unless an RTE cycle is in progress. `KeyOverrideOff` retries a pending lockdown close, the one exception to no auto-resume.
- `ClosedInsecure` follows the limits: a hand move goes to `StoppedOpen`, and a hand return to `ClosedInsecure`.
- Key-on clears proven-lock status in every state, so a later close re-proves. Faults and key-on during lock sequences rest the bolts.
- `StoppedOpen` and `StoppedClose` report `GATE_STATE_STOPPED` on legacy firmware too; the app shows it as "Unknown", as before.

## 9. Single open-request path

### 9.1 Model

Every open is an `OpenRequest` with `{request_id, gate_id, side, source, ts, actor?, decision_id?}`.

| Source | Enum | Origin |
|---|---|---|
| Plate | `OPEN_SOURCE_PLATE` | Fusion AUTHORIZED |
| App | `OPEN_SOURCE_APP` | IssueCommand with user |
| RTE | `OPEN_SOURCE_RTE` | Button on controller |
| Guard | `OPEN_SOURCE_GUARD` | Guard override |
| Guest | `OPEN_SOURCE_GUEST` | Fusion via guest pass |
| Schedule | `OPEN_SOURCE_SCHEDULE` | Reserved |

### 9.2 Online flow

```
Plate ─ fusion ─┐
App, Guard ─────┼─► GateArbiter (one per gate) ─► audit ─► GateCommand(OPEN, source, side, request_id)
RTE ─ Control ──┘            │                                   │
                         queue, §10                         firmware ack
```

- `SubmitDetection` stops publishing commands directly. `DashboardServiceImpl::IssueCommand` stops doing so for OPEN_GATE. Both call `GateArbiter::submit()`.
- No duplicate logic: the arbiter is the only publisher of OPEN_GATE, the audit row is written there.
- Allow/deny stays in fusion for plates. The arbiter decides *when*, not *whether*.

### 9.3 RTE on the controller

The button: normally-open, inside only, supervised by an end-of-line resistor read on the ADC, so a cut or short raises `RTE_LINE_FAULT`. Debounce 50 ms, rising edge; ignored while held. Held 5 s = local reset (§7.3). Held over 10 s raises `RTE_STUCK` and the input is ignored until released. Rate limit: one per 3 s.

```
RTE press
 ├─ Link up → send OpenRequest, wait for OpenReply; rte_grant_timeout_ms = 1,500
 │     ├─ GRANTED, then command → normal path
 │     ├─ QUEUED → wait up to rte_queue_max_s (20), then local authority
 │     └─ Timeout → local authority (below)
 └─ Link down (no Control stream, or no server frame for 5 s) → local authority
```

Local authority: firmware adds `request_id` to the served ring, posts `CommandOpen`, logs the record, arms a **local auto-close** of `offline_close_s` (default 30), subject to the beam.

**Late grants.** Firmware refuses any OPEN whose `request_id` is in a persisted ring of the last 32 served ids, and acks `ALREADY_SERVED` with no motion, so a grant arriving after a local open and close cannot produce a ghost cycle. Every OPEN from a capable server carries `expires_ts` (issued + 10 s); firmware refuses expired commands whatever the CommandGuard age limit. Unsynced clock: expiry falls back to arrival within the live stream only. The arbiter marks an offline-served `OpenRequest` as served and cancels any pending grant.

**Signature.** `GateCommand.signature` is unused today: no code reads or writes it `[code]`. When enabled, it covers the canonical encoding of **all** fields except `signature`, including `source`, `side`, `request_id`, `expires_ts`, `config` and `nonce`. New fields must be added to the canonical encoding in the same change.

Entry stays locked offline: no other source acts locally. App over LAN goes via the server, so it also stops when the server stops.

### 9.4 Store-and-forward

Record (fixed 64 bytes, little-endian): `start u16`, `seq u32`, `boot_id u32`, `uptime_ms u64`, `unix_ms i64`, `kind u8`, `source u8`, `side u8`, `result u8`, `state_after u8`, `reserved[11]`, `request_id[16]` (binary UUID, never text), `crc32 u32`, `commit u16`.

`boot_id` is an NVS `u32`, incremented and committed before the first record of a boot; on NVS loss it restarts from the highest `boot_id` found in the ring + 1. **Power-cut safety:** write the record with `commit=0xFFFF`, then program `commit=0xA55A` as a separate write. Recovery scans the ring and ignores any record with a missing commit marker or a bad CRC, so a torn write loses one record only.

- Durable ring in a dedicated 64 KB flash partition: 16 sectors of 4 KB, each with a 32-byte header, one always erased, giving **15 × 63 = 945 records** `[calc]`, or FRAM if selected. Written before the command runs; if the write fails, egress still proceeds.
- Kinds: `OFFLINE_OPEN`, `LOCAL_RESET`, `KEY_OVERRIDE`, `LOCK_FAULT`, `FORCED`, `POWER_LOST`, `POWER_RESTORED`, `CONFIG_APPLIED`.
- On reconnect, firmware sends `EventBatch` (max 16 records, one at a time). Server inserts with unique `(gate_id, boot_id, seq)`, replies `EventAck{last_seq}`. Records are freed only on ack, so an old server that ignores them loses nothing.
- Ring full: drop the oldest, count drops, and raise `EVENTS_DROPPED`.
- Time: with no sync at event time, the server computes `unix_ms` from the current uptime offset for the same `boot_id`. A battery RTC (§20) covers reboots.

### 9.5 Lockdown

Firmware persists the lockdown latch in NVS so it survives link loss and reboot. **Decided: the exit button beats lockdown**, always, with a Critical alert; there is no setting to invert this. Lockdown still refuses plate, guest, app and guard opens. The latch survives the RTE cycle (§8.4). Mechanical release stays available in all cases.

## 10. Two sides and lane arbitration

### 10.1 Sensing pairs

Each gate has `side ∈ {ENTRY, EXIT}`, each with one camera + one L1. One `gate-vision` process per side, with new `--side`. `DetectionFrame` gains `side` and `camera_id`.

Vehicle authorisation on the exit side: known allowlisted vehicles open automatically; blocklisted vehicles raise an alert and do not auto-open; **unknown vehicles exit by the button or the app** (a tagged request from a signed-in user at any role allowed to open), and the guard is alerted. A resident on foot uses RTE. Single-visit guests may exit on the same pass until `valid_until` or 24 h.

### 10.2 Arbiter state, per gate

```
current: none | {side, request, phase: GRANTED | PASSING}
waiting: entry[], exit[]  (FIFO per side)
occupied: bool  (from LiDAR gate-zone tracks)
```

### 10.3 Rules, shared lane (`lane_mode=SHARED`, default)

1. **Occupied wins.** A vehicle already in the throat finishes; no new grant in the opposite direction until clear.
2. **Exit before entry** among waiters. Requests within `simultaneity_window_ms` (500) are treated as simultaneous.
3. **Both logged:** each request gets an audit row with `queued_ms`, and `preempted_by` when it waits behind the other side.
4. **No needless cycle:** if entry is next when exit clears and the gate is still open, grant entry with no close.
5. **Pass timeout:** a grant unused after `pass_timeout_s` (30) is abandoned, the next waiter goes.
6. **Queue TTL:** an entry waiter expires after `entry_ttl_s` (120), or when LiDAR says it left. Auto-close is held off while a live waiter exists; the beam still rules.
7. **No starvation:** after `max_exit_streak` (3) consecutive exits, one entry waiter goes next.
8. **Re-validate:** a queued entry is rechecked against block and lockdown at grant time.
9. **Same vehicle dedupe:** a repeat read of the same plate on the same side refreshes the request.

Clear detection: LiDAR track crosses the gate plane, or beam cycles trip-then-clear. Either completes `PASSING`. If LiDAR is down, use beam only, then a fixed `pass_timeout_s`.

Physical signalling: the waiting driver has no idea they are queued. An optional red/green lamp output is budgeted (§20).

### 10.4 Two-lane (`lane_mode=TWO_LANE`)

Each side has its own arbiter queue. Rule 1 and 2 do not apply between sides, but the gate has one close: it closes only when neither side has a live request or occupancy. Applies to wide gates with marked lanes, and to two physical gates configured as separate gates. Config per gate, changed only while closed.

### 10.5 Scenarios

| Scenario | Result |
|---|---|
| Entry and exit within 500 ms | Exit granted, entry queued, both logged |
| Exit arrives while entry vehicle is mid-throat | Entry finishes, gate stays open, exit next |
| Entry waits, exit RTE pressed, nobody leaves | Timeout 30 s, entry next |
| Gate closing, exit arrives | Fresh open; operator re-opens on FRX `[S1]` |
| Server down, entry car and RTE | RTE opens locally. The entry car is not authorised, but physically could enter; the record shows an RTE open only. Known gap: tailgate window |

## 11. Camera

### 11.1 Choice

DS-2CD2643G2-IZS is **confirmed suitable on specification** `[S5]`: 4 MP 2688×1520, 2.8 to 12 mm motorised, shutter to 1/100,000 s, 120 dB WDR, IR to 60 m, RTSP, ONVIF, ISAPI, IP67, IK10 (`[S15]`), PoE 802.3at Class 4, max 15 W, or 12 VDC 13 W.

The 2.8 mm to 12 mm change `[calc]`, with a nominal 5 mm sensor width and a target of 250 pixels per metre (130 px across a 520 mm plate, `[assumed]` as a design figure): 2.8 mm reaches ≈ 6 m, consistent with the "3 to 5 m" in your note. 12 mm reaches ≈ 26 m in theory. Practical planning figure: 5 to 15 m, at 6 to 8 mm in most drives. Prove with the ALPR model at the site.

### 11.2 Availability in Kenya: **not confirmed**

I found no Kenyan listing for this exact model. Kenyan sellers carry G2 fixed models: DS-2CD2T46G2H-2I at KSh 21,500 ex-VAT, DS-2CD2046G2H-IU at KSh 18,000 ex-VAT `[S6]`. A Jiji ad offers an unnamed "4MP Acusense, motor 2.8-12mm" at KSh 14,999, which is too low to trust without a model check `[S16]`. Europe: about €210 ex-VAT to €400 `[S17][S18]`. Planning figure: **KSh 35,000 to 60,000 landed `[assumed]`**. Request quotes from `[S6]` sellers and Hikvision Store Kenya `[S19]` for this model, and the alternatives:

| Option | Note |
|---|---|
| DS-2CD2643G2-IZS | Primary |
| **DS-2CD2A46G2T/P-IZSY**, **DS-2CD3646G2T/P-IZSY** | Hikvision's ANPR-tuned varifocal 4 MP line `[S20][S21]`. Better for plates, cost and Kenyan stock unknown. Ask for it |
| Any G2 motorised 2.8 to 12 mm with RTSP, a true 720p extra stream | Acceptable if bench-tested |

Fixed-lens cameras do not solve the range problem: a 4 mm reaches about 8 m `[calc]`.

### 11.3 Streams

| Stream | Path `[assumed, confirm on unit]` | Use | Settings |
|---|---|---|---|
| Main | `/Streaming/Channels/101` | ALPR, MMYRC, 2688×1520, 25 fps | H.264 or H.265, per decoder; shutter ≤ 1/500 by day, night plate test |
| Sub | `/Streaming/Channels/102` | Fallback thumbnail, 640×360 | H.264 |
| Third | `/Streaming/Channels/103` | App, MediaMTX | **H.264 720p**, 15 fps, 1 to 2 Mbit/s |

The datasheet's table `[S5]` shows third-stream codecs H.265/H.264. Force H.264 because WebRTC clients widely support it and H.265 support is uneven. Your decision says "sub-stream feeds the app": F16 asks to use the third stream for quality, and 102 stays the fallback.

MediaMTX pulls RTSP on the server and offers WebRTC, then HLS. RTSP credentials never reach the app. Tokens are per gate and short-lived. Both streams at once add load to the camera's encoder; test.

PoE: existing `TL-SG1005P` `[code: docs]` supports 802.3at with 65 W, fine for two cameras. Note `DS-2CD2043G2-I` mentions in proto comments and docs change.

## 12. Power and power-loss behaviour

### 12.1 Distribution

```
240 V AC ─ fuse, MOV ─ certified AC-DC ─ charger/DC-UPS ─ 24 V battery (2 × 12 V) ─ BATT_BUS_24
BATT_BUS_24 ─► lock drivers, via hard-wired maglock cut for maglocks (§6.4)
BATT_BUS_24 ─ isolated or non-isolated DC-DC ─ 12 V ─► controller, beam, operator contacts
Operator: keeps its own mains feed and battery. Never shared with ours.
```

**Decided: 24 V locks.** 12 V comes from a DC-DC on the battery bus, so it is backed up. This differs slightly from "AC-DC gives 24 V and 12 V": a mains-side 12 V output would die in an outage. Charger: float near 27.3 V for lead-acid at 25 °C `[assumed]`, so a plain 24 V module cannot charge; use a 27 to 28 V unit or a DC-UPS, and rate lock parts for 28 V. Charge current about 0.1 C. **Charger setpoint and all §12.3 thresholds are tied to the chosen battery chemistry:** figures here are for lead-acid, and a LiFePO4 or other choice needs new values. The 12 V DC-DC input must be rated for charger transients and load dump, not just 28 V nominal.

### 12.2 Load estimate `[assumed until parts chosen]`

| Load | W |
|---|---|
| Controller, via DC-DC at 90% | 3.3 |
| 3 maglocks at 0.25 A, 24 V | 18 |
| Bolts: 0 idle; 2 at about 1 A for a few seconds | peak 48 |
| Beams, via DC-DC | 2.2 |

Runtime `[calc]`, 24 V 18 Ah (2 × 12 V 18 Ah, 432 Wh nominal) at 50% usable (216 Wh): locks + board 23.5 W ≈ 9 h; after maglock shedding 5.5 W ≈ 39 h. Lead-acid capacity falls with age and temperature; plan on the lower side.

Sensors and server (2 cameras, 2 L1, switch about 50 W at the gate, plus the server) are on a **separate site UPS** sized for each site.

### 12.3 Behaviour

| Trigger | Default | Action |
|---|---|---|
| Mains lost (PWR_GOOD low, or OP_NO_MAINS) | | Event `POWER_LOST`. All functions continue |
| Mains absent for `maglock_shed_s` | 3,600 s | Release maglocks if closed. Bolts hold, as decided. Log `MAGLOCK_SHED` |
| `V < reserve_v` | 23.6 V `[assumed]` | Plate, app and guard opens refused; RTE allowed. Open gate: start a beam-guarded close |
| `V < critical_v` | 22.8 V `[assumed]` | `BatteryCritical`: beam-guarded close, lock; RTE still attempted |
| `V < cutoff_v` | 21.6 V `[assumed]` | Save state; controller sleeps. Bolts spring extended. **Mechanical release is the only egress** |
| Mains back | | Charge; restore maglocks; re-prove locks; send records |

RTE is attempted at every level above cutoff; reserve limits only non-RTE opens. Reserve exists to ensure energy for retract and close. Voltages depend on chemistry and load; tune on bench (B7).

Motion in an outage depends on the operator's own battery. Our controller does not power the motor. If operator battery is low, Status shows it; opens will then fail and be reported.

Fail-secure at open position: if the battery dies while gate is open, the gate stays open. Hence close-on-critical.

## 13. Gate type verification and calibration

### 13.1 Rule

Type, layout and lane mode can be changed only in `Closed` or `ClosedInsecure`. Server and firmware both refuse otherwise (`FAILED_PRECONDITION`). A change makes the config `UNVERIFIED`; **no automatic opens** run until verified. Only RTE and guard open remain, with the prior verified config.

Prerequisite: installer has finished the operator's own limit setup. We do not replace it.

### 13.2 Verification pulse

Preconditions: admin session, gate closed, beam clear, LiDAR says zone empty on both sides, operator status OK, battery above reserve, one-time nonce (expires in 60 s).

1. `Verifying`. Unlock per §7.1.
2. `open()` for `verify_pulse_ms` (default 2,500, including soft start `[assumed]`), then `stop()`.
3. Capture over the window: limit-closed release (per leaf), status, motor current per sensor, LiDAR and camera motion.
4. Score.
5. `close()`, lock. Result, with evidence, goes to the app.

| Signal | Sliding | Single leaf | Dual leaf |
|---|---|---|---|
| LiDAR/camera | Linear motion **in** the gate plane | Arc, one tip moves out of plane | Two opposed arcs |
| Closed limits released | 1 | 1 | 2 (subject to leaf delay) |
| Current | One channel | One channel | Two channels, maybe staggered |
| Status | Slow flash | Slow flash | Slow flash |

**Pass:** motion signature matches and no other signal contradicts it. **Mismatch:** stop, close, lock, alert (as you decided), stay unverified. **Inconclusive:** e.g. LiDAR unavailable: fail with a reason. Do not pass without motion evidence.

The admin must watch the live stream. The app has a stop button throughout. The server sends a keepalive at 1 Hz in this mode; loss → `stop()`, then close.

Gates must be visible to a LiDAR or camera. Mount so at least one sensor sees the leaf. Dual-leaf sweep and direction (in or out) comes from the signature, and is recorded.

The LiDAR/camera classifier is a new server module: `gate_motion_verifier`. Its thresholds need real data. Early firmware can use a rule-based classifier, and fall back to "admin confirms by video" only if the user agrees (Q11).

### 13.3 Calibration

Two complete open/close cycles, with same preconditions. Measures: run time open and close per leaf, leaf delay, peak and mean current curve (binned to 32 points), unlock and lock timings. Pass if the cycles agree within ±10%. Output: `GateCalibration`, stored in NVS and on the server. Sets motor watchdog (run time × 1.5), expected-run-time for the Controller tab, current baseline, and the position estimator.

Position for app: `pos = t_moving / t_full`, corrected at limits. Reported per leaf as 0 to 1000 in telemetry. It is an estimate, never a safety input.

Re-calibrate prompt when run-time drifts more than 15% over 20 cycles.

## 14. Safety

### 14.1 Principles

| Rule | Detail |
|---|---|
| The operator owns entrapment safety | Collision sensing and auto-reverse remain inside Centurion `[S1]`. ADR-008's reason for rejecting direct drive still holds |
| Do not override | Never bridge Safety inputs, never raise collision force to overcome locks, never issue commands to fight a reversal, never disable collision sensing in Positive Close beyond what the manual allows |
| Beam to the operator | Closing beam wires straight to Safety Close. Our tap is **read-only**: a second beam output or a high-impedance isolated tap, tested so that our board failing cannot hold the beam "clear" |
| Vision never clears a close | LiDAR and camera can **hold off** a close (add safety) but cannot permit one the beam refuses |
| Beam during motion | Closing: operator stops and opens `[S1]`; we follow. Opening: Safety Close has no effect `[S1]`. On swing, a sweep-zone beam goes to Safety Open: stops then closes `[S1]` |
| Beam blocked on `Open` | No auto-close; retry as today. Alert after `beam_blocked_alert_s` (300) |
| Interlock | §6.3 is hardware |
| Pre-motion warning | Keep the operator's pre-open delay and pre-flash where sites want it, with a warning lamp `[S1]` |
| Network commands | All motion needs a fresh, non-replayed command; the CommandGuard stays |
| Close while unattended | Auto-close only with a working beam: a beam fault at the operator or our tap disables auto-close |

Manual close over the app: requires beam clear. Operators warn about crushing: the D5 manual says additional safety equipment should be fitted against entrapment `[S1]`. Commissioning includes a measured obstacle test. Reference standards (EN 12453, EN 12445, UL 325 mentioned in the code): applicability in Kenya not verified (Q10).

**Reset behaviour:** ESP32 reset drops `FW_PERMIT` → operator ESTOP. A bolt then falls against a moving gate only in the unlikely case the bolt drop outruns the stop. Accept, test B5.

**OTA:** refused unless `Closed` (or `Open` with auto-close paused off) and battery above reserve. Maglocks drop for the reboot time; bolts hold.

## 15. Motor current sensing

- **Sensor:** split-core, open-loop **DC Hall**, output as analog voltage. Range about ±50 A for a 10 A rated motor `[S1]` with stall and start peaks; check the D5 motor fuse at 30 A `[S1]`, V-Series 15 A per channel `[S3]`. Candidate class: YHDC HSTS016L `[assumed, part not verified]`.
- **Fit:** on one motor lead inside the operator, 1 sensor sliding or single leaf, 2 for dual leaf.
- **Signal:** the motor may be PWM driven. RC filter about 50 Hz to prevent aliasing, sample **200 SPS per channel**, report 50 ms mean and peak. This is for trends, not waveform analysis.
- **ADC:** ESP32-S3 ADC1 is GPIO 1 to 10 and all are used `[code]`; ADC2 conflicts with Wi-Fi. Use a dedicated **external I²C ADS1115-class ADC** for motor current. It delivers 860 SPS **shared**, so 2 channels get about 200 SPS each after mux settling `[assumed]`.
- **Uses:** run-time supervision, calibration curve, wear trend, verification, Controller tab, stall hint. **Not a safety function:** operator owns collision. A `CurrentOverrun` (> 1.5× baseline for 500 ms) is **logged, with an alert, while moving, and takes no action**: a firmware stop could interrupt a collision reversal.
- Sensor-fault detection: Vout near a rail → `CURRENT_SENSOR_FAULT`; features degrade, gate still works.

## 16. Controller telemetry

Telemetry stays 1 Hz with additive lock and operator fields. A new 30 s `ControllerStats` plus an on-demand `RequestStats` carries the bulk for the Controller tab.

| Tab item | Field | Source |
|---|---|---|
| CPU | `cpu_load_pct[2]` | FreeRTOS run-time stats |
| Heap | existing | `esp_get_free_heap_size` |
| Uptime, firmware, link | existing + `link`, `link_speed_mbps`, `ip`, `reconnects`, `reset_reason` | `esp_reset_reason` |
| Relay states | `outputs_mask` | Actuator |
| Cycle counts | `cycles_operator`, `cycles_lock[5]`, `cycles_relay[4]` | NVS |
| Limit/beam history | `FaultEvent`-style `input_edges` to server, stored, not in firmware | `InputEdge` records |
| Run time vs expected | `last_run_ms`, `expected_run_ms` per direction | Calibration |
| Motor current | `motor_mean_ma[2]`, `motor_peak_ma[2]` at end of run; live 2 Hz only when watched | Hall |
| Faults | existing FaultEvent, extended codes | |
| OTA | existing | `gate_ota`; note server RPC stubs are unimplemented |
| Temperature | `core_temp_c` (populate), `board_temp_c` | S3 sensor, I²C sensor |
| Power | `batt_v`, `rail_12v`, `mains_ok`, `on_battery_s` | ADC |

## 17. Proto changes

Rules: additive only, no renumbering, all in `gate.v1`. Update `gate_service.options` for each new firmware-touched string/repeated field, regenerate nanopb, and resize the Control stream `FrameAssembler` buffer: a decode error shows up as a reconnect loop `[code]`.

### 17.1 Enums

```proto
enum GateState { /* existing */
  GATE_STATE_UNLOCKING = 7; GATE_STATE_LOCKING = 8; GATE_STATE_CLOSED_INSECURE = 9;
  GATE_STATE_VERIFYING = 10; GATE_STATE_CALIBRATING = 11; GATE_STATE_PRE_CLOSE = 12;
  GATE_STATE_STOPPED = 13;
}
enum GateType   { GATE_TYPE_UNSPECIFIED=0; GATE_TYPE_SLIDING=1; GATE_TYPE_SINGLE_LEAF=2; GATE_TYPE_DUAL_LEAF=3; }
enum LockLayout { LOCK_LAYOUT_NONE=0; LOCK_LAYOUT_4=1; LOCK_LAYOUT_5=2; }
enum LaneMode   { LANE_MODE_SHARED=0; LANE_MODE_TWO_LANE=1; }
enum GateSide   { GATE_SIDE_UNSPECIFIED=0; GATE_SIDE_ENTRY=1; GATE_SIDE_EXIT=2; }
enum OpenSource { OPEN_SOURCE_UNSPECIFIED=0; OPEN_SOURCE_PLATE=1; OPEN_SOURCE_APP=2;
                  OPEN_SOURCE_RTE=3; OPEN_SOURCE_GUARD=4; OPEN_SOURCE_GUEST=5; OPEN_SOURCE_SCHEDULE=6; }
enum ConfigStatus { CONFIG_UNVERIFIED=0; CONFIG_VERIFIED=1; CONFIG_CALIBRATED=2; CONFIG_MISMATCH=3; }
// CommandKind additions
COMMAND_KIND_CONFIGURE=10; COMMAND_KIND_VERIFY_TYPE=11; COMMAND_KIND_CALIBRATE=12;
COMMAND_KIND_LOCKDOWN_ON=13; COMMAND_KIND_LOCKDOWN_OFF=14; COMMAND_KIND_REQUEST_STATS=15; COMMAND_KIND_CLEAR_FAULT=16;
// VehicleClass unchanged. DenyReason additions
DENY_REASON_PASS_EXPIRED=10; DENY_REASON_PASS_USED=11; DENY_REASON_WRONG_GATE=12;
// AuthVerdict addition
AUTH_VERDICT_REVIEW_MISMATCH=5;  // pass/plate OK, appearance mismatch: guard alert
```

`COMMAND_KIND_CLEAR_FAULT` is new: faults have no remote clear today (`clear_fault()` exists but `main.cpp` has no case for it).

### 17.2 Existing messages

```proto
message Telemetry { /* 1..15 */
  uint32 capabilities = 16;     // bitset, §21. 0 = legacy
  uint32 config_version = 17; ConfigStatus config_status = 18;
  uint32 lock_state_mask = 19;  // 3 bits per lock: driven, feedback, fault
  uint32 op_status = 20;        // decoded enum
  uint32 position_permille_a = 21; uint32 position_permille_b = 22;
  float batt_v = 23; bool mains_ok = 24; uint32 reason = 25;
  uint32 pending_events = 26;
  uint32 limit_extra_mask = 27; // leaf B limits, beam 2
  uint32 hardware_rev = 28;
}
message GateCommand { /* 1..9 */
  OpenSource source = 10; GateSide side = 11; string request_id = 12;
  GateConfig config = 13;       // CONFIGURE
  bytes nonce = 14;             // VERIFY_TYPE, CALIBRATE
  google.protobuf.Timestamp expires_ts = 15; // refused after this
}
message DetectionFrame { /* 1..7 */ GateSide side = 8; string camera_id = 9; VehicleAppearance appearance = 10; }
message AuthDecision   { /* 1..11 */ GateSide side = 12; string pass_id = 13; repeated string alerts = 14;
                         string force_reason = 15; }
message AuthorizeRequest { /* 1..4 */ string force_reason = 5; } // admin-only; §17.5
message OtaManifest    { /* 1..8 */ uint32 hardware_rev = 9; }
message Allowlistentry { /* 1..10 */ repeated string gate_ids = 11; } // empty = all gates
message ControlEnvelope { oneof payload { /* 1..4 */
    OpenRequest open_request = 5; EventBatch events = 6; EventAck events_ack = 7;
    ControllerStats stats = 8; VerifyReport verify = 9; GateCalibration calibration = 10;
    OpenReply open_reply = 11; } }
message DashboardEvent { oneof payload { /* 3..8 */
    OpenRequestRecord open_request = 9; ControllerStats stats = 10; Alert alert = 11; } }
```

`Telemetry.gate_id` remains first-frame identity. Existing `Control` server code ignores unknown payloads, so new envelopes are safe to old servers.

### 17.3 New messages

```proto
message GateConfig {
  uint32 version = 1; GateType type = 2; LockLayout lock_layout = 3; LaneMode lane_mode = 4;
  uint32 leaf_count = 5; uint32 auto_close_s = 6; uint32 offline_close_s = 7;
  uint32 bolt_hold_open_s = 8; uint32 maglock_shed_s = 9;
  reserved 10; LockTimings timings = 11;
}
message LockTimings { uint32 mag_release_ms=1; uint32 bolt_retract_ms=2; uint32 pre_move_ms=3;
  uint32 settle_ms=4; uint32 mag_bond_ms=5; uint32 bolt_extend_ms=6; uint32 retries=7; }
message OpenRequest { string request_id=1; string gate_id=2; GateSide side=3; OpenSource source=4;
  google.protobuf.Timestamp ts=5; bool offline_served=6; }
message OpenReply { string request_id=1; enum Status { UNSPECIFIED=0; GRANTED=1; QUEUED=2; DENIED=3; ALREADY_SERVED=4; }
  Status status=2; uint32 queue_position=3; }
message OpenRequestRecord { OpenRequest request=1; string actor=2; string decision_id=3;
  uint32 queued_ms=4; string preempted_by=5; string outcome=6; }
message ControllerEvent { uint64 seq=1; uint32 boot_id=2; uint64 uptime_ms=3; int64 unix_ms=4;
  uint32 kind=5; OpenSource source=6; GateSide side=7; uint32 result=8;
  GateState state_after=9; bytes request_id=10; } // 16 bytes binary
message EventBatch { repeated ControllerEvent events=1; uint32 dropped=2; }
message EventAck   { uint32 boot_id=1; uint64 last_seq=2; }
message ControllerStats { /* per §16 */ }
message VerifyReport { string command_id=1; GateType observed=2; bool passed=3; string detail=4;
  uint32 limits_released=5; uint32 motor_channels=6; }
message GateCalibration { uint32 open_ms_a=1; uint32 close_ms_a=2; uint32 open_ms_b=3; uint32 close_ms_b=4;
  repeated uint32 current_curve_ma=5; uint32 unlock_ms=6; uint32 lock_ms=7; }
message Alert { string ref=1; string kind=2; FaultSeverity severity=3; string gate_id=4;
  string plate=5; string message=6; string image_uri=7; }
message VehicleAppearance { string make=1; string model=2; uint32 year_from=3; uint32 year_to=4;
  string colour=5; float conf=6; string embedding_ref=7; }
```

`VerifyReport` is merged on the server with the LiDAR and camera verdict: final pass is the server's, and firmware waits for it before leaving `Verifying`.

### 17.4 New RPCs

```proto
service AdminService { /* existing */
  rpc UpsertBlocklist (UpsertBlocklistRequest) returns (UpsertAllowlistResponse);
  rpc ListBlocklist   (ListBlocklistRequest)   returns (ListBlocklistResponse);
  rpc DeleteBlocklist (DeleteAllowlistRequest) returns (UpsertAllowlistResponse);
  rpc SetVehicleAccess(SetVehicleAccessRequest) returns (VehicleAccess); // app dropdown
  rpc CreateGuestPass (GuestPass) returns (GuestPass);
  rpc ListGuestPasses (ListGuestPassesRequest) returns (ListGuestPassesResponse);
  rpc RevokeGuestPass (RevokeGuestPassRequest) returns (GuestPass);
  rpc GetGateConfig   (GetGateConfigRequest) returns (GateConfigRecord);
  rpc SetGateConfig   (SetGateConfigRequest) returns (GateConfigRecord);
  rpc VerifyGateType  (GateOpRequest) returns (GateOpStatus);
  rpc CalibrateGate   (GateOpRequest) returns (GateOpStatus);
  rpc SetLaneConfig   (SetLaneConfigRequest) returns (GateConfigRecord);
}
message BlocklistEntry { string plate_text=1; string reason=2; string added_by=3;
  google.protobuf.Timestamp added_ts=4; }
message GuestPass { string pass_id=1; string site_id=2; string plate_text=3; string guest_name=4;
  string host=5; repeated string gate_ids=6; enum Kind { UNSPECIFIED=0; SINGLE_VISIT=1; UNTIL=2; }
  Kind kind=7; google.protobuf.Timestamp valid_from=8; google.protobuf.Timestamp valid_until=9;
  uint32 entries_used=10; string status=11; string expected_make=12; string expected_model=13;
  string expected_colour=14; string created_by=15; }
```

All Admin RPCs check `Role::kDashboard`. Role check for admin-only actions stays in the dashboard backend, per the app rule (`admin` only for block, config, passes).

### 17.5 Fusion order and hooks

Revised `decide_at`:

1. Force-close. 2. No plate. 3 to 4. Confidence calculation.
5. **Blocklist:** a low-confidence blocklist hit raises an alert (soft check). A confident hit is DENIED unless step 5a applies. **Sticky force-open and guard override never pass it.**
   5a. **Admin force:** `AuthorizeRequest.force_reason` non-empty and `actor` verified as `admin` by the dashboard backend → AUTHORIZED, `OPEN_SOURCE_GUARD`, raises Critical alert `BLOCKLIST_FORCED`. Empty reason → `INVALID_ARGUMENT`.
6. Force-open. 7. Confidence floor. 8. Guard override. 9. Allowlist. 10. **Guest pass**. 11. Time and class rules. 12. `appearance_check()` hook.

**Roles and audit:** the backend accepts `force_reason` only from an authenticated `admin`, rejects it from `operator` and `viewer` with 403, and `gate-vision` can never set it. The reason, admin username and decision id go in `decisions.force_reason`, `decisions.actor` and `alerts`. Fusion trusts `force_reason` only over the `Role::kDashboard` channel.

Guest-pass logic:
- Matches plate, gate, validity → AUTHORIZED, `OPEN_SOURCE_GUEST`, guard alert `GUEST_ARRIVED`.
- Single visit: entries_used increments on **confirmed open** (arbiter ack), not on decision, so a failed open does not burn a pass. Status `USED` then. A one-visit guest must still exit: exit side allows the same pass until `valid_until` or 24 h.
- Pass with no plate (unknown car) is out of scope.

`appearance_check()` is a no-op until MMYRC. Later, if `expected_make/model` is set and `DetectionFrame.appearance.conf` is high and disagrees → default keeps AUTHORIZED and raises alert `MAKE_MODEL_MISMATCH` with image; `AUTH_VERDICT_REVIEW_MISMATCH` is available for sites wanting a guard hold. Your decision says alert, so default is alert only.

### 17.6 Fleet interfaces reserved (build after step 7, §26)

Separate file `shared/proto/fleet.proto`, package `gate.fleet.v1`. It **must not import** `gate_service.proto` command types.

| Reserved | Shape |
|---|---|
| Outbound agent | `FleetAgentService.Connect(stream AgentUp) returns (stream AgentDown)`. Site dials out; no inbound port |
| Health schema | `SiteHealth`: server version, uptime, GPU, disk, gate count by state, online counts, fault counts by code, firmware versions. Only anonymous aggregates: **no plate, image, video, name or user field** |
| Read-only role | `Role::kFleetReadOnly`, used by relayed view sessions. A static allowlist of read methods. Every session writes a local `fleet_view_sessions` row |
| Update manifest | `UpdateManifest`: component (server, firmware, model), version, sha256, ed25519 signature, `hardware_rev`, min version. Applied only by a site admin in its window; verified at the site, with rollback |

`AgentDown` is limited to `ViewRequest` and `UpdateAvailable`. **No message may carry a `GateCommand`, `OpenRequest`, `AuthorizeRequest`, `GateConfig` or any write.** `kFleetReadOnly` is denied on `IssueCommand`, `Authorize`, all Admin RPC writes and `Control`. CI test: every write RPC refuses the role, and a descriptor check fails the build if `fleet.proto` gains such a type.

## 18. Deployment profiles

Every site has **one local GPU server (RTX 20 series or newer)** that makes all gate decisions and works with no internet. The app reaches it on the LAN; remote app access is a site networking matter. System-admin oversight is §26.

| | Homestead | Estate |
|---|---|---|
| Gates | 1 | Several |
| Server | Small PC + RTX 20 or newer | Rack or tower GPU |
| Users | Family, phones | Guards + residents |
| Link gate↔server | LAN | LAN or fibre |
| LiDAR | USB if within reach; else UDP adapter | UDP adapter |
| Power | 24 V battery at gate, UPS for server and sensors | Same, plus generator where present |

**Homestead cost:** a GPU PC and its UPS are the largest items and need costing, but the choice of architecture is settled.

### One server, many gates at a site

- **Identity:** `gates.site_id` stays, set by install. Gates are matched by certificate CN `gate-field-<id>`; reject unregistered gates. Replaces `default_site_id_`, and leaves the fleet layer a stable site key.
- **App stations:** each station is one site server, with its own sign-in.
- **Scale:** one Control writer thread per gate is fine for a site `[code]`.
- **GPU:** one `gate-vision` per side per gate. LiDAR presence wakes the camera pipeline, so idle gates use little GPU. Target 8 sides on an 8 GB RTX 20-class card `[assumed]`, to be measured.
- **Decode:** use NVDEC for main streams where supported.
- **Failure isolation:** a dead gate affects only itself. A server outage stops entry at every gate of that site; offline egress and keyed override stay. Estates need a server UPS and a spare plan.

## 19. Database migrations

### 19.1 Auth DB (`allowlist_store`, `kSchemaVersion` 1 → 2)

Additive, one transaction, backup first.

```sql
ALTER TABLE blocklist ADD COLUMN added_by TEXT NOT NULL DEFAULT '';
CREATE TABLE allowlist_gates (site_id TEXT, plate_text TEXT, gate_id TEXT,
  PRIMARY KEY(site_id, plate_text, gate_id),
  FOREIGN KEY(site_id, plate_text) REFERENCES allowlist(site_id, plate_text) ON DELETE CASCADE);
CREATE TABLE guest_passes (
  pass_id TEXT PRIMARY KEY, site_id TEXT NOT NULL, plate_text TEXT NOT NULL,
  guest_name TEXT NOT NULL DEFAULT '', host TEXT NOT NULL DEFAULT '',
  kind TEXT NOT NULL CHECK (kind IN ('single','until')),
  valid_from INTEGER NOT NULL, valid_until INTEGER NOT NULL,
  entries_used INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','used','expired','revoked')),
  expected_make TEXT, expected_model TEXT, expected_colour TEXT,
  created_by TEXT NOT NULL, created_ts INTEGER NOT NULL);
CREATE INDEX guest_passes_lookup ON guest_passes(site_id, plate_text, status);
CREATE TABLE guest_pass_gates (pass_id TEXT REFERENCES guest_passes ON DELETE CASCADE,
  gate_id TEXT, PRIMARY KEY(pass_id, gate_id));
```

`allowlist_store.cpp`: `migrate()` currently throws on a newer DB and rewrites `schema_version`. Add an explicit v1→v2 step. Empty `allowlist_gates` means all gates.

### 19.2 History DB (`history_store`): introduce `PRAGMA user_version`

Existing DB treated as version 0 if tables exist. v1:

```sql
ALTER TABLE gates ADD COLUMN site_id TEXT NOT NULL DEFAULT '';
ALTER TABLE gates ADD COLUMN gate_type TEXT NOT NULL DEFAULT 'sliding';
ALTER TABLE gates ADD COLUMN lock_layout TEXT NOT NULL DEFAULT 'none';
ALTER TABLE gates ADD COLUMN lane_mode TEXT NOT NULL DEFAULT 'shared';
ALTER TABLE gates ADD COLUMN config_version INTEGER NOT NULL DEFAULT 0;
ALTER TABLE gates ADD COLUMN config_status TEXT NOT NULL DEFAULT 'unverified';
ALTER TABLE decisions ADD COLUMN side TEXT NOT NULL DEFAULT '';
ALTER TABLE commands ADD COLUMN source TEXT NOT NULL DEFAULT '';
ALTER TABLE commands ADD COLUMN side TEXT NOT NULL DEFAULT '';
ALTER TABLE commands ADD COLUMN request_id TEXT NOT NULL DEFAULT '';
ALTER TABLE decisions ADD COLUMN force_reason TEXT NOT NULL DEFAULT '';
CREATE TABLE fleet_view_sessions (id INTEGER PRIMARY KEY, start_ms INTEGER, end_ms INTEGER, viewer TEXT, scope TEXT);
CREATE TABLE open_requests (request_id TEXT PRIMARY KEY, ts_ms INTEGER NOT NULL,
  gate_id TEXT NOT NULL, side TEXT NOT NULL, source TEXT NOT NULL, actor TEXT NOT NULL DEFAULT '',
  decision_id TEXT, queued_ms INTEGER NOT NULL DEFAULT 0, preempted_by TEXT,
  outcome TEXT NOT NULL, offline INTEGER NOT NULL DEFAULT 0);
CREATE TABLE controller_events (gate_id TEXT, boot_id INTEGER, seq INTEGER, ts_ms INTEGER,
  ts_estimated INTEGER NOT NULL DEFAULT 0, kind TEXT, payload TEXT,
  PRIMARY KEY (gate_id, boot_id, seq));
CREATE TABLE gate_configs (gate_id TEXT, version INTEGER, config TEXT NOT NULL, status TEXT,
  created_by TEXT, created_ms INTEGER, PRIMARY KEY (gate_id, version));
CREATE TABLE gate_calibrations (gate_id TEXT, config_version INTEGER, ts_ms INTEGER,
  data TEXT NOT NULL, PRIMARY KEY (gate_id, config_version, ts_ms));
CREATE TABLE controller_stats (gate_id TEXT, ts_ms INTEGER, data TEXT NOT NULL,
  PRIMARY KEY (gate_id, ts_ms));   -- 30-day retention job
CREATE TABLE input_edges (gate_id TEXT, ts_ms INTEGER, input TEXT, level INTEGER);
CREATE INDEX idx_edges ON input_edges(gate_id, ts_ms);   -- 30-day retention job
```

Existing `gates.direction` becomes redundant for two-sided gates; keep it. Migrating existing rows: `site_id` set to the running default site. `lock_layout='none'` keeps old gates working.

## 20. I/O budget for board rev 3

### 20.1 Pins

ESP32-S3-WROOM-1 N16R8 usable pins `[code: docs/hardware/10]`: avoid strapping 0, 3, 45, 46; USB 19, 20; octal PSRAM/flash. Available: 1, 2, 4 to 18, 21, 38 to 44, 47, 48. UART0 43/44 stays. Rev 3 gives up the external JTAG header (39 to 42) and **debugs over native USB-JTAG**.

**Reset state:** a 10 kΩ external pulldown on each of **GPIO 1 (FW_PERMIT), 4 (FRX), 5 (TRG), 39, 40, 41 (LOCK_EN3 to 5)** and on 17, 18 (LOCK_EN1, 2), since internal state is not defined through reset. All are active-high, so reset means permit off, no pulse, locks off.

**Direct GPIO:** only what is time-critical or safety related. The rest goes on I²C.

| Function | Count | Pin | Notes |
|---|---|---|---|
| W5500 SPI, INT, RST | 6 | 8 to 13 | Unchanged |
| RS-485 | 2 | 21, 14 | Auto-direction transceiver, freeing IO38. Check its turnaround at the actual baud rate; RS-485 is for field peripherals with no protocol yet |
| WDT_KICK | 1 | 38 | External watchdog, §6.4 |
| OP_FRX, OP_TRG | 2 | 4, 5 | Same as rev 2 relays |
| FW_PERMIT | 1 | 1 | Goes into the hardware AND |
| Limit open A, closed A | 2 | 6, 7 | Keep |
| Beam close tap | 1 | 15 | Keep |
| Status decode | 1 | 2 | Opto |
| RTE | 1 | 16 | Moved from LED |
| LOCK_EN 1 to 5 | 5 | 17, 18, 39, 40, 41 | PWM capable (LEDC); hold PWM |
| I²C SDA, SCL | 2 | 47, 48 | |
| EXPANDER INT | 1 | 42 | |
| **Total direct** | **25** | | **Fully used; 0 spare** on direct pins |

**TCA9555-class I²C expander (16 I/O, ×3):**

| Function | Count |
|---|---|
| Bolt RETRACTED ×2, EXTENDED ×2 | 4 |
| Maglock bond ×3 | 3 |
| Limit open B, closed B | 2 |
| Beam 2 tap | 1 |
| Tamper (box lid) | 1 |
| PWR_GOOD, charger fault | 2 |
| Driver fault ×5 (or shared) | 5 |
| Permit readback | 1 |
| Status LEDs R/G/Y | 3 |
| OP_PED, spare relay | 2 |
| Lane lamps R/G | 2 |
| Key sense inside, outside | 2 |
| Maglock-cut sense | 1 |
| WDT_TRIPPED | 1 |
| RTE line fault latch | 1 |
| Spare | 17 |
| **Total** | **48 with 17 spare** |

The hardware AND reads the bolt and maglock signals in hardware, ahead of the expander, so an I²C failure cannot cause motion. **No expander may carry safety logic:** expanders only report. I²C bus-stuck recovery: on timeout, clock SCL 9 times, issue STOP, reset the expanders by their reset line; persistent failure raises `IO_BUS_FAULT`, treats locks as unproven and blocks motion by withholding `FW_PERMIT`. RTE stays on direct GPIO.

**ADS1115-class ADC (4 ch, ×2):** ADC 1: motor current ×2 only. ADC 2: battery V, 12 V rail, driver current sum, RTE supervision (end-of-line). ADC 1 has 2 channels spare but they stay unused to keep the motor sample rate.

**Other I²C:** RTC with backup cell, board temp sensor, optional FRAM.

Isolation: all field inputs keep series resistor, clamp and filter, as J8; RTE, limits and lock feedback wetted at 12 V through opto or protected inputs. Long runs to locks and sensors carry lightning and EMC risk; TVS on every field pin.

### 20.2 Outputs and power

| Item | Spec |
|---|---|
| Lock drivers ×5 | Low-side MOSFET rated for 28 V rail plus transients (40 V min `[assumed]`), 2 A continuous, gated by `WDT_OK`, flyback + TVS, per-channel fuse, fault sense; **hard off at reset** by gate pulldown |
| Maglock | Suppressor to limit release spike; check release time |
| Operator contacts | Relays or optos for FRX, TRG, PED. Relay contact rated for cycles: FRX and TRG are pulses (lifespan far better than holding) |
| K_PERMIT | Safety-role relay; force-guided contact if a suitable part exists; NO to LCK/STP; contact readback |
| Maglock feed | Hard-wired through the two NC contacts of §6.4, on terminals, no firmware path around them |
| 12 V DC-DC | From BATT_BUS_24, ≥ 2 A; fused |
| Beam supply | Fused 12 V from the DC-DC |
| Cable entry | Input range to 30 V, with TVS to suit. Rev 2 J1 (15 V, 2 A) is unusable. Size for 6 A peak at 24 V |
| Mains | Separate fused section, slotting or clear creepage, earth, no user access. **Prefer mains-side unit outside the board**, in its own compartment |
| Board size | 140 × 120 mm with mains: check IRM body size and clearance early; enclosure follows |

## 21. Backward compatibility

| Concern | Handling |
|---|---|
| Deployed rev 2 firmware | Sends `capabilities=0`. Server treats it as `LEGACY`: no lock states, no config commands, plain OPEN/CLOSE. It ignores `source`, `side`, `request_id` (proto3 unknown fields). Arbiter still applies; RTE simply absent |
| New enum values to old firmware | CommandKind unknown → existing `default` returns error ack. Server never sends them when the capability is missing |
| New firmware, old server | New envelopes ignored by old server; events retained until ack. Telemetry unaffected. Upgrade the server first in any case |
| `GateState` new values | `gate_state_name()` falls back to a numeric form for unknown enums on old dashboards `[code]`. Old MAUI app shows an unmapped string: must not crash. Check the app's mapping for every new value, specifically `GATE_STATE_STOPPED` (not checked yet) |
| App | The dashboard JSON adds fields only |
| Firmware rev 2 on v2 code | `ContactorActuator`, `lock_layout=NONE`. State machine behaviour identical; all existing tests stay |
| Rev 3 image on rev 2 hardware | `OtaManifest.hardware_rev`; firmware checks before writing. Note: **old firmware ignores this field**, so never publish a rev 3 manifest to a channel old devices read; use separate channels |
| Hardware detection | Board ID by strap resistor on the ADC or expander, reported in `hardware_rev`. Refuse to boot a rev 3 image on the wrong board |
| Config | NVS config with its own version; defaults to `UNVERIFIED`, auto-open off; manual RTE works |
| Capabilities bits | `1 locks`, `2 operator_profile`, `4 event_log`, `8 verify`, `16 stats`, `32 current` |
| Rollout order | Migrations → server → dashboard → app → firmware |
| Rollback | Migrations additive; older server ignores new tables. Auth DB refuses a newer schema version (`current > kSchemaVersion` throws): **backup before upgrade**, restoring is the rollback |

## 22. Simulator changes

`simulation/virtual_gate` should reuse the real state machine, as it does now.

- Move sequencing into a **host-testable pure `LockSequencer`** in `gate_state_machine` or a sibling, so firmware and simulator share it; same for `RteArbiter` (local authority), `StatusDecoder` and `EventRing`.
- `VirtualOperator`: FRX/TRG/ESTOP behaviour from §5.1, Status patterns, Standard mode, operator-owned reversal on beam, collision, low battery. Types: sliding, single, dual (with leaf delay).
- `VirtualLock`: configurable release, jam, sticking feedback, broken wire.
- Interlock model: an independent AND that blocks motion regardless of the firmware model, so a test can **inject a firmware bug**.
- Power: battery model, mains loss, thresholds.
- Current trace: synthetic curve, overrun, sensor fault.
- Network: link drop for RTE and store-and-forward; `mock_server_main` gains arbiter-compatible handling and EventAck.
- Two sides: `ScenarioSource` gets `side`; simultaneous events.
- Console: `rte`, `jam b1`, `drop-power`, `drop-link`, `set-type`, `verify`, `calibrate`, `force`.
- Gate motion traces for `gate_motion_verifier`, including deliberate wrong-type config.

## 23. Test plan

### 23.1 Host and CI

| Suite (`tests/`) | Coverage |
|---|---|
| `state_machine` | Full table §8.4 for both profiles; every state × every event: **no path to Drive actions without LocksReleased** |
| `rte_matrix` (new) | RTE × every state × battery level × link state × lockdown: outcome per §7.3, §8.4; never ignored silently |
| `late_grant` (new) | Ghost late grant after local open and close; `expires_ts`; `QUEUED` reply; served-ring reboot persistence |
| `lockdown` (new) | Latch persists after RTE and across reboot; every state in `LockdownOn` table |
| `reversal` (new) | Collision and beam reversal: no direction fault, no stop, no command from firmware; overrun logs only |
| `battery_close` (new) | BatteryCritical with beam blocked never closes; retry when clear |
| `event_ring` (new) | Torn writes at each byte, recovery, boot_id loss, 945 capacity |
| `fleet_guard` (new) | Read-only role refused on all write RPCs; descriptor check on `fleet.proto` |
| `lock_sequencer` (new) | §7 timings, retry, partial fail, feedback faults, 4/5 layouts, property test: any random feedback sequence never yields permit with an unretracted bolt |
| `status_decoder` (new) | All patterns, jitter ±20%, transitions |
| `rpc`, `proto` | Round trips, nanopb sizes, old-to-new and new-to-old decode against a frozen v1 descriptor |
| `fusion` | New order, block before force-open, soft block, guest pass cases, hook |
| `arbiter` (new) | All §10 rules, timeouts, starvation, two-lane, multi-gate |
| `auth`, `dash` | v1→v2, v0→v1 migrations on a copied real DB; idempotent event ingest; multi-site isolation |
| `dashboard_api` | Admin-only checks, gate must be closed for config |
| `simulation` | End-to-end scenarios below |
| `firmware_integration` | Event ring power-cut safety, config NVS |

### 23.2 Simulator scenarios (must pass before hardware)

RTE offline; RTE with slow server; ghost late grant; key override inside and outside; hung firmware model with the interlock and maglock cut; collision reversal not interrupted; lockdown persistence after RTE; beam-guarded battery close; server dies mid-cycle; jam on each lock; bolt drops mid-travel; beam during closing without double-reverse; wrong gate type; verify with a person detected; outage through every battery stage; 1,000-record ring overflow; exit and entry simultaneous; 24 h soak with random events.

### 23.3 Bench (real operator required)

| # | Test | Proves |
|---|---|---|
| B1 | Capture Status waveforms | Decoder timing |
| B2 | Fault patterns during motion | Decoder priority |
| B3 | LCK ESTOP: stop, release, re-start; TRG direction after stop; no self-start | §5.3, §6.3 |
| B4 | Real lock timings, at 21 to 28 V, hot and cold | §7 |
| B5 | Reset during motion | Interlock, bolt-gate contact |
| B6 | Hall sensor accuracy with real operator PWM | §15 |
| B7 | Battery run-down in real load | §12 |
| B8 | Beam tap: board off or dead cannot hide a beam break | §14 |
| B9 | **Remove firmware**: operator cannot drive into bolts | §6.3 |
| B10 | **Full** egress route, timed, no power, by a non-engineer: inside release, bolt releases, **and the operator's key release**, then moving the gate by hand | F1 |
| B11 | 10,000 lock cycles, dust and water spray | Reliability |
| B12 | Surge and ESD on long field wires | §20 |
| B13 | ESTOP (LCK open) during contact and during reversal, with a test obstacle: does it leave a person pinned? Measure residual force and release method | §5.3, §6.3 |
| B14 | Maglock cut with **hung firmware** and powered drivers | §6.4 |
| B15 | Key override inside and outside: gate openable, logged `KEY_OVERRIDE`, not `GATE_FORCED`, no drive while active | §6.4 |
| B16 | External watchdog: halt pump task, outputs go safe, no spurious trips over 72 h | §6.4 |
| B17 | RTE matrix on real hardware: lockdown, low battery, each fault class, local reset | §7.3 |
| B18 | Battery close with beam blocked and clear | §12 |
| B19 | Collision reversal not interrupted by firmware, while a current overrun occurs | §5.3 |

### 23.4 Site acceptance

Obstacle test per operator manual; verify and calibrate; ALPR range day, night, rain at chosen focal length; two-side arbitration with real cars; outage drill; emergency-egress drill with household.

### 23.5 Release gates

No field deployment before B3, B5, B8, B9, B10, B13, B14, B15, B16, B17, B18, B19 pass. If B13 shows pinning, the interlock design is reviewed before deployment.

**Board freeze:** step 5 must not freeze rev 3 until B3, B13 and Q4 are closed, since each can change the interlock or lock interfaces.

### 23.6 Tracked backlog

| Item | Owner | Decision point |
|---|---|---|
| Enable `GateCommand` signature, or record why not | User, architect | **Before first field use** |
| Check app mapping for new `GateState` values | App, step 6 | Before firmware rollout |
| RS-485 auto-direction turnaround | Board, step 5 | Before board freeze |

## 24. Open questions

| # | Question | Owner | Needed by | Default |
|---|---|---|---|---|
| Q3 | Dual leaf: add ground anchor? | User, installer | Board | No |
| Q4 | Bolt and maglock parts with two-state feedback, duty rating, keeper sense, available in Kenya | Research | Board | Unknown: the design depends on it |
| Q6 | Warranty: Hall sensor in operator, Status tap, LCK use | Dealer | Install | Assume OK until told |
| Q7 | L1 UDP adapter availability | Research | Step 4 | Adapter |
| Q10 | Legal: gate safety standards, data protection | User, adviser | Field | Not reviewed |
| Q11 | May an admin's video confirmation replace missing motion evidence? | User | Step 4 | No |
| Q13 | Maglock holding force; does it even add useful strength on a fabricated Kenyan gate frame? | Installer | Board | Keep |
| Q14 | Do customers keep operator remotes? §6.3 means they do nothing locked. Integrate via an RF receiver into RTE-class input? | User | Board | Remove remotes |

Closed by user decision: lock voltage (24 V), outage entry (keyed override), RTE beats lockdown, admin-only blocklist force, unknown exit by button or app, local server per site. Numbers Q1, Q2, Q5, Q8, Q9, Q12 are retired.

## 25. Sources

Accessed 2026-10-31. Prices are reseller or vendor pages, undated unless noted.

- `[S1]` D5-Evo installation manual, Centurion, 2024: https://www.centsys.co.za/pdf/prod/d5-evo/1010.D.01.0001%20D5-Evo%20Manual%2009052024_AP_Web.pdf (spec table, §10.4, §16 terminals, §17.2 status)
- `[S2]` V-Series electrical guide, 2022: https://www.centsys.com.au/wp-content/uploads/2022/06/1228.D.01.0012_V-Series_Elec_Guide_19052022_AP_Web.pdf
- `[S3]` Same guide, controller scope, modes and fuses (§22, §25.3); also https://www.centsys.com.au/wp-content/uploads/2022/06/1228.D.01.0010-V-Series-User-guide-20052022_AP_Web.pdf
- `[S4]` Vector2 installation manual (Leaf Delay, Positive Close): https://www.centsys.co.za/upload/0_07_A_0012%20VECTOR%202%20Installation%20Manual-16052014-NG-web.pdf
- `[S5]` DS-2CD2643G2-IZS datasheet: https://download.discomp.cz/hikvision/datasheets/DS-2CD2643G2-IZS.pdf
- `[S6]` Kenyan Hikvision sellers: https://solwave.co.ke/brand/hikvision/ , https://rapidtech.co.ke/hikvision-cctv-surveillance-cameras/
- `[S7]` Secudepo D5 Evo: https://secudepo.co.ke/product/ccenturion-d5-evo-sliding-gate-motor/
- `[S8]` Glantix: https://glantix.co.ke/Brands/centurion
- `[S9]` Techyshop (KSh 75,000 ex-VAT): https://www.techyshop.co.ke/centurion-d5-evo-domestic-light-industrial-sliding-gate-motor/ ; Tungsten: https://tungsten.co.ke/product/centurion-d5-evo-d10-evo-gate-motor/
- `[S10]` Premier Automatic Gates, price guide: https://automaticgates.co.ke/automatic-gate-motor-prices-in-kenya-how-much-should-you-budget/
- `[S11]` Jiji Vantage: https://jiji.co.ke/nairobi-central/safety-equipment/centurion-vantage-swing-gate-smart-operators-uC7Oz3toNHxHbm4DeBxOsa2O.html
- `[S12]` Jiji linear swing: https://jiji.co.ke/nairobi-central/doors/linear-swing-gate-motor-centurion-BQGoCRYAvEhLCZskl4oJkXSY.html
- `[S13]` FAAC 740: https://www.electricgatemotors.co.uk/product/faac-740-sliding-gate-motor/
- `[S14]` FAAC C721 listing: https://powerbizt.netpeople.hu/en/products/faac-109321
- `[S15]` Polvision spec quote: https://www.polvision.eu/en_US/p/DS-2CD2643G2-IZS2.8-12mm/5127
- `[S16]` Jiji Hikvision: https://jiji.co.ke/387-cctv-cameras/hikvision
- `[S17]` German price: https://industry-electronics.com/hikvision/ds-2cd2643g2-izs-netzwerkkamer-lieske_263527.htm
- `[S18]` Estonia price: https://www.ox.ee/en/product/2393142
- `[S19]` Hikvision Store Kenya: https://hikvisionstorekenya.co.ke/
- `[S20]` DS-2CD2A46G2T/P-IZSY ANPR: https://www.montersi.pl/instrukcje/DS_2CD2A46G2T_P_IZSY_ANPR.pdf
- `[S21]` DS-2CD3646G2T/P-IZSY datasheet: https://assets.hikvision.com/prd/public/all/doc/sm000061386/DS-2CD3646G2T_P-IZSY_Datasheet_20231024.pdf

**Not verified:** FAAC/BFT terminals and Kenyan distribution; authorised Centurion distributor status; D10 family electrical details; V-Series Status table; Hikvision channel paths on a real unit; all `[assumed]` thresholds.

## 26. Fleet oversight (user decisions, 2026-10-31)

Topology: one local GPU server per site (RTX 20 series or newer) makes every
gate decision and keeps working without internet. One remote server on a
Google Cloud VM, for the system admin, does no inference. Each site connects
outbound over an encrypted link; sites need no open ports or fixed IP.

Rules:
1. The remote server cannot command gates.
2. Plates, images, video and personal data stay on site. Only health data and
   anonymous statistics are sent up and stored remotely.
3. The system admin can view each site remotely, read-only: no edit path
   exists. Viewed data is relayed on demand and not stored remotely. Every
   remote view session is logged at the site.
4. Updates (server, firmware, models) are published only by the system admin.
   A site admin can apply only those updates, in its own safe window, with
   rollback. Signatures are verified at the site. Controllers keep verifying
   signed firmware.

Build after step 7. Interface reserved in §17.6: outbound agent, health schema,
read-only API role, update manifest.
