#!/usr/bin/env python3
"""Extract reproducible 320x240 HTML UI geometry without screenshots.

Requires Playwright + Chromium. --full emits every rendered node/style;
default emits the stable anchor baseline for review and checks.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "prototypes/esp32-ui/index.html"
OUT = ROOT / "prototypes/esp32-ui/ui-baseline.json"
PROPS = ["color", "backgroundColor", "backgroundImage", "borderRadius", "borderTopWidth",
         "borderTopColor", "boxShadow", "fontSize", "fontFamily", "fontWeight",
         "lineHeight", "letterSpacing", "textAlign", "opacity", "animationName",
         "animationDuration", "animationTimingFunction", "display", "overflow"]
JS = """(props) => {
 const root=document.querySelector('.screen'), box=root.getBoundingClientRect();
 const page=document.querySelector('.page.active');
 function inspect(el) {
   const rect=el.getBoundingClientRect(), cs=getComputedStyle(el);
   const tree=[]; let n=el;
   while(n && n!==page) {const p=n.parentElement; if(!p)break;
     tree.unshift(Array.prototype.indexOf.call(p.children,n));n=p;}
   const style={}; for(const k of props) {const v=cs[k]; if(v && v!=='none' && v!=='normal' && v!=='0px' && v!=='auto')style[k]=v;}
   const r=n=>Math.round(n*100)/100;
   return {path:tree.join('.'),tag:el.tagName.toLowerCase(),id:el.id||undefined,
    classes:(el.className?.baseVal??el.className)||undefined,
    action:el.dataset.act||undefined,role:el.getAttribute('aria-label')||undefined,
    text:(el.children.length===0?el.textContent.trim():undefined)||undefined,
    icon:el.querySelector(':scope > use')?.getAttribute('href')||undefined,
    bounds:[r(rect.x-box.x),r(rect.y-box.y),r(rect.width),r(rect.height)],style};
 }
 return {page:page.dataset.page,items:Array.from([page,...page.querySelectorAll('*')])
   .filter(e=>{const c=getComputedStyle(e);return c.display!=='none'&&c.visibility!=='hidden'&&e.getBoundingClientRect().width>0;})
   .map(inspect)};
}"""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, default=OUT)
    ap.add_argument("--chromium", default=os.environ.get("CHROMIUM", ""))
    ap.add_argument("--full", action="store_true", help="emit every rendered node and computed style")
    args = ap.parse_args()
    with sync_playwright() as p:
        options = {"headless": True, "args": ["--no-sandbox", "--disable-dev-shm-usage"]}
        if args.chromium:
            options["executable_path"] = args.chromium
        browser = p.chromium.launch(**options)
        page = browser.new_page(viewport={"width": 900, "height": 650}, device_scale_factor=1,
                                locale="zh-CN", reduced_motion="reduce")
        page.set_content(SOURCE.read_text(encoding="utf-8"), wait_until="load")
        # HTML device scales for desktop preview; LCD geometry is 320x240 unscaled.
        page.add_style_tag(content=".screen{transform:none!important;top:0!important;left:0!important}")
        snapshots = {}
        def snap(name):
            snapshots[name] = page.evaluate(JS, PROPS)
        def press(action):
            page.locator(f'.page.active [data-act="{action}"]').first.click()
        snap("home")
        press("talk");snap("voice.listening")
        press("voice-simulate");snap("voice.thinking")
        page.wait_for_timeout(950);snap("voice.speaking")
        press("home");press("story");snap("player.story.playing")
        press("pause");snap("player.story.paused")
        press("toggle-volume");snap("player.story.volume")
        press("home");press("song");snap("player.song.playing")
        press("home");press("calendar");snap("calendar.default")
        press("cal-detail");snap("calendar.detail")
        press("cal-close");press("cal-next");snap("calendar.next-month")
        press("home");press("settings");snap("settings.child-on")
        page.locator("#kid-toggle").uncheck();snap("settings.child-off")
        symbols=page.locator("svg.symbols symbol").evaluate_all(
            "els => Object.fromEntries(els.map(e=>[e.id,e.outerHTML]))")
        css=page.locator("style").all_text_contents()
        browser.close()
    content=SOURCE.read_bytes()
    result={"schema":1,"source":"prototypes/esp32-ui/index.html",
            "source_sha256":hashlib.sha256(content).hexdigest(),
            "viewport":{"width":320,"height":240,"orientation":"landscape","dpr":1},
            "snapshots":snapshots,"svg_symbols":symbols,
            "actions":sorted(set(re.findall(r'data-act="([^"]+)"',content.decode()))),
            "css_keyframes":sorted(set(re.findall(r'@keyframes\s+([\w-]+)',"\n".join(css))))}
    if not args.full:
        selected = {"home", "voice.listening", "player.story.playing",
                    "player.story.volume", "calendar.default", "calendar.detail",
                    "settings.child-on"}
        compact = {key: result[key] for key in ("schema", "source", "source_sha256", "viewport")}
        compact["icons"] = list(symbols)
        compact["actions"] = result["actions"]
        compact["keyframes"] = result["css_keyframes"]
        compact["states"] = {}
        for name, state in snapshots.items():
            if name not in selected:
                continue
            anchors = []
            for obj in state["items"]:
                cls, obj_id, act = (obj.get("classes") or "", obj.get("id") or "",
                                    obj.get("action") or "")
                if not (cls or obj_id or act) or cls.startswith("day") or cls.startswith("calendar-days"):
                    continue
                if obj["tag"] in {"path", "ellipse", "use", "svg", "circle", "rect", "i",
                                  "span", "small", "b", "strong", "label"} and not (act or obj_id):
                    continue
                key = obj_id or ("@" + act if act else "." + cls.replace(" ", "."))
                style = {k: v for k, v in obj["style"].items()
                         if k in {"fontSize", "borderRadius"} and v != "0px"}
                anchors.append([key, [round(x) for x in obj["bounds"]], style])
            compact["states"][name] = {"page": state["page"], "anchors": anchors}
        result = compact
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n",
                           encoding="utf-8")
    print(f"Wrote {args.output}: {len(snapshots)} sampled states; {len(symbols)} SVG symbols")

if __name__ == "__main__":
    main()
