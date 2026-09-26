CaelestiaWin plugins
====================
Drop a .lua file in this folder. It is loaded at startup and re-loaded whenever you save it.

A plugin declares itself and provides draw(ctx):

  plugin = { name="My widget", surface="desktop", x=60, y=60, w=260, h=132 }
  function draw(ctx) ... end

  surface = "desktop"  draws on the wallpaper at x,y with size w,h
  surface = "bar"      gets a slot h pixels tall in the taskbar (x,y,w are given to you)

ctx = { x, y, w, h, dt, hovered, clicked } - x,y is your slot's top-left on screen.
Drawing outside your slot is clipped away.

API (global table cw):
  cw.color(r,g,b[,a])            -> colour value
  cw.accent() cw.ink() cw.ink2() cw.card()   -> the current theme's colours
  cw.rect_fill(x,y,w,h,col[,round])
  cw.rect(x,y,w,h,col[,round][,thickness])
  cw.line(x1,y1,x2,y2,col[,thickness])
  cw.circle(x,y,r,col[,thickness])   cw.circle_fill(x,y,r,col)
  cw.text(x,y,size,col,str)          cw.text_w(size,str) -> width
  cw.stats()   -> cpu, gpu, cpu_temp, gpu_temp, mem_used, mem_total, disk_used,
                  disk_total, net_down, net_up, battery, charging, online
  cw.media()   -> title, artist, album, playing, pos, dur
  cw.weather() -> ok, city, text, temp, feels, wind, humidity, code
  cw.mouse()   -> x, y, down, clicked
  cw.now()     -> milliseconds since boot (for animation)
  cw.open(path_or_url)               cw.log(text)  (shown on the Plugins page)

Sandbox: io, package, require, load/loadfile/dofile, debug and the dangerous os.*
functions are removed. A plugin that loops forever is stopped with an error rather
than freezing the shell, and an erroring plugin is switched off until you save it again.
