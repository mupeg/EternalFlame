#include <FastLED.h>

// ============================================================================
// 1. PIN DEFINITIONS
// ============================================================================
#define PIN_BUTTON       2    
#define PIN_POOFER_MAIN  3    
#define PIN_POOFER_AUX1  4    
#define PIN_POOFER_AUX2  5    
#define PIN_POOFER_AUX3  6    
#define PIN_FIRE_DATA    7    
#define PIN_FUSE_DATA    8    

// ============================================================================
// 2. HARDWARE GEOMETRY
// ============================================================================
const int tube1H = 20;        
const int tube2H = 30;        
const int tube3H = 20;        
const int numFuseLED = 40;    

// ============================================================================
// 3. GAMEPLAY & ODDS
// ============================================================================
float winPercentage = 30.0;             
unsigned long minTimeTillNextWinMs = 30000; // 30 second lockout

int pooferAux1Chance = 50;  
int pooferAux2Chance = 30;  
int pooferAux3Chance = 0;

// ============================================================================
// 4. TIMING & SPEEDS
// ============================================================================
unsigned long targetFuseTimeMs = 9000;  // 9 second burn on win
unsigned long pooferTime = 1000;        // 1 second blast
unsigned long pooferNextWait = 500;     // 0.5s between aux tosses

float lossSpeedMult = 2.5;    
float returnSpeedMult = 5.0;  

// ============================================================================
// 5. INTERNAL LOGIC & BUFFERS
// ============================================================================
const int segLens[] = {tube1H, tube2H, tube3H};
const int numFireLED = tube1H + tube2H + tube3H; 
const int NUM_PHYSICAL_FIRE = (tube1H * 3) + (tube2H * 3) + (tube3H * 3);

CRGB fire[NUM_PHYSICAL_FIRE];
CRGB fuse[numFuseLED];
byte heat[numFireLED];

float winSpeed, lossSpeed, returnSpeed;
enum GamePhase { IDLE, FUSE_BURN, POOFING };
GamePhase currentPhase = IDLE;
bool isWinner = false, isHighIntensity = false, isReturning = false, buttonArmed = true;
float fusePos = -5.0;      
int lossPoint = 0, auxStep = 0;
unsigned long lastWinMs = 0, pooferOffTime = 0, nextTossMs = 0, lastDebounceTime = 0, debounceDelay = 50;
bool lastButtonState = HIGH;

void setup() {
  int safePins[] = {PIN_POOFER_MAIN, PIN_POOFER_AUX1, PIN_POOFER_AUX2, PIN_POOFER_AUX3};
  for(int p : safePins) { digitalWrite(p, LOW); pinMode(p, OUTPUT); digitalWrite(p, LOW); }

  Serial.begin(115200);
  while (!Serial && millis() < 2000); 
  
  float totalFrames = targetFuseTimeMs / 16.67; 
  winSpeed = (numFuseLED + 5) / totalFrames;
  lossSpeed = winSpeed * lossSpeedMult;
  returnSpeed = winSpeed * returnSpeedMult;

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  FastLED.addLeds<WS2812B, PIN_FIRE_DATA, GRB>(fire, NUM_PHYSICAL_FIRE);
  FastLED.addLeds<WS2812B, PIN_FUSE_DATA, GRB>(fuse, numFuseLED);
  FastLED.setBrightness(255);

  Serial.println("--- Full Ignition System Loaded ---");
}

void loop() {
  fireUpdate();
  handleButton();

  switch (currentPhase) {
    case FUSE_BURN:
      if (fuseUpdate()) {
        if (isWinner) triggerPooferSequence();
        else currentPhase = IDLE;
      }
      break;
    case POOFING:
      updatePooferLogic();
      break;
    default: break; 
  }
  mapAndShow();
}

void handleButton() {
  if (currentPhase != IDLE) return;
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonState) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && buttonArmed) {
      unsigned long timeSinceLast = millis() - lastWinMs;
      if (timeSinceLast >= minTimeTillNextWinMs) {
        isWinner = (random(0, 101) <= winPercentage);
        lossPoint = isWinner ? numFuseLED : random(numFuseLED * 0.35, numFuseLED * 0.75);
        fusePos = 0; isReturning = false; buttonArmed = false; 
        currentPhase = FUSE_BURN;
      } else {
        buttonArmed = false;
      }
    }
  }
  if (reading == HIGH) buttonArmed = true; 
  lastButtonState = reading;
}

void fireUpdate() {
  int cooling = 100;
  int sparking = 60;

  if (isHighIntensity) {
    cooling = 40; sparking = 220;
  } else if (currentPhase == FUSE_BURN && isWinner) {
    sparking = 60 + (80 * (fusePos / numFuseLED)); // Excitement build-up
  }

  for (int i = 0; i < numFireLED; i++) heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / numFireLED) + 2));
  for (int k = numFireLED - 1; k >= 2; k--) heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  if (random8() < sparking) heat[random8(7)] = qadd8(heat[random8(7)], random8(160, 255));
}

bool fuseUpdate() {
  fill_solid(fuse, numFuseLED, CRGB::Black);
  float currentSpeed = isWinner ? winSpeed : lossSpeed;

  if (!isReturning) {
    fusePos += currentSpeed;
    for (int i = 0; i < 5; i++) {
      int idx = (int)fusePos - i;
      if (idx >= 0 && idx < numFuseLED) {
        if (i == 0)      fuse[idx] = CRGB::White;
        else if (i < 3)  fuse[idx] = CRGB::Yellow;
        else             fuse[idx] = CRGB::Red;
        fuse[idx].fadeToBlackBy(i * 51);
      }
    }
    if (isWinner && fusePos >= numFuseLED) return true; 
    if (!isWinner && fusePos >= lossPoint) isReturning = true;
  } else {
    fusePos -= returnSpeed;
    for (int i = 0; i < 5; i++) {
      int idx = (int)fusePos + i;
      if (idx >= 0 && idx < numFuseLED) {
        fuse[idx] = CRGB::DarkOrange;
        fuse[idx].fadeToBlackBy(i * 51);
      }
    }
    if (fusePos <= -5) return true; 
  }
  return false;
}

void mapAndShow() {
  static unsigned long lastShow = 0;
  if (millis() - lastShow < 16) return; 
  lastShow = millis();

  int pOff = 0, lOff = 0;
  for (int s = 0; s < 3; s++) {
    int h = segLens[s];
    for (int i = 0; i < h; i++) {
      CRGB color = HeatColor(heat[lOff + i]);
      // --- FINAL SPARK LOGIC ---
      // If we just ignited, make the bottom of the first tube flash white
      if (isHighIntensity && s == 0 && i < 3 && (millis() - (pooferOffTime - pooferTime) < 50)) {
        color = CRGB::White;
      }
      fire[pOff + i] = color;
      fire[pOff + (h * 2) - 1 - i] = color;
      fire[pOff + (h * 2) + i] = color;
    }
    pOff += (h * 3); lOff += h;
  }
  FastLED.show();
}

void triggerPooferSequence() {
  digitalWrite(PIN_POOFER_MAIN, HIGH);
  isHighIntensity = true;
  pooferOffTime = millis() + pooferTime;
  nextTossMs = millis() + pooferNextWait;
  auxStep = 1;
  currentPhase = POOFING;
  Serial.println(">>> BOOM! Ignition Successful.");
}

void updatePooferLogic() {
  unsigned long now = millis();
  if (auxStep >= 1 && auxStep <= 3 && now >= nextTossMs) {
    int pins[] = {0, PIN_POOFER_AUX1, PIN_POOFER_AUX2, PIN_POOFER_AUX3};
    int chance = (auxStep == 1) ? pooferAux1Chance : (auxStep == 2 ? pooferAux2Chance : pooferAux3Chance);
    if (random(0, 101) < chance) {
      digitalWrite(pins[auxStep], HIGH);
      pooferOffTime = now + pooferTime; 
      nextTossMs = now + pooferNextWait;
      auxStep++;
    } else { auxStep = 0; }
  }
  if (now >= pooferOffTime) {
    digitalWrite(PIN_POOFER_MAIN, LOW);
    digitalWrite(PIN_POOFER_AUX1, LOW);
    digitalWrite(PIN_POOFER_AUX2, LOW);
    digitalWrite(PIN_POOFER_AUX3, LOW);
    isHighIntensity = false;
    lastWinMs = now;
    isWinner = false;
    currentPhase = IDLE;
  }
}