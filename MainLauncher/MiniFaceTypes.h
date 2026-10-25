#ifndef MINIFACE_TYPES_H
#define MINIFACE_TYPES_H

// ---------------- NAPI RITMUS ----------------
enum DayPhase { DAY_SLEEP, DAY_WAKING, DAY_ACTIVE, DAY_WINDDOWN };

// ---------------- MINIFACE ALLAPOTOK ----------------
enum MiniFaceState {
  MF_NONE, MF_THINKING, MF_JOY, MF_WINK, MF_DREAM,
  MF_SLEEP, MF_WAKE,
  MF_CHARGE_HAPPY, MF_FEED_HAPPY,
  MF_CIRCLE_SEQ, MF_EXPLODE, MF_DART, MF_SINK_REPEAT,
  MF_GAME_WIN, MF_GAME_LOSE,
  MF_INFO_TIME, MF_INFO_WEATHER,
  MF_INFO_KIND            // <-- UJ: kedves uzenet info-arc
};

// A kozponti esemeny-utemezo tipusai, sullyal - magasabb szam = gyakoribb
enum FaceEvent {
  EVT_NONE,          // nem tortenik semmi ebben a korben, csak tovabb nezelodik
  EVT_SHAPE_CURIOUS,
  EVT_SHAPE_CUTE,
  EVT_WINK,
  EVT_THINKING,
  EVT_JOY,
  EVT_DREAM,
  EVT_CIRCLE_SEQ,
  EVT_DART,
  EVT_SINK_REPEAT,
  EVT_EXPLODE,
  EVT_INFO_TIME,
  EVT_INFO_WEATHER,
  EVT_INFO_KIND,     // <-- UJ: kedves uzenet
  EVT_COUNT
};

// ---------------- EHSEG ----------------
enum HungerState { HUNGER_IDLE, HUNGER_CIRCLE, HUNGER_SAD };

// ---------------- SZEM-FORMA ----------------
enum EyeShapeType { SHAPE_NORMAL, SHAPE_WORRIED, SHAPE_ANGRY, SHAPE_CURIOUS, SHAPE_CUTE, SHAPE_FOCUSED, SHAPE_CIRCLE };

struct ShapeParams {
  float w, h, cornerRadius;
  float outerTopCut, innerTopCut, bottomCurveIn;
};

// ---------------- ROBBANAS RESZECSKEK ----------------
struct Particle { float x, y, vx, vy; bool active; };

// ---------------- GOMB ALLAPOT ----------------
struct ButtonState {
  bool pressed = false;
  unsigned long pressStart = 0;
  bool longFired = false;
};

// ---------------- SZEM-SZINKRON JATEK FORMAI ----------------
enum SyncShape { SYNC_CURIOUS, SYNC_FLAT, SYNC_CIRCLE, SYNC_DIAMOND, SYNC_TRIANGLE, SYNC_SHAPE_COUNT };

#endif
