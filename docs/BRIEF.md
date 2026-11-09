# VibeGuard: Project Brief

Taken from the shared Claude conversation of 30 Sep 2026. The files attached in that chat (MEMORY.md, architecture diagram, sample report, cover-page photo, generated PDFs) are hidden in the shared link, so this brief covers only what the visible messages say.

## Context
- Course: **21CSE253T, Internet of Things**, SRM Institute of Science and Technology, Dept. of Computing Technologies
- Faculty: **Dr. Arulalan V (103059)**
- Full title: **VibeGuard: Predictive Maintenance System for Rotating Machines Using Vibration Analysis and Edge Processing**
- Team:
  | Member | Reg. No. |
  |---|---|
  | Thanvik Sourya Gunupati | RA2311003010972 |
  | Yashwanth Lakkakula | RA2311003010969 |
  | G. Karthikeyan Reddy | RA2311003010725 |
- Targets: rotating machines such as motors, pumps and fans. It needs no internet, cloud or subscription.

## The idea
A **₹1,140 edge device (ESP32 + MPU6050)** that listens to a motor's vibration, learns what "healthy" looks like in **20 seconds**, then diagnoses **which fault** is developing instead of only raising an alarm.

## How it works
1. **Sensing:** MPU6050 samples **Z-axis acceleration at 500 Hz**.
2. **DSP on the ESP32:** **256-point FFT** with a **Hann window** gives 1.95 Hz per bin, with Nyquist at 250 Hz.
3. **Band energy maps to a fault:**
   | Band | Fault |
   |---|---|
   | 1× shaft frequency | Rotor imbalance |
   | 2×–3× harmonics | Mechanical looseness |
   | 100–240 Hz broadband | Bearing wear |
4. **Baseline learning:** a 20 s calibration on a healthy motor. **Ratios to the baseline** drive the fault classification and a **health %**.
5. **Dashboard:** served from the ESP32's own **WiFi access point at 192.168.4.1**. It shows the health % and the band that triggered.
6. **Advisory engine** (on the diagram): repair cost vs. failure cost.

## Pitch
- About ₹40,000+ for commercial monitoring vs. **₹1,140** for VibeGuard
- About ₹150 for a bearing fix vs. **₹1,800+** for a motor replacement

## Deliverables listed as ready
- Firmware
- 8-slide deck
- Architecture diagram

## Built in the chat: Design Representation Document (PDF)
- **Format:** Designs Act representation set, 8 pages: a cover plus 7 sheets (Perspective, Front, Rear, Top, Bottom, Left Side, Right Side)
- **Article:** "Machine Vibration Monitoring Device", **Locarno class 10-05** (measuring instruments; the subclass still needs confirming)
- **Enclosure design**, chosen by the user:
  - A compact, rounded sensor puck, about **70 × 56 × 30 mm**
  - Attaches with a **clamp or strap**: a curved saddle underneath fits a motor about 44 mm across, with a slotted strap lug on each side
  - **Fully sealed** except for an amber **status LED slot** on top, so all information goes through the dashboard
  - Grey body, teal lugs, amber LED (colour isn't claimed)
- **Cover page:** SRM header, Dept. of Computing Technologies, "INTERNET OF THINGS", course code, faculty, the document title, VibeGuard name and subtitle, perspective render, a description and the team table
- Names were removed from every sheet. Signature lines appear only on the last sheet.
- Dated 30 Sep 2026
- There are two versions in ~/Downloads:
  - **VibeGuard Design Document.pdf** (8 pages): the final one. It has the cover, then sheets 1–7 with a header table (Designs Act / Project / Article / Locarno / Sheet No.), the standard disclaimers, and signatures plus "To The Controller of Designs, The Patent Office, Kolkata" only on the last sheet.
  - **VibeGuard Design Representation Sheets.pdf** (7 pages): the earlier version. It has no cover, and the applicant names and signature block appear on every sheet.
- What the renders show: a pale grey rounded-rectangle body with a dark seam line around its middle, an amber slot-shaped LED window centred on top, and a concave arched saddle on the underside (seen in the side view). A teal rectangular lug with a through-slot for the strap sticks out at each end.

## Open issues raised in the chat

All five are addressed by the firmware rebuilt on 5 Nov 2026. See [ALGORITHM.md](ALGORITHM.md#answers-to-the-questions-flagged-before-the-presentation) and [HARDWARE.md](HARDWARE.md#mounting-notes).

- **Diagram vs. firmware:** the diagram promises cost advice from the advisory engine. Check that the firmware actually shows it.
- **Bearing band:** 100–240 Hz sits right below the 250 Hz Nyquist limit. Be ready to justify it, or raise the sample rate.
- **Finding the 1× shaft frequency:** confirm whether the firmware uses a peak search or a fixed range. This is the likeliest examiner question.
- **Hardcoded thresholds** are tuned on one motor. Say so upfront.
- **Demo risk:** mount the sensor with hot glue, not tape.
- **IP:** a design registration protects only the shape. The FFT method would need a patent. File before any public demo.
- The physical build is still a breadboard with no enclosure yet.
