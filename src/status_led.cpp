#include "status_led.h"

#include "button_gestures.h"

bool runStatusLedSelfTest() {
  StatusLedIndicator led;
  if (led.on(0, JigglerMode::Medium, false) || led.remainingMs(0)) return false;
  const JigglerMode modes[] = {JigglerMode::Slow, JigglerMode::Medium, JigglerMode::Fast};
  const uint32_t periods[] = {1200, 500, 180};
  for (unsigned i = 0; i < 3; ++i) {
    led.wake(100);
    if (!led.on(100, modes[i], true) || !led.on(100 + periods[i] / 2 - 1, modes[i], true)) return false;
    if (led.on(100 + periods[i] / 2, modes[i], true) || !led.on(100 + periods[i], modes[i], true)) return false;
    if (led.remainingMs(4099) != 1 || led.on(4100, modes[i], true) || led.on(10000, modes[i], true)) return false;
    // A fresh press must work after the first four-second window has expired.
    led.wake(12000);
    if (!led.on(12000, modes[i], true) || led.remainingMs(12000) != 4000 || led.on(16000, modes[i], true)) return false;
    // OFF cancels the window immediately, including a currently lit phase.
    led.wake(17000);
    if (!led.on(17000, modes[i], true)) return false;
    led.stop();
    if (led.on(17001, modes[i], false) || led.remainingMs(17001) || led.on(17500, modes[i], true)) return false;
    led.wake(18000);
    if (!led.on(18000, modes[i], true)) return false;
    // Safety gate prevents light when OFF even if a stale window exists.
    if (led.on(18001, modes[i], false) || led.remainingMs(18001)) return false;
  }
  // Short ON, short OFF before expiry, ON again, active double click, then
  // an OFF double click: indication must follow the resulting ON/OFF state.
  ButtonGestures button(30, 300);
  JigglerEngine engine;
  unsigned singles = 0, doubles = 0;
  const auto sample = [&](bool raw, uint32_t now) {
    const ButtonGestureEvent event = button.update(raw, now);
    if (event == ButtonGestureEvent::ShortPress) {
      ++singles;
      engine.setActive(!engine.active(), now);
      if (engine.active()) led.wake(now);
      else led.stop();
    } else if (event == ButtonGestureEvent::DoublePress) {
      ++doubles;
      engine.setMode(nextJigglerMode(engine.mode()), now);
      if (engine.active()) led.wake(now);
    }
  };
  sample(true, 20000); sample(false, 20010);  // Bounce must not wake.
  if (led.on(20010, engine.mode(), engine.active())) return false;
  sample(true, 20100); sample(true, 20130);
  if (led.on(20130, engine.mode(), engine.active())) return false;
  sample(false, 20180); sample(false, 20210); sample(false, 20511);
  if (singles != 1 || !engine.active() || !led.on(20511, engine.mode(), engine.active())) return false;
  sample(true, 21000); sample(true, 21030);
  sample(false, 21080); sample(false, 21110); sample(false, 21411);
  if (singles != 2 || engine.active() || led.remainingMs(21411) || led.on(21411, engine.mode(), false)) return false;
  sample(true, 25000); sample(true, 25030);
  sample(false, 25080); sample(false, 25110);
  sample(false, 25411);
  if (singles != 3 || !engine.active() || !led.on(25411, engine.mode(), true)) return false;
  sample(true, 26000); sample(true, 26030);
  sample(false, 26080); sample(false, 26110);
  sample(true, 26200); sample(true, 26230);
  sample(false, 26280); sample(false, 26310);
  if (doubles != 1 || !engine.active() || engine.mode() != JigglerMode::Fast || led.remainingMs(26310) != 4000) return false;
  if (!led.on(26310, engine.mode(), true) || led.on(30310, engine.mode(), true)) return false;
  sample(true, 31000); sample(true, 31030);
  sample(false, 31080); sample(false, 31110); sample(false, 31411);
  if (singles != 4 || engine.active() || led.remainingMs(31411)) return false;
  sample(true, 32000); sample(true, 32030);
  sample(false, 32080); sample(false, 32110);
  sample(true, 32200); sample(true, 32230);
  sample(false, 32280); sample(false, 32310);
  if (doubles != 2 || engine.active() || engine.mode() != JigglerMode::Slow || led.remainingMs(32310)) return false;
  if (led.on(32310, engine.mode(), false)) return false;
  // Unsigned elapsed time must remain correct across millis() rollover.
  led.wake(UINT32_MAX - 1000);
  if (!led.on(UINT32_MAX - 1000, JigglerMode::Slow, true) || led.remainingMs(2998) != 1 || led.on(2999, JigglerMode::Slow, true)) return false;
  return true;
}
