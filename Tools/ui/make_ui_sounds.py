"""Synthesises the UI sounds and the front-end music loop (no samples, no third-party audio).

    python Tools/ui/make_ui_sounds.py            # writes Tools/ui/sounds/*.wav (48 kHz, 16-bit)
    .\\Tools\\ue\\run.ps1 import_ui_assets.py -NullRhi   # imports them into /Game/UI/Sounds

Everything is additive/FM synthesis with numpy, so the set is reproducible and owned by the project.
The music loop is rendered on a circular buffer, so note tails wrap to the start and the loop is seamless.
"""
import os
import wave

import numpy as np

RATE = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sounds")


def midi(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def env(n, attack, decay, sustain=0.0, release=0.0, hold=None):
    """ADSR-ish envelope over n samples (times in seconds); exponential decay to the sustain level."""
    t = np.arange(n) / RATE
    a = np.clip(t / max(attack, 1e-4), 0.0, 1.0)
    d = sustain + (1.0 - sustain) * np.exp(-np.maximum(t - attack, 0.0) / max(decay, 1e-4))
    e = a * d
    if hold is not None and release > 0.0:
        e *= np.clip(1.0 - (t - hold) / release, 0.0, 1.0)
    return e


def tone(freq, seconds, partials=((1, 1.0),), attack=0.003, decay=0.08, sustain=0.0, glide=0.0, vibrato=0.0):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    f = freq * (1.0 + glide * np.exp(-t / 0.03)) * (1.0 + vibrato * np.sin(2 * np.pi * 5.5 * t))
    phase = 2 * np.pi * np.cumsum(f) / RATE
    out = np.zeros(n)
    for mult, amp in partials:
        out += amp * np.sin(phase * mult)
    return out * env(n, attack, decay, sustain)


def fm_bell(freq, seconds, index=2.2, ratio=3.5, decay=0.25):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    mod = index * np.exp(-t / (decay * 0.6)) * np.sin(2 * np.pi * freq * ratio * t)
    return np.sin(2 * np.pi * freq * t + mod) * env(n, 0.002, decay)


def noise(seconds, seed=1):
    return np.random.default_rng(seed).uniform(-1.0, 1.0, int(seconds * RATE))


def one_pole_lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def highpass(x, cutoff):
    return x - one_pole_lowpass(x, cutoff)


def place(buffer, sound, start, gain=1.0, wrap=False):
    start = int(start)
    if wrap:
        idx = (np.arange(len(sound)) + start) % len(buffer)
        np.add.at(buffer, idx, sound * gain)
    else:
        end = min(len(buffer), start + len(sound))
        buffer[start:end] += sound[: end - start] * gain


def write(name, data, peak=0.85):
    data = np.asarray(data, dtype=np.float64)
    if data.ndim == 1:
        data = np.stack([data, data], axis=1)
    data = data / max(1e-9, np.max(np.abs(data))) * peak
    pcm = (np.clip(data, -1.0, 1.0) * 32767).astype("<i2")
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    print(f"{path}  {len(data) / RATE:.2f} s")


def fade_tail(x, seconds=0.01):
    n = int(seconds * RATE)
    x[-n:] *= np.linspace(1.0, 0.0, n)
    return x


# ---------------------------------------------------------------------------------------------------- UI cues

def ui_sounds():
    # Move: a soft round blip, slightly falling.
    move = tone(1250, 0.07, ((1, 1.0), (2, 0.25)), attack=0.002, decay=0.025, glide=0.12)
    write("ui_move", fade_tail(move), peak=0.45)

    # Value change (left/right): a higher, drier tick.
    tick = tone(1850, 0.05, ((1, 1.0), (3, 0.15)), attack=0.001, decay=0.014)
    write("ui_tick", fade_tail(tick), peak=0.4)

    # Accept: two-note rising bell (E6 -> B6).
    accept = np.zeros(int(0.32 * RATE))
    place(accept, fm_bell(midi(88), 0.25, index=1.6, decay=0.12), 0)
    place(accept, fm_bell(midi(95), 0.3, index=1.4, decay=0.16), 0.06 * RATE)
    write("ui_accept", fade_tail(accept), peak=0.6)

    # Back: two notes down (G5 -> D5), softer and rounder.
    back = np.zeros(int(0.28 * RATE))
    place(back, tone(midi(79), 0.18, ((1, 1.0), (2, 0.2)), decay=0.06), 0)
    place(back, tone(midi(74), 0.22, ((1, 1.0), (2, 0.2)), decay=0.08), 0.07 * RATE)
    write("ui_back", fade_tail(back), peak=0.5)

    # Error: two low soft-square pulses.
    err = np.zeros(int(0.3 * RATE))
    buzz = tone(185, 0.09, ((1, 1.0), (3, 0.33), (5, 0.2), (7, 0.14)), attack=0.004, decay=0.05, sustain=0.4)
    place(err, buzz, 0)
    place(err, buzz, 0.12 * RATE)
    write("ui_error", fade_tail(one_pole_lowpass(err, 2400)), peak=0.45)

    # Press Start: a quick sparkling arpeggio C6 E6 G6 C7 plus shimmer.
    start = np.zeros(int(1.1 * RATE))
    for i, note in enumerate((84, 88, 91, 96)):
        place(start, fm_bell(midi(note), 0.7, index=1.8, ratio=2.0, decay=0.3), i * 0.055 * RATE, 0.8)
    shimmer = highpass(noise(0.6, 7), 6000) * env(int(0.6 * RATE), 0.05, 0.18)
    place(start, shimmer, 0.12 * RATE, 0.12)
    write("ui_start", fade_tail(start), peak=0.65)

    # Race go (menu -> race): rising whoosh into a bright major stab.
    n = int(1.2 * RATE)
    go = np.zeros(n)
    sweep_n = int(0.55 * RATE)
    sweep = noise(0.55, 3)
    cut = np.linspace(400, 7000, sweep_n)
    lp = np.zeros(sweep_n)
    acc = 0.0
    for i in range(sweep_n):
        a = np.exp(-2 * np.pi * cut[i] / RATE)
        acc = (1 - a) * sweep[i] + a * acc
        lp[i] = acc
    place(go, lp * np.linspace(0.0, 1.0, sweep_n) ** 2, 0, 0.9)
    for note in (72, 76, 79, 84):
        place(go, fm_bell(midi(note), 0.65, index=1.2, ratio=2.0, decay=0.28), 0.5 * RATE, 0.5)
    write("ui_go", fade_tail(go), peak=0.7)

    # Pause / resume: a short filtered swoop down / up.
    for name, a, b in (("ui_pause", 900, 260), ("ui_resume", 260, 900)):
        n = int(0.22 * RATE)
        t = np.arange(n) / RATE
        f = np.geomspace(a, b, n)
        s = np.sin(2 * np.pi * np.cumsum(f) / RATE) * env(n, 0.01, 0.08)
        write(name, fade_tail(s), peak=0.45)

    # Screen slide: an airy little whoosh for screen transitions.
    sw = one_pole_lowpass(highpass(noise(0.3, 11), 900), 5000) * np.sin(np.linspace(0, np.pi, int(0.3 * RATE))) ** 2
    write("ui_whoosh", fade_tail(sw), peak=0.3)


# ---------------------------------------------------------------------------------------------- menu music

BPM = 120.0
STEP = 60.0 / BPM / 2.0  # an eighth note
BARS = 16

CHORDS = {
    "F": (53, (65, 69, 72)), "Dm": (50, (62, 65, 69)), "Bb": (46, (62, 65, 70)), "C": (48, (64, 67, 72)),
    "Am": (45, (64, 69, 72)), "Gm": (43, (62, 67, 70)),
}
# One chord per half bar (two per bar).
PROGRESSION = [
    "F", "F", "Dm", "Dm", "Bb", "Bb", "C", "C", "F", "F", "Am", "Am", "Bb", "C", "F", "F",
    "Bb", "Bb", "C", "C", "Am", "Am", "Dm", "Dm", "Gm", "Gm", "C", "C", "F", "F", "C", "C",
]
# (bar, eighth step, midi note, length in eighths)
MELODY = [
    (0, 0, 72, 2), (0, 2, 69, 1), (0, 3, 72, 1), (0, 4, 77, 3), (0, 7, 76, 1),
    (1, 0, 74, 2), (1, 2, 69, 2), (1, 4, 77, 2), (1, 6, 76, 1), (1, 7, 74, 1),
    (2, 0, 74, 3), (2, 3, 72, 1), (2, 4, 70, 2), (2, 6, 74, 2),
    (3, 0, 72, 2), (3, 2, 76, 2), (3, 4, 79, 3),
    (4, 0, 81, 2), (4, 2, 79, 1), (4, 3, 77, 1), (4, 4, 72, 2), (4, 6, 69, 2),
    (5, 0, 72, 2), (5, 2, 76, 2), (5, 4, 81, 2), (5, 6, 79, 1), (5, 7, 76, 1),
    (6, 0, 77, 2), (6, 2, 74, 2), (6, 4, 76, 2), (6, 6, 79, 2),
    (7, 0, 77, 4), (7, 4, 72, 1), (7, 5, 74, 1), (7, 6, 76, 2),
    (8, 0, 77, 3), (8, 3, 74, 1), (8, 4, 70, 2), (8, 6, 74, 2),
    (9, 0, 76, 3), (9, 3, 72, 1), (9, 4, 67, 2), (9, 6, 72, 2),
    (10, 0, 81, 2), (10, 2, 79, 1), (10, 3, 76, 1), (10, 4, 72, 2), (10, 6, 76, 2),
    (11, 0, 74, 2), (11, 2, 77, 2), (11, 4, 81, 3), (11, 7, 79, 1),
    (12, 0, 79, 2), (12, 2, 77, 1), (12, 3, 74, 1), (12, 4, 70, 2), (12, 6, 74, 2),
    (13, 0, 72, 2), (13, 2, 76, 1), (13, 3, 79, 1), (13, 4, 82, 2), (13, 6, 81, 1), (13, 7, 79, 1),
    (14, 0, 81, 3), (14, 3, 77, 1), (14, 4, 72, 2), (14, 6, 77, 2),
    (15, 0, 76, 2), (15, 2, 74, 1), (15, 3, 76, 1), (15, 4, 79, 4),
]


def lead_note(freq, seconds):
    # A soft pulse lead: odd/even partials of a 30% pulse, low-passed, a little vibrato.
    partials = [(k, np.sin(np.pi * k * 0.3) / k) for k in range(1, 12)]
    n = int(seconds * RATE)
    s = tone(freq, seconds, partials, attack=0.01, decay=0.35, sustain=0.45, vibrato=0.004)
    return s * np.clip(1.0 - (np.arange(n) / RATE - (seconds - 0.05)) / 0.05, 0.0, 1.0)


def bass_note(freq, seconds):
    partials = [(k, 1.0 / (k * k)) for k in (1, 3, 5, 7)]
    return tone(freq, seconds, partials, attack=0.004, decay=0.16, sustain=0.25)


def chord_stab(notes, seconds):
    out = np.zeros(int(seconds * RATE))
    for note in notes:
        out += fm_bell(midi(note), seconds, index=1.1, ratio=1.0, decay=0.12)
    return out / len(notes)


def pad(notes, seconds):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for note in notes:
        for detune in (-0.004, 0.004):
            f = midi(note - 12) * (1.0 + detune)
            out += sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, 6))
    e = np.minimum(1.0, t / 0.25) * np.minimum(1.0, (seconds - t) / 0.25)
    return out * e / (len(notes) * 4)


def kick():
    n = int(0.3 * RATE)
    t = np.arange(n) / RATE
    f = 45 + 95 * np.exp(-t / 0.03)
    return np.sin(2 * np.pi * np.cumsum(f) / RATE) * env(n, 0.001, 0.12)


def snare():
    n = int(0.22 * RATE)
    body = tone(190, 0.22, ((1, 1.0),), decay=0.05)
    return body * 0.5 + highpass(noise(0.22, 5), 1800) * env(n, 0.001, 0.07)


def hat(seed):
    return highpass(noise(0.05, seed), 7000) * env(int(0.05 * RATE), 0.001, 0.012)


def music():
    total = int(BARS * 8 * STEP * RATE)
    left = np.zeros(total)
    right = np.zeros(total)
    lead = np.zeros(total)
    for bar, step, note, length in MELODY:
        place(lead, lead_note(midi(note), length * STEP * 0.95), (bar * 8 + step) * STEP * RATE, wrap=True)
    lead = one_pole_lowpass(lead, 3200)
    # A short echo (dotted eighth), panned opposite, for space.
    echo = np.roll(lead, int(STEP * 1.5 * RATE)) * 0.28
    left += lead * 0.55 + echo * 0.3
    right += lead * 0.45 + echo * 0.6

    for half, name in enumerate(PROGRESSION):
        root, notes = CHORDS[name]
        start = half * 4 * STEP * RATE
        p = pad(notes, 4 * STEP + 0.2)
        place(left, p, start, 0.22, wrap=True)
        place(right, p, start, 0.22, wrap=True)
        for k in range(4):  # bass: root, octave, root, fifth on eighths 0, 1.5, 2, 3
            note = (root, root + 12, root, root + 7)[k]
            b = bass_note(midi(note), STEP * (1.4 if k == 0 else 0.9))
            place(left, b, start + (0, 1.5, 2, 3)[k] * STEP * RATE, 0.5, wrap=True)
            place(right, b, start + (0, 1.5, 2, 3)[k] * STEP * RATE, 0.5, wrap=True)
        for off in (1, 3):  # off-beat chord skank
            s = chord_stab(notes, STEP * 0.9)
            place(left, s, start + off * STEP * RATE, 0.28, wrap=True)
            place(right, s, start + off * STEP * RATE, 0.36, wrap=True)

    k, s = kick(), snare()
    for beat in range(BARS * 4):
        t = beat * 2 * STEP * RATE
        if beat % 2 == 0:
            place(left, k, t, 0.55, wrap=True)
            place(right, k, t, 0.55, wrap=True)
        else:
            place(left, s, t, 0.22, wrap=True)
            place(right, s, t, 0.22, wrap=True)
    for eighth in range(BARS * 8):
        h = hat(100 + eighth % 7)
        g = 0.07 if eighth % 2 else 0.04
        place(left, h, eighth * STEP * RATE, g * 0.8, wrap=True)
        place(right, h, eighth * STEP * RATE, g * 1.2, wrap=True)

    mix = np.stack([left, right], axis=1)
    mix = np.tanh(mix * 1.4) / np.tanh(1.4)
    write("music_menu_loop", mix, peak=0.7)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    ui_sounds()
    music()
