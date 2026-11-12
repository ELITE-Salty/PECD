"""Provided image-processing helpers from last year's leafs.py.

Read the color rules at checkpoint 5. The ONNX pipeline belongs in main.py.
"""
import sys
import cv2
import numpy as np

MAX_SIDE = 1200
MIN_LEAF_PX = 1500

def read_and_resize(path, max_side=MAX_SIDE):
    img = cv2.imread(path)
    if img is None:
        print(f"ERROR: cannot read {path}", file=sys.stderr); sys.exit(1)
    h, w = img.shape[:2]
    if max(h, w) > max_side:
        s = max_side / float(max(h, w))
        img = cv2.resize(img, (int(w*s), int(h*s)), interpolation=cv2.INTER_AREA)
    return img

def split_leaf_masks(bin_mask):
    num, labels, stats, _ = cv2.connectedComponentsWithStats(bin_mask, connectivity=8)
    masks = []
    for idx in range(1, num):
        area = int(stats[idx, cv2.CC_STAT_AREA])
        if area >= MIN_LEAF_PX:
            m = np.zeros_like(bin_mask, np.uint8)
            m[labels == idx] = 255
            masks.append(m)
    masks.sort(key=lambda m: int(m.sum()), reverse=True)
    return masks

def color_fracs(img_bgr, mask):
    hsv = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2HSV)
    inside = (mask == 255)
    if np.count_nonzero(inside) == 0:
        return dict(green=0.0, yellow=0.0, brown=0.0)
    H,S,V = cv2.split(hsv)
    h, s, v = H[inside], S[inside], V[inside]
    total = float(h.size)
    green  = np.count_nonzero((h>=35)&(h<=85)&(s>=30)&(v>=30)) / total
    yellow = np.count_nonzero((h>=20)&(h<35)&(s>=20)&(v>=40)) / total
    brown  = np.count_nonzero((h>=5)&(h<25)&(v<=200)) / total
    return dict(green=green, yellow=yellow, brown=brown)

def health_from_colors(fr):
    g = fr["green"]; yb = fr["yellow"] + fr["brown"]
    if g > yb + 0.05:  return "healthy-ish"
    if yb > g + 0.05:  return "stressed-ish"
    return "borderline"

def draw_overlay(img, masks, lines):
    out = img.copy()
    idx = np.zeros(img.shape[:2], np.uint16)
    for i, m in enumerate(masks, 1):
        cnts, _ = cv2.findContours((m>0).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if cnts:
            cv2.drawContours(out, cnts, -1, (0,0,255), 2)
            x,y,w,h = cv2.boundingRect(cnts[0])
            cv2.putText(out, f"Leaf {i}", (x, max(20,y-8)), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255,255,255), 2)
        idx[m==255] = i
    # legend box
    x0,y0 = 10,22
    cv2.rectangle(out, (x0-8,y0-20), (x0+620, y0-20+22*len(lines)+24), (0,0,0), -1)
    y = y0
    for s in lines:
        cv2.putText(out, s, (x0,y), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255,255,255), 2)
        y+=22
    return out, idx

