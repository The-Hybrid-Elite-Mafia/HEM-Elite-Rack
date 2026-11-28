# Staging engines — offline CPU benchmark

One core of an Intel Xeon @ 2.10 GHz (cloud Linux VM), g++ -O2 -march=x86-64-v3, 48 kHz, 512-sample blocks,
10 s of noise + tones, best of 3 runs. Re-run after the idle fix (NoteForge, SubArchitect, VoiceCircuit,
ChoirForge, ThroatShift skip their pitch analysis while they are dead flat). **def** = factory defaults (dead flat). **act** = every smooth control
at 70 % of its range and every on/off switch on (bypass, listen, solo, freeze, mute, monitor, resets left off).
%RT = processing time / audio time (100 % = one full core). Not measured on Steven's PC; a desktop CPU is usually faster.

```
ENGINE                     def ms/blk    def %RT act ms/blk    act %RT
DYNAMIC EQ 8                   0.0261      0.24%     0.0587      0.55%
V.P.R.A.                       0.0130      0.12%     0.0610      0.57%
SUB-BASS & 808 ARCHITECT       0.0071      0.07%     0.2751      2.58%
VOICECIRCUIT                   0.0081      0.08%     0.5267      4.94%
NOTEFORGE                      0.0047      0.04%     0.4236      3.97%
THROATSHIFT                    0.0036      0.03%     0.2174      2.04%
VOICEVEIL                      0.0693      0.65%     0.0710      0.67%
LEVELPILOT                     0.0045      0.04%     0.0085      0.08%
S.I.P.A.                       0.0009      0.01%     0.0289      0.27%
LEADSUITE                      0.0028      0.03%     0.0060      0.06%
CHOIRFORGE                     0.0079      0.07%     0.5299      4.97%
VOICEPRESS                     0.0164      0.15%     0.0268      0.25%
RESONANCEGUARD                 0.1048      0.98%     0.1564      1.47%
BANDMOTION                     0.0187      0.18%     0.0474      0.44%
GRAPHICFORGE                   0.0021      0.02%     0.0398      0.37%
SILKCONTOUR                    0.0042      0.04%     0.0278      0.26%
GLOWLEVEL                      0.0028      0.03%     0.0340      0.32%
STRIKECLAMP                    0.0029      0.03%     0.0189      0.18%
BUSFOUNDRY                     0.0010      0.01%     0.0325      0.30%
BANDCOUNCIL                    0.0011      0.01%     0.1588      1.49%
IMPACTGUIDE                    0.0062      0.06%     0.1788      1.68%
CHAMBERPLATE                   0.0030      0.03%     0.2310      2.17%
ECHOFORGE                      0.0011      0.01%     0.0232      0.22%
MOTIONFIELD                    0.0014      0.01%     0.0796      0.75%
DEPTHMATRIX                    0.0021      0.02%     0.5240      4.91%
CONSOLECORE                    0.0216      0.20%     0.0627      0.59%
CHANNELMATRIX                  0.0450      0.42%     0.1234      1.16%
EMBERSTRIP                     0.0100      0.09%     0.0772      0.72%
MIXBOND                        0.0074      0.07%     0.0184      0.17%
REELAURA                       0.0130      0.12%     0.0798      0.75%
PEAKCITADEL                    0.0223      0.21%     0.0291      0.27%
IMPACTCROWN                    0.0114      0.11%     0.2137      2.00%
MASTERCOUNCIL                  0.0329      0.31%     0.0506      0.47%
HITCONTOUR                     0.0144      0.13%     0.0700      0.66%
DRUMKEY                        0.0465      0.44%     0.0490      0.46%
AIREMBER                       0.0092      0.09%     0.0102      0.10%
BASSBEACON                     0.0169      0.16%     0.0277      0.26%
ROOMERASER                     0.0187      0.18%     0.0596      0.56%
MONITORSPHERE                  0.0033      0.03%     0.0341      0.32%
DE-ESSER                       0.0129      0.12%     0.0230      0.22%
MASTER LIMITER                 0.1694      1.59%     0.1840      1.73%
OPTICAL COMPRESSOR             0.0123      0.12%     0.0176      0.16%
PARAMETRIC EQ                  0.0192      0.18%     0.0339      0.32%
REVERB                         0.0007      0.01%     0.0821      0.77%
STEREO DELAY                   0.0007      0.01%     0.0250      0.23%
TUBE PREAMP                    0.0065      0.06%     0.0487      0.46%
VCA COMPRESSOR                 0.0077      0.07%     0.0169      0.16%
```

Build: see the top of staging_bench.cpp (same include paths as the staging test build).
