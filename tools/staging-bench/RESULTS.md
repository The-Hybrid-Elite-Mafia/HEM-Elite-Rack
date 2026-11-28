# Staging engines — offline CPU benchmark

One core of an Intel Xeon @ 2.10 GHz (cloud Linux VM), g++ -O2 -march=x86-64-v3, 48 kHz, 512-sample blocks,
10 s of noise + tones, best of 3 runs. **def** = factory defaults (dead flat). **act** = every smooth control
at 70 % of its range and every on/off switch on (bypass, listen, solo, freeze, mute, monitor, resets left off).
%RT = processing time / audio time (100 % = one full core). Not measured on Steven's PC; a desktop CPU is usually faster.

```
ENGINE                     def ms/blk    def %RT act ms/blk    act %RT
DYNAMIC EQ 8                   0.0385      0.36%     0.0831      0.78%
V.P.R.A.                       0.0177      0.17%     0.0846      0.79%
SUB-BASS & 808 ARCHITECT       0.3361      3.15%     0.3722      3.49%
VOICECIRCUIT                   0.3218      3.02%     0.7032      6.59%
NOTEFORGE                      0.5578      5.23%     0.5732      5.37%
THROATSHIFT                    0.2770      2.60%     0.2907      2.73%
VOICEVEIL                      0.0938      0.88%     0.0974      0.91%
LEVELPILOT                     0.0074      0.07%     0.0117      0.11%
S.I.P.A.                       0.0012      0.01%     0.0396      0.37%
LEADSUITE                      0.0037      0.04%     0.0080      0.07%
CHOIRFORGE                     0.2921      2.74%     0.6301      5.91%
VOICEPRESS                     0.0169      0.16%     0.0353      0.33%
RESONANCEGUARD                 0.1444      1.35%     0.2109      1.98%
BANDMOTION                     0.0256      0.24%     0.0674      0.63%
GRAPHICFORGE                   0.0027      0.03%     0.0561      0.53%
SILKCONTOUR                    0.0055      0.05%     0.0386      0.36%
GLOWLEVEL                      0.0040      0.04%     0.0494      0.46%
STRIKECLAMP                    0.0040      0.04%     0.0263      0.25%
BUSFOUNDRY                     0.0013      0.01%     0.0485      0.46%
BANDCOUNCIL                    0.0014      0.01%     0.2068      1.94%
IMPACTGUIDE                    0.0066      0.06%     0.2347      2.20%
CHAMBERPLATE                   0.0039      0.04%     0.3104      2.91%
ECHOFORGE                      0.0014      0.01%     0.0324      0.30%
MOTIONFIELD                    0.0019      0.02%     0.1092      1.02%
DEPTHMATRIX                    0.0027      0.03%     0.7037      6.60%
CONSOLECORE                    0.0292      0.27%     0.0848      0.79%
CHANNELMATRIX                  0.0646      0.61%     0.1673      1.57%
EMBERSTRIP                     0.0118      0.11%     0.1030      0.97%
MIXBOND                        0.0097      0.09%     0.0278      0.26%
REELAURA                       0.0182      0.17%     0.1104      1.03%
PEAKCITADEL                    0.0311      0.29%     0.0376      0.35%
IMPACTCROWN                    0.0131      0.12%     0.3042      2.85%
MASTERCOUNCIL                  0.0399      0.37%     0.0705      0.66%
HITCONTOUR                     0.0318      0.30%     0.0889      0.83%
DRUMKEY                        0.0610      0.57%     0.0645      0.60%
AIREMBER                       0.0120      0.11%     0.0134      0.13%
BASSBEACON                     0.0229      0.21%     0.0379      0.36%
ROOMERASER                     0.0258      0.24%     0.0945      0.89%
MONITORSPHERE                  0.0045      0.04%     0.0462      0.43%
DE-ESSER                       0.0173      0.16%     0.0314      0.29%
MASTER LIMITER                 0.2364      2.22%     0.2500      2.34%
OPTICAL COMPRESSOR             0.0167      0.16%     0.0242      0.23%
PARAMETRIC EQ                  0.0258      0.24%     0.0462      0.43%
REVERB                         0.0008      0.01%     0.1122      1.05%
STEREO DELAY                   0.0008      0.01%     0.0347      0.33%
TUBE PREAMP                    0.0079      0.07%     0.0678      0.64%
VCA COMPRESSOR                 0.0109      0.10%     0.0228      0.21%
```

Build: see the top of staging_bench.cpp (same include paths as the staging test build).
