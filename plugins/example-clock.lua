-- CaelestiaWin example plugin: a desktop clock + CPU bar.
-- Edit and save; the shell reloads it within a second.
plugin = { name = "Clock & CPU", surface = "desktop", x = 60, y = 60, w = 260, h = 132 }

function draw(ctx)
  local a = cw.accent()
  cw.rect_fill(ctx.x, ctx.y, ctx.w, ctx.h, cw.color(0,0,0,110), 16)
  cw.rect(ctx.x, ctx.y, ctx.w, ctx.h, cw.color(255,255,255,40), 16, 1.0)

  local t = os.date("%H:%M")
  cw.text(ctx.x + 18, ctx.y + 14, 34, cw.color(255,255,255,235), t)
  cw.text(ctx.x + 18, ctx.y + 58, 14, cw.color(255,255,255,150), os.date("%A, %d %B"))

  local s = cw.stats()
  local w = ctx.w - 36
  cw.rect_fill(ctx.x + 18, ctx.y + 92, w, 8, cw.color(255,255,255,45), 4)
  cw.rect_fill(ctx.x + 18, ctx.y + 92, w * s.cpu, 8, a, 4)
  cw.text(ctx.x + 18, ctx.y + 104, 13, cw.color(255,255,255,170),
          string.format("CPU %.0f%%   RAM %.0f%%", s.cpu*100,
                        (s.mem_total > 0) and (s.mem_used/s.mem_total*100) or 0))
end
