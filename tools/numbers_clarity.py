#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""How clear NUMBERS is: the 16 phrases rendered by the firmware's engine (tests/numbers_say.c), transcribed by a
speech recogniser (Whisper base.en through faster-whisper, on the CPU), scored as word accuracy against the phrases.
0.9.4: Ed, "I can't really hear what the voice is saying"; the tuning it drove (eng_numbers.c, gen_numbers.py):

  setting (D4 unless said)               before   after
  clean, A2                               0.60     0.76
  PHANTOM's voice (RADIO 45 HUMAN 95)     ~0.2     0.70
  RADIO 50 HUMAN 70                       0.16     0.67
  STATION (RADIO 110)                      -       0.62
  (Flite's own voice at 8 kHz: 0.96; the recogniser has no context, a listener does better)

  pip install faster-whisper   (the model downloads from Hugging Face on first use)
  cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/numbers_say tests/numbers_say.c -lm
  python3 tools/numbers_clarity.py [NOTE RADIO SPEED HUMAN [DROP TAIL]] [-v]
"""
import re
import subprocess
import sys
import tempfile
import warnings

import numpy as np
from scipy import signal

warnings.filterwarnings("ignore")
PHR = ["I'M IN OUTER SPACE", "GROUND CONTROL COME IN", "CAN ANYONE HEAR ME", "THIS IS CAPSULE SEVEN OVER",
       "SIGNAL LOST", "OXYGEN FAILURE REPEAT OXYGEN FAILURE", "WE ARE STILL HERE", "IS ANYONE THERE",
       "MAYDAY MAYDAY MAYDAY", "I'M NOT ALONE", "GOODBYE EARTH", "DRIFTING FOREVER", "WHO IS CALLING",
       "STANDBY RECEIVING TRANSMISSION", "NINER FOUR SEVEN ZERO", "COLD DARK FAR FROM HOME"]
NUM = {"0": "ZERO", "1": "ONE", "2": "TWO", "3": "THREE", "4": "FOUR", "5": "FIVE", "6": "SIX", "7": "SEVEN",
       "8": "EIGHT", "9": "NINER"}


def norm(t):
    t = t.upper().replace("-", " ")
    t = re.sub(r"\d", lambda m: " " + NUM[m.group(0)] + " ", t)
    for a, b in (("NINE ", "NINER "), ("STAND BY", "STANDBY"), ("MAY DAY", "MAYDAY"), ("GOOD BYE", "GOODBYE")):
        t = t.replace(a, b)
    return re.findall(r"[A-Z']+", t)


def accuracy(ref, hyp):
    r = ref.split()
    d = np.zeros((len(r) + 1, len(hyp) + 1), int)
    d[:, 0], d[0, :] = range(len(r) + 1), range(len(hyp) + 1)
    for i in range(1, len(r) + 1):
        for j in range(1, len(hyp) + 1):
            d[i, j] = min(d[i - 1, j] + 1, d[i, j - 1] + 1, d[i - 1, j - 1] + (r[i - 1] != hyp[j - 1]))
    return max(0.0, 1 - d[-1, -1] / len(r))


def main():
    from faster_whisper import WhisperModel
    args = [a for a in sys.argv[1:] if a != "-v"] or ["62", "45", "-12", "95", "0", "20"]
    m = WhisperModel("base.en", device="cpu", compute_type="int8")
    acc = []
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["build/host/numbers_say", d] + args, check=True)
        for k, ref in enumerate(PHR):
            x = np.fromfile(f"{d}/p{k:02d}.raw", np.float32)
            x = signal.resample_poly(x, 160, 441).astype(np.float32)
            x = x / (np.abs(x).max() + 1e-9) * 0.7
            segs, _ = m.transcribe(x, language="en", beam_size=5, condition_on_previous_text=False)
            hyp = norm(" ".join(s.text for s in segs))
            acc.append(accuracy(ref, hyp))
            if "-v" in sys.argv:
                print(f"  {acc[-1]:4.2f}  {ref:38s} -> {' '.join(hyp)}")
    print(f"NUMBERS {' '.join(args)}: word accuracy {np.mean(acc):.2f}")


if __name__ == "__main__":
    main()
