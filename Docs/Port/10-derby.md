# Home Run Derby: design spec

A new motion game (no web original). Catalog id `derby`, level `L_Derby`, 1-4 batters, turn-based (MaxLocal 4,
shared controls). The phone is the bat.

## 1. Rules (`Games/Derby/DerbyRules`, tests `MovecadeKart.Derby.{Schedule,Contact,Match}`)

- RNG: Mulberry32. Round seed `seed ^ (0x9E3779B9 * (round + 1))`; every batter in a round faces the same ten pitches.
- Schedule: five draws per pitch (type, speed, windup, contact bias, break), used or not. Pitches 1-3 fastballs;
  4-6 fastball (r < .6) or changeup; 7-10 fastball (< .3), changeup (< .6) or curve.
  Fastball `88 + 7r + 0.4i` mph, windows 30/165 ms; changeup `77 + 5r`, 28/155; curve `72 + 6r`, 24/140.
  Flight `17 m / speed * 1.06`; windup `1300 + 600r` ms; bias `2r - 1`.
- Timing quality `q`: 1 inside the perfect window, `1 - ((|dt| - perfect) / (contact - perfect))^1.25` to the edge,
  a whiff beyond it.
- Contact: spray `28 aim + 32 dt/contact + sign(dt) 25 (1 - q)^2` (degrees, + right field; early pulls);
  launch `29 + 2 strength + bias 40 (1 - q)^0.7 - 4 (1 - q)`;
  exit `(62 + 38 strength)(0.42 + 0.58 q^0.9) + 0.18 pitch mph`.
- Flight: 10 ms semi-implicit Euler from 1 m, drag `0.005 |v| v`, backspin lift `0.0016 |v|` perpendicular, g 9.81.
  Fence `100.584 + 21.336 cos(2 spray)` m (330 ft lines, 400 centre), 3 m tall: over it is a home run, under it
  the ball stops at the wall. Foul past 45 degrees (or launch under -5).
- Points: moonshot (HR >= 450 ft) `1.5 ft`, home run `ft`, in play `ft / 10`. Takes and whiffs are strikes, fouls
  until two. Ten pitches or three strikes per turn; rounds (default 1) cycle the order.

## 2. Swing detector (`Games/Derby/DerbySwing`, tests `MovecadeKart.Derby.Swing`)

Gyro magnitude onset 240 deg/s, must peak >= 420, ends under max(onset, 45% of peak) or after 700 ms; refractory
450 ms, held open while the follow-through keeps moving. Strength from the peak (full at 1300 deg/s). Direction
from the calibrated quaternion: the barrel is the phone's +Y; its ground heading at the peak (0 at the screen)
against square contact (+-90 in the swing's sense, from the yaw rate about vertical: + is right-handed); past
square pulls. No quaternion: the swing counts, aimed through the middle.

## 3. Play (`ADerbyGame`)

Intro, step-in card, set (1 s; a phone batter 2.4 s or READY), windup, pitch, flight or the call. Local: hold
Space / RT / A / click to load (0.55 s to full), release to swing (peak 110 ms later); mouse, A/D or stick aim.
Bots: Gaussian timing error, sigma `16 + 90 (1 - skill)` (+18 curves), changeups fool them early. A phone's swing
peak is mapped to the game clock through the detector's clock less 60 ms of latency; phones get 350 ms of grace
after the window before a take is called.

## 4. Party Mode

`advance-turn` names the batter; `pad:prompt` (mode `swing`, A = READY) per phone; sensor frames feed the detector;
`pad:press {button: a}` is READY while the pitcher waits and a button swing while the ball is on its way;
`derby:pitch {playerId, pitch, type, mph, arriveInMs}` as each pitch leaves the hand; `play:feedback` per pitch,
live scores, `complete-game`. The server and phone side is the motion pad patch (0002).

## 5. Presentation

`ADerbyBallpark`: night park built in code (mown outfield, infield, wall with markers and ads, foul poles,
bleachers and grandstand full of bouncing fans, six light towers, scoreboard, stars and moon). `ADerbyGame`:
pitcher and batter avatars (windup, swing arc with the bat in the hands), ball with a trail, contact sparks and
hit-stop, broadcast camera on the ball, fireworks and crowd on home runs, slow motion through a moonshot's arc.
`SDerbyHud`: score bug with the count, standings, radar gun, timing, live distance, the call, aim and load.
