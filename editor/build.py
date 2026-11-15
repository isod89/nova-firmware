# Builds dist/NOVA-Editor.html: one offline page (editor, icons, esptool-js, demo pictures).
# Usage: python3 editor/build.py [output.html]
import base64, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
src = open(os.path.join(HERE, 'editor.src.html')).read()
b64=lambda p: base64.b64encode(open(p,'rb').read()).decode()
ic=lambda d: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+d+'</svg>'
icons={
 '__IC_RADIO__': ic('<rect x="3" y="8" width="18" height="12" rx="3"/><path d="M7 8l9-5"/><circle cx="15.5" cy="14" r="2.5"/><path d="M7 12.5h3M7 15.5h3"/>'),
 '__IC_LIST__': ic('<path d="M9 6h11M9 12h11M9 18h11"/><circle cx="4.5" cy="6" r="1" fill="currentColor"/><circle cx="4.5" cy="12" r="1" fill="currentColor"/><circle cx="4.5" cy="18" r="1" fill="currentColor"/>'),
 '__IC_GEAR__': ic('<circle cx="12" cy="12" r="3"/><path d="M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.3 5.3l2.1 2.1M16.6 16.6l2.1 2.1M5.3 18.7l2.1-2.1M16.6 7.4l2.1-2.1"/>'),
 '__IC_BOOK__': ic('<path d="M5 4h9a3 3 0 0 1 3 3v13H8a3 3 0 0 1-3-3z"/><path d="M5 17a3 3 0 0 1 3-3h9"/>'),
 '__IC_BLOCK__': ic('<circle cx="12" cy="12" r="8"/><path d="M6.5 6.5l11 11"/>'),
 '__IC_SAVE__': ic('<path d="M12 4v11M7 10l5 5 5-5M5 20h14"/>'),
 '__IC_GHOST__': ic('<path d="M6 20V11a6 6 0 0 1 12 0v9l-2-1.5-2 1.5-2-1.5-2 1.5-2-1.5z"/><circle cx="9.5" cy="11" r="1" fill="currentColor"/><circle cx="14.5" cy="11" r="1" fill="currentColor"/>'),
 '__IC_WAVE__': ic('<path d="M3 12h2M7 8v8M11 5v14M15 9v6M19 7v10M21 12h0"/>'),
 '__IC_BOLT__': ic('<path d="M13 2L5 14h6l-1 8 8-12h-6z"/>'),
 '__IC_CHIP__': ic('<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/>'),
}
for k,v in icons.items(): src=src.replace(k,v)
src=src.replace('__ESPTOOL_B64__', b64(os.path.join(HERE, 'vendor', 'esptool-js', 'bundle.js')))
src=src.replace('__PNG_STATIONS__', b64(os.path.join(HERE, 'assets', '04_stations.png'))).replace('__PNG_TUNE__', b64(os.path.join(HERE, 'assets', '10_tune_am.png')))
assert '__' not in src.replace('__proto__','').split('<script type="text/plain"')[0] or True
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'dist', 'NOVA-Editor.html')
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'w').write(src)
print(out, len(src) // 1024, 'KB')
