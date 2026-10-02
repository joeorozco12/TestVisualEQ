#!/usr/bin/env python3
"""Generates the wiring diagrams in docs/img/*.svg. Run: python3 tools/draw_hookup.py
Pure-Python SVG, no dependencies. PNGs are rendered afterwards with tools/svg2png.js."""
import os, math

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "img")
FONT = "Helvetica, Arial, sans-serif"
C5V, CGND, C3V3, CSIG, CDATA, CAUD, CNOTE = "#d62828", "#111111", "#f77f00", "#1d4ed8", "#15803d", "#7c3aed", "#6b7280"
BOX, BOXS, HILITE = "#f8fafc", "#334155", "#fff3c4"


class SVG:
    def __init__(self, w, h, title):
        self.w, self.h, self.p = w, h, []
        self.p.append(f'<rect width="{w}" height="{h}" fill="#ffffff"/>')
        self.text(16, 26, title, 17, weight="bold")

    def raw(self, s): self.p.append(s)

    def text(self, x, y, s, size=13, anchor="start", weight="normal", fill="#111", italic=False, rotate=None):
        tr = f' transform="rotate({rotate} {x} {y})"' if rotate is not None else ""
        st = ' font-style="italic"' if italic else ""
        self.p.append(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" text-anchor="{anchor}" '
                      f'font-weight="{weight}" fill="{fill}"{st}{tr}>{s}</text>')

    def note(self, x, y, s, size=12): self.text(x, y, s, size, fill=CNOTE, italic=True)

    def rect(self, x, y, w, h, fill=BOX, stroke=BOXS, rx=6, sw=1.5, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.p.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def line(self, pts, color=CSIG, w=2, dash=None, arrow=False):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        pt = " ".join(f"{x},{y}" for x, y in pts)
        self.p.append(f'<polyline points="{pt}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linejoin="round" stroke-linecap="round"{d}/>')
        if arrow:
            (x1, y1), (x2, y2) = pts[-2], pts[-1]
            a = math.atan2(y2 - y1, x2 - x1)
            L = 9
            p1 = (x2 - L * math.cos(a - 0.45), y2 - L * math.sin(a - 0.45))
            p2 = (x2 - L * math.cos(a + 0.45), y2 - L * math.sin(a + 0.45))
            self.p.append(f'<polygon points="{x2},{y2} {p1[0]:.1f},{p1[1]:.1f} {p2[0]:.1f},{p2[1]:.1f}" fill="{color}"/>')

    def wire(self, pts, color=CSIG, label=None, lpos=None, w=2):
        self.line(pts, color, w)
        if label:
            if lpos is None:
                (x1, y1), (x2, y2) = pts[0], pts[1]
                lpos = ((x1 + x2) / 2, (y1 + y2) / 2 - 6)
            self.text(lpos[0], lpos[1], label, 12, anchor="middle", fill=color, weight="bold")

    def dot(self, x, y, color=CSIG, r=3.5):
        self.p.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')

    def ground(self, x, y, color=CGND):
        self.line([(x, y), (x, y + 10)], color)
        for i, hw in enumerate((12, 8, 4)):
            yy = y + 10 + i * 5
            self.line([(x - hw, yy), (x + hw, yy)], color, 2)

    def resistor(self, x1, y1, x2, y2, label, label2=None, color="#111"):
        """IEC rectangle between two points (horizontal or vertical)."""
        L = math.hypot(x2 - x1, y2 - y1); bl, bw = 34, 12
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        horiz = abs(x2 - x1) > abs(y2 - y1)
        if horiz:
            self.line([(x1, y1), (mx - bl / 2, my)], color); self.line([(mx + bl / 2, my), (x2, y2)], color)
            self.rect(mx - bl / 2, my - bw / 2, bl, bw, fill="#fff", stroke=color, rx=0)
            self.text(mx, my - 10, label, 12, anchor="middle", weight="bold")
            if label2: self.text(mx, my + 22, label2, 11, anchor="middle", fill=CNOTE)
        else:
            self.line([(x1, y1), (mx, my - bl / 2)], color); self.line([(mx, my + bl / 2), (x2, y2)], color)
            self.rect(mx - bw / 2, my - bl / 2, bw, bl, fill="#fff", stroke=color, rx=0)
            self.text(mx + 12, my + 4, label, 12, weight="bold")
            if label2: self.text(mx + 12, my + 18, label2, 11, fill=CNOTE)

    def ldr(self, x1, y1, x2, y2, label="LDR"):
        self.resistor(x1, y1, x2, y2, label)
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        for dy in (-7, 5):
            self.line([(mx - 30, my + dy - 10), (mx - 10, my + dy), ], "#111", 1.5, arrow=True)

    def cap(self, x1, y1, x2, y2, label, polar=False, color="#111"):
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        horiz = abs(x2 - x1) > abs(y2 - y1)
        g, pl = 4, 12
        if horiz:
            self.line([(x1, y1), (mx - g, my)], color); self.line([(mx + g, my), (x2, y2)], color)
            self.line([(mx - g, my - pl), (mx - g, my + pl)], color, 2.5)
            if polar: self.p.append(f'<path d="M{mx+g+3},{my-pl} Q{mx+g-4},{my} {mx+g+3},{my+pl}" fill="none" stroke="{color}" stroke-width="2.5"/>')
            else: self.line([(mx + g, my - pl), (mx + g, my + pl)], color, 2.5)
            self.text(mx, my - 18, label, 12, anchor="middle", weight="bold")
            if polar: self.text(mx - 16, my - 14, "+", 13, anchor="middle", weight="bold")
        else:
            self.line([(x1, y1), (mx, my - g)], color); self.line([(mx, my + g), (x2, y2)], color)
            self.line([(mx - pl, my - g), (mx + pl, my - g)], color, 2.5)
            if polar: self.p.append(f'<path d="M{mx-pl},{my+g+3} Q{mx},{my+g-4} {mx+pl},{my+g+3}" fill="none" stroke="{color}" stroke-width="2.5"/>')
            else: self.line([(mx - pl, my + g), (mx + pl, my + g)], color, 2.5)
            self.text(mx + 16, my + 4, label, 12, weight="bold")
            if polar: self.text(mx - 20, my - 6, "+", 13, anchor="middle", weight="bold")

    def speaker(self, x, y, label="4–8 Ω"):
        self.rect(x, y - 14, 14, 28, fill="#e5e7eb", rx=2)
        self.p.append(f'<polygon points="{x+14},{y-14} {x+36},{y-30} {x+36},{y+30} {x+14},{y+14}" fill="#fff" stroke="{BOXS}" stroke-width="1.5"/>')
        self.text(x + 18, y + 46, label, 12, anchor="middle")

    def module(self, x, y, w, h, title, left=(), right=(), top=(), bottom=(), sub=None, fill=BOX, pinlen=22, tsize=14):
        """Box with named pins. Returns {pin: (x, y)} at the outer end of each pin stub."""
        self.rect(x, y, w, h, fill=fill)
        self.text(x + w / 2, y + 20, title, tsize, anchor="middle", weight="bold")
        if sub: self.text(x + w / 2, y + 36, sub, 11, anchor="middle", fill=CNOTE)
        pins = {}
        def place(names, side):
            n = len(names)
            if not n: return
            if side in ("left", "right"):
                top_pad = 48 if sub else 32
                avail = h - top_pad - 12
                step = avail / n
                for i, nm in enumerate(names):
                    py = y + top_pad + step * (i + 0.5)
                    px = x if side == "left" else x + w
                    ex = px - pinlen if side == "left" else px + pinlen
                    self.line([(px, py), (ex, py)], "#111", 1.5)
                    self.text(px + (6 if side == "left" else -6), py + 4, nm, 12, anchor=("start" if side == "left" else "end"))
                    pins[nm] = (ex, py)
            else:
                step = w / n
                for i, nm in enumerate(names):
                    px = x + step * (i + 0.5)
                    py = y if side == "top" else y + h
                    ey = py - pinlen if side == "top" else py + pinlen
                    self.line([(px, py), (px, ey)], "#111", 1.5)
                    self.text(px, py + (14 if side == "top" else -6), nm, 11, anchor="middle")
                    pins[nm] = (px, ey)
        place(left, "left"); place(right, "right"); place(top, "top"); place(bottom, "bottom")
        return pins

    def legend(self, x, y, items):
        for i, (c, s) in enumerate(items):
            self.line([(x, y + i * 18), (x + 24, y + i * 18)], c, 3)
            self.text(x + 32, y + i * 18 + 4, s, 12)

    def save(self, name):
        body = "\n".join(self.p)
        svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">\n{body}\n</svg>\n'
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, name), "w") as f: f.write(svg)
        print("wrote", name)


LEG = [(C5V, "5 V"), (CGND, "GND"), (C3V3, "3V3"), (CSIG, "logic signal"), (CDATA, "LED data"), (CAUD, "audio")]


# --------------------------------------------------------------------------- 1
def power_star():
    s = SVG(960, 640, "0. Power distribution — one star point, every 5 V load fed in parallel")
    bank = s.module(30, 200, 170, 110, "USB power bank", right=["VBUS (5 V)", "GND"], sub="2 A / 2.4 A port")
    # cable
    s.wire([bank["VBUS (5 V)"], (330, bank["VBUS (5 V)"][1])], C5V)
    s.wire([bank["GND"], (330, bank["GND"][1])], CGND)
    s.rect(240, 215, 70, 70, fill="#fff", dash="4 3"); s.text(275, 236, "USB cable", 11, anchor="middle"); s.text(275, 250, "≤ 0.5 m", 11, anchor="middle"); s.text(275, 264, "24 AWG pwr", 11, anchor="middle")
    # star block
    s.rect(330, 190, 70, 130, fill=HILITE); s.text(365, 215, "STAR", 12, anchor="middle", weight="bold"); s.text(365, 229, "terminal", 11, anchor="middle"); s.text(365, 242, "block", 11, anchor="middle")
    # buses
    s.line([(400, 110), (400, 590)], C5V, 4); s.text(392, 100, "5 V bus", 12, anchor="end", fill=C5V, weight="bold")
    s.line([(440, 110), (440, 590)], CGND, 4); s.text(448, 100, "GND bus", 12, fill=CGND, weight="bold")
    s.wire([(330 + 70, bank["VBUS (5 V)"][1]), (400, bank["VBUS (5 V)"][1])], C5V); s.dot(400, bank["VBUS (5 V)"][1], C5V)
    s.wire([(330 + 70, bank["GND"][1]), (440, bank["GND"][1])], CGND); s.dot(440, bank["GND"][1], CGND)
    loads = [("ESP32 DevKit", "VIN / 5V", "GND", None, "feeds the 3V3 LDO; nothing else hangs on the DevKit 5 V pin"),
             ("74AHCT125", "VCC (14)", "GND (7)", ("100 nF", False), "ceramic across pins 14/7"),
             ("PAM8302 amp", "VIN", "GND", ("100 µF", True), "electrolytic at the module"),
             ("HC-SR04", "VCC", "GND", None, "5 V only; 3.3 V = short range"),
             ("5×5 WS2812B grid", "5V", "GND", ("1000 µF", True), "≥ 6.3 V, leads ≤ 2 cm from grid pads")]
    y = 120
    for title, p5, pg, cap, note in loads:
        m = s.module(640, y, 180, 72, title, left=[p5, pg], tsize=13)
        s.wire([(400, m[p5][1]), m[p5]], C5V); s.dot(400, m[p5][1], C5V)
        s.wire([(440, m[pg][1]), m[pg]], CGND); s.dot(440, m[pg][1], CGND)
        if cap:
            cx = 560
            s.dot(cx, m[p5][1], C5V); s.dot(cx, m[pg][1], CGND)
            s.cap(cx, m[p5][1], cx, m[pg][1], cap[0], polar=cap[1])
        s.note(640, y + 86, note, 11)
        y += 96
    s.note(30, 400, "Rules:", 12); s.note(30, 418, "• one GND for everything (shifter + divider", 11); s.note(30, 432, "  reference the ESP32 GND)", 11)
    s.note(30, 450, "• never bank + PC USB at the same time", 11)
    s.note(30, 468, "• grid never through the DevKit's 5 V pin", 11)
    s.note(30, 486, "• twist each 5 V/GND pair on the way out", 11)
    s.legend(30, 540, LEG[:2])
    s.save("00_power_star.svg")


# --------------------------------------------------------------------------- 2
def led_shifter():
    s = SVG(1000, 560, "1. LED grid through a 74AHCT125 level shifter (bench_leds)")
    esp = s.module(30, 150, 170, 200, "ESP32 DevKit", right=["VIN / 5V", "GND", "GPIO27"], sub="30-pin, classic WROOM")
    # DIP-14 with pins
    L = ["1  1OE", "2  1A", "3  1Y", "4  2OE", "5  2A", "6  2Y", "7  GND"]
    R = ["VCC  14", "4OE  13", "4A  12", "4Y  11", "3OE  10", "3A  9", "3Y  8"]
    ic = s.module(420, 90, 160, 360, "74AHCT125", left=L, right=R, sub="quad buffer, 5 V side", fill="#eef2ff")
    s.raw(f'<path d="M{490},{90} a10,10 0 0 0 20,0" fill="none" stroke="{BOXS}" stroke-width="1.5"/>')  # notch
    grid = s.module(800, 150, 170, 200, "5×5 WS2812B", left=["DIN", "5V", "GND"], sub="DIN corner = (0,0)")
    # rails
    s.line([(230, 60), (980, 60)], C5V, 3); s.text(232, 52, "5 V (from star)", 12, fill=C5V, weight="bold")
    s.line([(230, 520), (980, 520)], CGND, 3); s.text(232, 540, "GND (from star)", 12, fill=CGND, weight="bold")
    # ESP32 power
    s.wire([esp["VIN / 5V"], (250, esp["VIN / 5V"][1]), (250, 60)], C5V); s.dot(250, 60, C5V)
    s.wire([esp["GND"], (270, esp["GND"][1]), (270, 520)], CGND); s.dot(270, 520, CGND)
    # GPIO27 -> 1A
    s.wire([esp["GPIO27"], (330, esp["GPIO27"][1]), (330, ic["2  1A"][1]), ic["2  1A"]], CDATA, "GPIO27 → 1A  (≤ 15 cm)", (340, esp["GPIO27"][1] + 22))
    # 1OE -> GND
    s.wire([ic["1  1OE"], (370, ic["1  1OE"][1]), (370, 520)], CGND); s.dot(370, 520, CGND)
    s.text(372, ic["1  1OE"][1] - 6, "OE low = enabled", 11, fill=CNOTE)
    # unused inputs/OE -> GND
    for nm in ("4  2OE", "5  2A"):
        s.wire([ic[nm], (370, ic[nm][1])], CGND); s.dot(370, ic[nm][1], CGND)
    s.text(372, ic["5  2A"][1] + 16, "unused: tie to GND", 11, fill=CNOTE)
    s.wire([ic["7  GND"], (370, ic["7  GND"][1])], CGND); s.dot(370, ic["7  GND"][1], CGND)
    for nm in ("4OE  13", "4A  12", "3OE  10", "3A  9"):
        s.wire([ic[nm], (640, ic[nm][1]), (640, 520)], CGND)
    s.dot(640, 520, CGND); s.text(644, ic["3A  9"][1] - 40, "unused", 11, fill=CNOTE); s.text(644, ic["3A  9"][1] - 27, "→ GND", 11, fill=CNOTE)
    s.text(606, ic["4Y  11"][1] + 4, "NC", 11, fill=CNOTE); s.text(606, ic["3Y  8"][1] + 4, "NC", 11, fill=CNOTE); s.text(394, ic["6  2Y"][1] + 4, "NC", 11, fill=CNOTE, anchor="end")
    # VCC + 100 nF
    s.wire([ic["VCC  14"], (660, ic["VCC  14"][1]), (660, 60)], C5V); s.dot(660, 60, C5V)
    s.dot(640, ic["VCC  14"][1], C5V)
    s.cap(640, ic["VCC  14"][1], 640, ic["4OE  13"][1], "100 nF"); s.dot(640, ic["4OE  13"][1], CGND)
    s.note(656, (ic["VCC  14"][1] + ic["4OE  13"][1]) / 2 + 18, "ceramic, at pins 14/7", 11)
    # 1Y -> 330R -> DIN
    y1 = ic["3  1Y"][1]
    s.wire([ic["3  1Y"], (385, y1), (385, 85), (600, 85)], CDATA)
    s.resistor(600, 85, 680, 85, "330 Ω", color=CDATA); s.text(640, 106, "series damping", 11, anchor="middle", fill=CNOTE)
    s.wire([(680, 85), (750, 85), (750, grid["DIN"][1]), grid["DIN"]], CDATA)
    s.text(755, 120, "up to ~1 m,", 11, fill=CDATA); s.text(755, 133, "twist with GND", 11, fill=CDATA)
    # grid power + 1000 uF
    s.wire([grid["5V"], (720, grid["5V"][1]), (720, 60)], C5V); s.dot(720, 60, C5V)
    s.wire([grid["GND"], (720, grid["GND"][1]), (720, 520)], CGND); s.dot(720, 520, CGND)
    s.dot(740, grid["5V"][1], C5V); s.dot(740, grid["GND"][1], CGND)
    s.cap(740, grid["5V"][1], 740, grid["GND"][1], "", polar=True)
    s.text(758, grid["5V"][1] + 36, "1000 µF", 12, weight="bold"); s.text(758, grid["5V"][1] + 50, "≥ 6.3 V", 11, fill=CNOTE)
    s.note(30, 400, "Why: WS2812B V_IH = 0.7·VDD = 3.5 V.", 11); s.note(30, 416, "3.3 V data works until the cable drop is", 11); s.note(30, 430, "small and VDD is a full 5.0 V.", 11)
    s.note(30, 460, "Alt: 74HCT245 (DIR→5 V, OE→GND, A1 in, B1 out)", 11); s.note(30, 474, "or SN74AHCT1G125 single gate.", 11)
    s.legend(30, 505, [LEG[0], LEG[1], LEG[4]])
    s.save("01_led_shifter.svg")


# --------------------------------------------------------------------------- 3
def ultrasonic():
    s = SVG(820, 420, "2. HC-SR04 ultrasonic with ECHO divider (bench_ultrasonic)")
    esp = s.module(30, 100, 170, 220, "ESP32 DevKit", right=["VIN / 5V", "GND", "GPIO32", "GPIO33"])
    us = s.module(620, 100, 170, 220, "HC-SR04", left=["VCC", "TRIG", "ECHO", "GND"], sub="5 V module")
    s.wire([esp["VIN / 5V"], (260, esp["VIN / 5V"][1]), (260, us["VCC"][1]), us["VCC"]], C5V, "5 V from star", (430, us["VCC"][1] - 8))
    s.wire([esp["GND"], (290, esp["GND"][1]), (290, 395), (560, 395), (560, us["GND"][1]), us["GND"]], CGND)
    s.wire([esp["GPIO32"], (320, esp["GPIO32"][1]), (320, us["TRIG"][1]), us["TRIG"]], CSIG, "TRIG ← GPIO32 (3.3 V is a valid high)", (470, us["TRIG"][1] - 8))
    ye = us["ECHO"][1]; yg = esp["GPIO33"][1]
    # ECHO -> 1k -> node -> GPIO33 ; node -> 2k -> GND
    s.resistor(us["ECHO"][0], ye, 470, ye, "1 kΩ", color=CSIG)
    s.dot(470, ye, CSIG)
    s.wire([(470, ye), (420, ye), (420, yg), esp["GPIO33"]], CSIG, "3.33 V to GPIO33", (330, yg - 8))
    s.resistor(470, ye, 470, 395, "2 kΩ", color=CSIG); s.dot(470, 395, CGND)
    s.text(560, 345, "5 V × 2k/(1k+2k) = 3.33 V", 12, fill=CNOTE)
    s.text(560, 361, "never ECHO → GPIO directly", 12, fill=CNOTE)
    s.note(30, 350, "Mount 40–100 cm up, under a lip, aimed slightly down at the approach.", 11)
    s.note(30, 366, "Leads > 1 m: add 100 nF across VCC/GND at the sensor.", 11)
    s.legend(640, 52, [LEG[0], LEG[1], LEG[3]])
    s.save("02_ultrasonic.svg")


# --------------------------------------------------------------------------- 4
def ldr():
    s = SVG(900, 420, "3. Ambient light: LDR divider on GPIO34 (bench_light) — or BH1750 on I²C")
    esp = s.module(30, 100, 170, 220, "ESP32 DevKit", right=["3V3", "GPIO34", "GND", "GPIO21 SDA", "GPIO22 SCL"])
    y3, y34, yg = esp["3V3"][1], esp["GPIO34"][1], esp["GND"][1]
    nx = 400
    s.wire([esp["3V3"], (nx, y3)], C3V3); s.dot(nx, y3, C3V3)
    s.ldr(nx, y3, nx, y34, "LDR (GL5528)")
    s.dot(nx, y34, CSIG)
    s.wire([(nx, y34), esp["GPIO34"]], CSIG, "GPIO34 (ADC1, input-only)", (310, y34 + 20))
    s.resistor(nx, y34, nx, 370, "10 kΩ", "mandatory"); s.dot(nx, 370, CGND)
    s.wire([(nx, y34), (470, y34)], CSIG); s.dot(470, y34, CSIG)
    s.cap(470, y34, 470, 370, "100 nF"); s.dot(470, 370, CGND)
    s.wire([esp["GND"], (240, yg), (240, 370), (470, 370)], CGND)
    s.note(230, 400, "Brighter → lower R_LDR → higher voltage at GPIO34. Thresholds calibrated with bench_light.", 11)
    # BH1750 alt
    s.rect(600, 90, 280, 250, fill="#fff", dash="5 4"); s.text(740, 112, "Alternative: BH1750 (I²C)", 13, anchor="middle", weight="bold")
    bh = s.module(680, 130, 140, 170, "BH1750", left=["VCC", "GND", "SDA", "SCL", "ADDR"], tsize=13)
    s.wire([bh["VCC"], (640, bh["VCC"][1])], C3V3); s.text(636, bh["VCC"][1] + 4, "3V3", 11, anchor="end", fill=C3V3)
    s.wire([bh["GND"], (640, bh["GND"][1])], CGND); s.text(636, bh["GND"][1] + 4, "GND", 11, anchor="end")
    s.wire([bh["SDA"], (640, bh["SDA"][1])], CSIG); s.text(636, bh["SDA"][1] + 4, "GPIO21", 11, anchor="end", fill=CSIG)
    s.wire([bh["SCL"], (640, bh["SCL"][1])], CSIG); s.text(636, bh["SCL"][1] + 4, "GPIO22", 11, anchor="end", fill=CSIG)
    s.text(636, bh["ADDR"][1] + 4, "NC", 11, anchor="end", fill=CNOTE)
    s.note(610, 322, "set LIGHT_SENSOR_TYPE LIGHT_SENSOR_BH1750", 11)
    s.note(30, 350, "Sensor must see sky, not the grid:", 11); s.note(30, 366, "top of the pumpkin under a hot-glue dome.", 11)
    s.legend(30, 395, [LEG[2], LEG[1], LEG[3]])
    s.save("03_ldr.svg")


# --------------------------------------------------------------------------- 5
def audio_sd():
    s = SVG(1000, 620, "4. Audio: DAC1 → PAM8302 → speaker, and µSD on VSPI (bench_audio)")
    esp = s.module(30, 90, 190, 460, "ESP32 DevKit", right=["VIN / 5V", "3V3", "GND", "GPIO25 (DAC1)", "GPIO26", "GPIO5", "GPIO18", "GPIO19", "GPIO23"])
    amp = s.module(560, 80, 180, 220, "PAM8302", left=["VIN", "GND", "SD", "A+", "A−"], right=["OUT +", "OUT −"], sub="class-D, bridged out")
    sd = s.module(560, 360, 180, 220, "µSD module", left=["VCC", "GND", "CS", "SCK", "MISO", "MOSI"], sub="3.3 V type, no shifter")
    s.speaker(830, (amp["OUT +"][1] + amp["OUT −"][1]) / 2)
    s.wire([amp["OUT +"], (830, amp["OUT +"][1])], CAUD); s.wire([amp["OUT −"], (830, amp["OUT −"][1])], CAUD)
    s.note(770, amp["OUT −"][1] + 70, "never ground a speaker lead", 11)
    # power
    s.wire([esp["VIN / 5V"], (260, esp["VIN / 5V"][1]), (260, amp["VIN"][1]), amp["VIN"]], C5V, "5 V from star", (400, amp["VIN"][1] - 8))
    s.dot(500, amp["VIN"][1], C5V); s.dot(500, amp["GND"][1], CGND)
    s.cap(500, amp["VIN"][1], 500, amp["GND"][1], "100 µF", polar=True)
    s.wire([esp["GND"], (290, esp["GND"][1]), (290, amp["GND"][1]), amp["GND"]], CGND)
    s.wire([(290, amp["GND"][1]), (290, sd["GND"][1]), sd["GND"]], CGND)
    s.wire([esp["3V3"], (275, esp["3V3"][1]), (275, sd["VCC"][1]), sd["VCC"]], C3V3, "3V3", (420, sd["VCC"][1] - 8))
    # audio
    y25 = esp["GPIO25 (DAC1)"][1]
    s.wire([esp["GPIO25 (DAC1)"], (330, y25), (330, amp["A+"][1]), (400, amp["A+"][1])], CAUD)
    s.resistor(400, amp["A+"][1], 480, amp["A+"][1], "1 kΩ", color=CAUD); s.wire([(480, amp["A+"][1]), amp["A+"]], CAUD)
    s.text(345, amp["A+"][1] + 24, "DAC out, 1.65 V bias; module input is AC-coupled", 11, fill=CNOTE)
    s.wire([amp["A−"], (520, amp["A−"][1]), (520, 330), (290, 330)], CGND); s.dot(290, 330, CGND)
    s.wire([esp["GPIO26"], (360, esp["GPIO26"][1]), (360, amp["SD"][1]), amp["SD"]], CSIG, "SD: low = mute (idle), high while playing", (430, amp["SD"][1] - 8))
    # SD SPI
    for pin, gp in (("CS", "GPIO5"), ("SCK", "GPIO18"), ("MISO", "GPIO19"), ("MOSI", "GPIO23")):
        xo = {"CS": 380, "SCK": 400, "MISO": 420, "MOSI": 440}[pin]
        s.wire([esp[gp], (xo, esp[gp][1]), (xo, sd[pin][1]), sd[pin]], CSIG)
        s.text(xo + 6, sd[pin][1] - 5, gp, 10, fill=CSIG)
    s.note(560, 600, "SPI leads < 10 cm. GPIO5 is a strapping pin: the card's CS pull-up keeps it high at boot.", 11)
    s.note(30, 580, "Gain pot on the module ≈ ⅓ turn, then AUDIO_VOLUME / web slider.", 11)
    s.legend(800, 420, [LEG[0], LEG[1], LEG[2], LEG[3], LEG[5]])
    s.save("04_audio_sd.svg")


# --------------------------------------------------------------------------- 6
def dump_load():
    s = SVG(760, 400, "6. Optional keep-alive dump load (only if bench_power says the bank still drops)")
    esp = s.module(30, 110, 170, 160, "ESP32 DevKit", right=["VIN / 5V", "GPIO4", "GND"])
    s.wire([esp["GPIO4"], (240, esp["GPIO4"][1])], CSIG)
    s.resistor(240, esp["GPIO4"][1], 330, esp["GPIO4"][1], "1 kΩ", color=CSIG)
    gx, gy = 400, esp["GPIO4"][1]
    s.wire([(330, gy), (gx - 18, gy)], CSIG)
    # N-MOSFET symbol (simplified)
    s.line([(gx - 18, gy - 22), (gx - 18, gy + 22)], "#111", 2.5)   # gate plate
    s.line([(gx - 10, gy - 20), (gx - 10, gy + 20)], "#111", 2.5)   # channel
    s.line([(gx - 10, gy - 16), (gx + 14, gy - 16), (gx + 14, gy - 50)], "#111", 2)  # drain
    s.line([(gx - 10, gy + 16), (gx + 14, gy + 16), (gx + 14, gy + 50)], "#111", 2)  # source
    s.line([(gx - 10, gy), (gx + 14, gy), (gx + 14, gy + 16)], "#111", 1.5)
    s.line([(gx - 2, gy - 5), (gx - 10, gy), (gx - 2, gy + 5)], "#111", 1.5)
    s.text(gx + 28, gy + 4, "2N7000", 12, weight="bold"); s.text(gx + 28, gy + 18, "(logic-level N-MOSFET)", 10, fill=CNOTE)
    s.text(gx - 24, gy - 28, "G", 11, anchor="end"); s.text(gx + 20, gy - 40, "D", 11); s.text(gx + 20, gy + 44, "S", 11)
    s.resistor(gx + 14, gy - 50, gx + 14, 60, "22 Ω 2 W", "≈ 230 mA pulse")
    s.wire([(gx + 14, 60), (260, 60), (260, esp["VIN / 5V"][1]), esp["VIN / 5V"]], C5V, "5 V from star", (330, 52))
    s.wire([(gx + 14, gy + 50), (gx + 14, 320), (260, 320), (260, esp["GND"][1]), esp["GND"]], CGND)
    s.note(480, 262, "Set KEEPALIVE_LOAD_PIN 4 in config.h.", 11)
    s.note(480, 278, "0.6 s every 8 s → 0.1 W average.", 11)
    s.note(480, 294, "2N2222 also fine: 1 kΩ into base,", 11); s.note(480, 308, "collector to the 22 Ω, emitter to GND.", 11)
    s.legend(480, 335, [LEG[0], LEG[1], LEG[3]])
    s.save("06_dump_load.svg")


# --------------------------------------------------------------------------- 7
def pinmap():
    s = SVG(760, 760, "ESP32 DevKit (30-pin) — pins used by this project")
    left = [("EN", None), ("VP / 36", ("VBUS sense ÷2 (opt.)", CSIG)), ("VN / 39", None), ("D34", ("LDR divider", CSIG)), ("D35", ("mic envelope (opt.)", CSIG)),
            ("D32", ("HC-SR04 TRIG", CSIG)), ("D33", ("HC-SR04 ECHO (÷)", CSIG)), ("D25", ("DAC1 → PAM8302 A+", CAUD)),
            ("D26", ("PAM8302 SD", CSIG)), ("D27", ("LED data → 74AHCT125", CDATA)), ("D14", ("PIR out (opt.)", CSIG)), ("D12", ("strap: keep free", "#9ca3af")),
            ("GND", ("GND", CGND)), ("D13", None), ("VIN", ("5 V from star", C5V))]
    right = [("D23", ("µSD MOSI", CSIG)), ("D22", ("I²C SCL (BH1750, opt.)", CSIG)), ("TX0", ("USB serial", "#9ca3af")), ("RX0", ("USB serial", "#9ca3af")),
             ("D21", ("I²C SDA (BH1750, opt.)", CSIG)), ("GND", ("GND", CGND)), ("D19", ("µSD MISO", CSIG)), ("D18", ("µSD SCK", CSIG)),
             ("D5", ("µSD CS (strap, pull-up ok)", CSIG)), ("TX2 / 17", None), ("RX2 / 16", None), ("D4", ("dump load (opt.)", CSIG)),
             ("D2", ("on-board status LED", CSIG)), ("D15", ("strap: keep free", "#9ca3af")), ("3V3", ("3V3 → LDR, µSD, BH1750", C3V3))]
    bx, by, bw, bh = 290, 70, 180, 640
    s.rect(bx, by, bw, bh, fill="#1f2937", stroke="#111", rx=10)
    s.rect(bx + 55, by - 10, 70, 60, fill="#9ca3af", rx=3); s.text(bx + 90, by + 24, "USB", 11, anchor="middle", fill="#111")
    s.rect(bx + 40, by + 120, 100, 160, fill="#374151", rx=4); s.text(bx + 90, by + 205, "ESP32-WROOM", 11, anchor="middle", fill="#e5e7eb")
    s.rect(bx + 20, by + 75, 18, 12, fill="#2563eb", rx=2); s.text(bx + 29, by + 100, "D2", 9, anchor="middle", fill="#e5e7eb")
    s.rect(bx + 10, by + bh - 40, 24, 24, fill="#6b7280", rx=3); s.text(bx + 22, by + bh - 24, "BOOT", 7, anchor="middle", fill="#fff")
    s.rect(bx + bw - 34, by + bh - 40, 24, 24, fill="#6b7280", rx=3); s.text(bx + bw - 22, by + bh - 24, "EN", 7, anchor="middle", fill="#fff")
    step = (bh - 100) / 15
    for i, (nm, use) in enumerate(left):
        y = by + 60 + step * (i + 0.5)
        s.rect(bx - 6, y - 7, 12, 14, fill="#fbbf24", stroke="#92400e", rx=2)
        s.text(bx - 14, y + 4, nm, 11, anchor="end", weight="bold" if use and use[1] != "#9ca3af" else "normal")
        if use: s.text(bx - 70, y + 4, use[0], 11, anchor="end", fill=use[1], weight="bold" if use[1] != "#9ca3af" else "normal")
    for i, (nm, use) in enumerate(right):
        y = by + 60 + step * (i + 0.5)
        s.rect(bx + bw - 6, y - 7, 12, 14, fill="#fbbf24", stroke="#92400e", rx=2)
        s.text(bx + bw + 14, y + 4, nm, 11, weight="bold" if use and use[1] != "#9ca3af" else "normal")
        if use: s.text(bx + bw + 70, y + 4, use[0], 11, fill=use[1], weight="bold" if use[1] != "#9ca3af" else "normal")
    s.note(16, 735, "BOOT button = GPIO0 (next effect / long-press force-on). 38-pin boards add flash pins SD0–SD3/CLK/CMD: never use them.", 10)
    s.note(16, 750, "Pin order matches the common DevKit V1; verify against your board's silkscreen.", 10)
    s.save("07_esp32_pinmap.svg")


# --------------------------------------------------------------------------- 8
def assembly():
    s = SVG(900, 770, "7. Assembly inside the pumpkin (cross-section)")
    cx, cy = 420, 360
    # pumpkin body
    s.raw(f'<ellipse cx="{cx}" cy="{cy}" rx="260" ry="220" fill="#fdba74" stroke="#c2410c" stroke-width="4"/>')
    for dx in (-150, -75, 0, 75, 150):
        s.raw(f'<path d="M{cx+dx},{cy-215} Q{cx+dx*1.15},{cy} {cx+dx},{cy+215}" fill="none" stroke="#ea580c" stroke-width="2" opacity="0.6"/>')
    s.raw(f'<rect x="{cx-22}" y="{cy-262}" width="44" height="52" rx="8" fill="#65a30d" stroke="#365314" stroke-width="3"/>')
    # cut-outs (front face, shown as dashed)
    s.raw(f'<polygon points="{cx-130},{cy-90} {cx-70},{cy-90} {cx-100},{cy-40}" fill="#fff7ed" stroke="#9a3412" stroke-width="2" stroke-dasharray="5 3"/>')
    s.raw(f'<polygon points="{cx+70},{cy-90} {cx+130},{cy-90} {cx+100},{cy-40}" fill="#fff7ed" stroke="#9a3412" stroke-width="2" stroke-dasharray="5 3"/>')
    s.raw(f'<path d="M{cx-120},{cy+40} Q{cx},{cy+120} {cx+120},{cy+40} L{cx+90},{cy+60} L{cx+50},{cy+45} L{cx},{cy+70} L{cx-50},{cy+45} L{cx-90},{cy+60} Z" fill="#fff7ed" stroke="#9a3412" stroke-width="2" stroke-dasharray="5 3"/>')
    # skewer + grid
    s.line([(cx - 200, cy - 150), (cx + 200, cy - 150)], "#78350f", 5)
    s.line([(cx, cy - 150), (cx, cy - 120)], "#111", 2)
    s.rect(cx - 45, cy - 120, 90, 90, fill="#1f2937", stroke="#111", rx=4)
    for i in range(5):
        for j in range(5):
            s.raw(f'<rect x="{cx-40+i*17}" y="{cy-115+j*17}" width="12" height="12" rx="2" fill="#fbbf24"/>')
    # speaker pod
    s.raw(f'<rect x="{cx-30}" y="{cy+15}" width="60" height="40" rx="6" fill="#e5e7eb" stroke="#374151" stroke-width="2"/>')
    s.raw(f'<circle cx="{cx}" cy="{cy+35}" r="12" fill="#9ca3af" stroke="#374151" stroke-width="2"/>')
    # ultrasonic in right eye
    s.rect(cx + 72, cy - 85, 56, 24, fill="#1e40af", stroke="#111", rx=3)
    s.raw(f'<circle cx="{cx+86}" cy="{cy-73}" r="8" fill="#9ca3af" stroke="#111"/><circle cx="{cx+114}" cy="{cy-73}" r="8" fill="#9ca3af" stroke="#111"/>')
    # LDR on top
    s.raw(f'<circle cx="{cx-60}" cy="{cy-228}" r="9" fill="#fde68a" stroke="#92400e" stroke-width="2"/>')
    s.raw(f'<path d="M{cx-75},{cy-222} Q{cx-60},{cy-245} {cx-45},{cy-222}" fill="none" stroke="#a16207" stroke-width="2" stroke-dasharray="3 2"/>')
    s.line([(cx - 60, cy - 219), (cx - 60, cy - 160), (cx - 150, cy - 160), (cx - 150, cy + 95)], "#f77f00", 2)
    # box at bottom
    s.rect(cx - 160, cy + 95, 320, 90, fill="#f1f5f9", stroke="#334155", rx=6)
    s.text(cx, cy + 112, "IP54 box", 12, anchor="middle", weight="bold")
    for i, (nm, w) in enumerate((("ESP32", 54), ("shifter", 50), ("amp", 40), ("µSD", 40), ("bank", 60), ("silica", 44))):
        x = cx - 150 + sum(ww + 6 for _, ww in (("ESP32", 54), ("shifter", 50), ("amp", 40), ("µSD", 40), ("bank", 60), ("silica", 44))[:i])
        s.rect(x, cy + 125, w, 44, fill="#fff", rx=3); s.text(x + w / 2, cy + 151, nm, 10, anchor="middle")
    # cables from box
    s.line([(cx - 60, cy + 95), (cx - 60, cy - 30), (cx, cy - 30)], "#15803d", 2)       # LED data/power
    s.line([(cx + 20, cy + 95), (cx + 20, cy + 55)], "#7c3aed", 2)                        # speaker
    s.line([(cx + 60, cy + 95), (cx + 60, cy - 55), (cx + 72, cy - 60)], "#1d4ed8", 2)   # ultrasonic
    # drip loop (bank charging / USB exit)
    s.raw(f'<path d="M{cx+160},{cy+170} C{cx+230},{cy+170} {cx+230},{cy+250} {cx+300},{cy+230} C{cx+340},{cy+220} {cx+340},{cy+160} {cx+400},{cy+160}" fill="none" stroke="#111" stroke-width="3"/>')
    s.text(cx + 300, cy + 272, "drip loop → bank charger / nothing", 11, anchor="middle", fill=CNOTE)
    # numbered markers + legend
    def mark(x, y, n):
        s.raw(f'<circle cx="{x}" cy="{y}" r="11" fill="#111"/>'); s.text(x, y + 4, str(n), 12, anchor="middle", weight="bold", fill="#fff")
    mark(cx - 85, cy - 240, 1); mark(cx - 60, cy - 110, 2); mark(cx + 140, cy - 95, 3); mark(cx + 45, cy + 10, 4)
    mark(cx - 175, cy + 105, 5); mark(cx + 240, cy + 215, 6)
    items = ["LDR on top under a hot-glue dome; wire in through the stem hole",
             "Grid hung from a skewer, conformal-coated back; lights the cut-outs from behind",
             "HC-SR04 in an eye, under the lid lip, aimed slightly down at the approach",
             "Speaker in a sealed pod behind the mouth; rear sealed from the flesh",
             "IP54 box: ESP32, shifter perfboard, amp, µSD, star terminal, power bank, silica gel",
             "Single grommet exit at the bottom; every cable leaves with a drip loop"]
    for i, t in enumerate(items):
        mark(30, 612 + i * 20, i + 1); s.text(50, 616 + i * 20, t, 12)
    s.note(16, 752, "Nothing bare touches flesh. Interior is wet, acidic, 100 % RH and rots in ~5 days. Lid on; condensation at dusk is the main risk → conformal coat.", 10)
    s.save("08_assembly.svg")


# --------------------------------------------------------------------------- 9
def overview():
    s = SVG(1100, 780, "Whole node at a glance — every wire, by GPIO")
    esp = s.module(430, 170, 240, 380, "ESP32 DevKit",
                   left=["VIN / 5V", "GND", "GPIO27", "GPIO32", "GPIO33", "GPIO34", "3V3"],
                   right=["GPIO25 (DAC1)", "GPIO26", "GPIO5 CS", "GPIO18 SCK", "GPIO19 MISO", "GPIO23 MOSI", "GPIO4 (opt.)"], sub="core 1: LEDs/sensors · core 0: net/audio")
    bank = s.module(40, 40, 200, 90, "USB power bank 2 A", right=["5 V", "GND"], tsize=13)
    s.rect(300, 40, 70, 90, fill=HILITE); s.text(335, 80, "STAR", 12, anchor="middle", weight="bold"); s.text(335, 96, "5 V / GND", 10, anchor="middle")
    s.wire([bank["5 V"], (300, bank["5 V"][1])], C5V); s.wire([bank["GND"], (300, bank["GND"][1])], CGND)
    s.wire([(370, bank["5 V"][1]), (400, bank["5 V"][1]), (400, esp["VIN / 5V"][1]), esp["VIN / 5V"]], C5V)
    s.wire([(370, bank["GND"][1]), (385, bank["GND"][1]), (385, esp["GND"][1]), esp["GND"]], CGND)
    s.text(380, 140, "5 V + GND also to: shifter, amp, HC-SR04, grid (see 0.)", 10, fill=CNOTE, anchor="end")
    sh = s.module(130, 200, 150, 80, "74AHCT125", left=["1Y"], right=["1A"], tsize=13)
    grid = s.module(40, 320, 110, 90, "5×5 grid", right=["DIN"], tsize=13)
    s.wire([esp["GPIO27"], (360, esp["GPIO27"][1]), (360, sh["1A"][1]), sh["1A"]], CDATA, "GPIO27", (331, sh["1A"][1] - 8))
    s.wire([sh["1Y"], (80, sh["1Y"][1]), (80, 300), (180, 300), (180, grid["DIN"][1]), grid["DIN"]], CDATA, "330 Ω", (130, 294))
    us = s.module(130, 420, 150, 100, "HC-SR04", right=["TRIG", "ECHO"], tsize=13)
    s.wire([esp["GPIO32"], (340, esp["GPIO32"][1]), (340, us["TRIG"][1]), us["TRIG"]], CSIG, "GPIO32", (365, esp["GPIO32"][1] - 6))
    s.wire([esp["GPIO33"], (325, esp["GPIO33"][1]), (325, us["ECHO"][1]), us["ECHO"]], CSIG, "GPIO33 via 1k/2k", (365, esp["GPIO33"][1] - 6))
    ldrm = s.module(130, 560, 150, 90, "LDR + 10 kΩ", right=["node", "3V3"], tsize=13)
    s.wire([esp["GPIO34"], (310, esp["GPIO34"][1]), (310, ldrm["node"][1]), ldrm["node"]], CSIG, "GPIO34", (365, esp["GPIO34"][1] - 6))
    s.wire([esp["3V3"], (295, esp["3V3"][1]), (295, ldrm["3V3"][1]), ldrm["3V3"]], C3V3)
    amp = s.module(820, 150, 160, 120, "PAM8302", left=["A+", "SD"], right=["OUT"], tsize=13)
    s.speaker(1010, amp["OUT"][1]); s.wire([amp["OUT"], (1010, amp["OUT"][1])], CAUD)
    s.wire([esp["GPIO25 (DAC1)"], (740, esp["GPIO25 (DAC1)"][1]), (740, amp["A+"][1]), amp["A+"]], CAUD, "1 kΩ", (770, amp["A+"][1] - 6))
    s.wire([esp["GPIO26"], (725, esp["GPIO26"][1]), (725, amp["SD"][1]), amp["SD"]], CSIG)
    sd = s.module(820, 320, 160, 160, "µSD (3.3 V)", left=["CS", "SCK", "MISO", "MOSI"], tsize=13)
    for pin, gp, xo in (("CS", "GPIO5 CS", 760), ("SCK", "GPIO18 SCK", 745), ("MISO", "GPIO19 MISO", 730), ("MOSI", "GPIO23 MOSI", 715)):
        s.wire([esp[gp], (xo, esp[gp][1]), (xo, sd[pin][1]), sd[pin]], CSIG)
    dl = s.module(820, 520, 160, 70, "dump load (opt.)", left=["gate"], tsize=13)
    s.wire([esp["GPIO4 (opt.)"], (700, esp["GPIO4 (opt.)"][1]), (700, dl["gate"][1]), dl["gate"]], CSIG, "1 kΩ", (770, dl["gate"][1] - 6))
    s.text(550, 580, "BOOT button = GPIO0 · status LED = GPIO2 (both on-board)", 11, anchor="middle", fill=CNOTE)
    s.legend(340, 670, LEG)
    s.save("09_overview.svg")


if __name__ == "__main__":
    for fn in (power_star, led_shifter, ultrasonic, ldr, audio_sd, dump_load, pinmap, assembly, overview):
        fn()
